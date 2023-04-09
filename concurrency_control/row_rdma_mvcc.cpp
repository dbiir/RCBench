/*
   Copyright 2016 Massachusetts Institute of Technology

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

//#include "mvcc.h"
#include "txn.h"
#include "row.h"
#include "manager.h"
#include "rdma.h"
#include "row_rdma_mvcc.h"
#include "mem_alloc.h"
#include "qps/op.hh"
#if CC_ALG == RDMA_MVCC
void Row_rdma_mvcc::init(row_t * row) {
	_row = row;
}

RC Row_rdma_mvcc::access(yield_func_t &yield, TxnManager * txn, Access *access, access_t type, uint64_t cor_id) {
	RC rc = RCOK;
    if(type == RD){
        //local read;
        row_t *temp_row = (row_t *)mem_allocator.alloc(row_t::get_row_size(ROW_DEFAULT_SIZE));
        memcpy(temp_row, _row, row_t::get_row_size(ROW_DEFAULT_SIZE));
        
        uint64_t change_num = 0;
		bool result = false;
		result = txn->get_version(temp_row,&change_num,txn->txn);
		if(result == false){//no proper version: Abort
			DEBUG_T("local %ld no version\n",temp_row->get_primary_key());
			rc = Abort;
			return rc;
		}
		//check txn_id
		if(temp_row->txn_id[change_num] != 0 && temp_row->txn_id[change_num] != txn->get_txn_id() + 1){
			DEBUG_T("local %ld write by other %ld\n",temp_row->get_primary_key(),temp_row->txn_id[change_num]);
			rc = Abort;
			return rc;
		}
        uint64_t version = change_num;
		uint64_t old_rts = temp_row->rts[version];
		uint64_t new_rts = txn->get_timestamp();
		uint64_t rts_offset = access->offset + 2*sizeof(uint64_t) + HIS_CHAIN_NUM*sizeof(uint64_t) + version*sizeof(uint64_t);
		uint64_t cas_result;
		rc = txn->cas_remote_content(access->location,rts_offset,old_rts,new_rts,cas_result);//lock
		if(rc == Abort || cas_result!=old_rts){ //CAS fail, atomicity violated
			DEBUG_T("local %ld rts update failed old %ld now %ld new %ld\n",temp_row->get_primary_key(), old_rts, cas_result, new_rts);
			rc = Abort;
			return rc;			
		}
		//read success
		temp_row->rts[version] = txn->get_timestamp();
     	
        txn->cur_row->copy(temp_row);
		mem_allocator.free(temp_row,row_t::get_row_size(ROW_DEFAULT_SIZE));
		rc = RCOK;
		return rc;
    }
    else if(type == WR){
        uint64_t lock = txn->get_txn_id()+1;
		uint64_t try_lock = -1;
		rc = txn->cas_remote_content(yield, access->location,access->offset,0,lock,try_lock, cor_id);//lock
		if(rc == Abort || try_lock != 0){
			DEBUG_T("local %ld %ld lock failed other %ld/%ld me %ld\n",_row->get_primary_key(),access->offset, try_lock, _row->_tid_word, lock);
			rc = Abort;
			return rc;
		}
		DEBUG_T("local %ld %ld lock me %ld\n",_row->get_primary_key(),access->offset,  lock);
        //local read;
        row_t *temp_row = (row_t *)mem_allocator.alloc(row_t::get_row_size(ROW_DEFAULT_SIZE));
        memcpy(temp_row, _row, row_t::get_row_size(ROW_DEFAULT_SIZE));

		uint64_t version = (temp_row->version_num)%HIS_CHAIN_NUM;
		if((temp_row->txn_id[version] != 0 && temp_row->txn_id[version] != txn->get_txn_id() + 1)||(txn->get_timestamp() <= temp_row->rts[version])){
			//local unlock and Abort
			_row->_tid_word = 0;
			mem_allocator.free(temp_row,row_t::get_row_size(ROW_DEFAULT_SIZE));
			DEBUG_T("local %ld write by other other %ld (write op)\n", temp_row->get_primary_key(), temp_row->txn_id[version]);
			rc = Abort;
			return rc;
		}

		_row->txn_id[version] = txn->get_txn_id() + 1;
		_row->rts[version] = txn->get_timestamp();
		//temp_row->version_num = temp_row->version_num + 1;
		_row->_tid_word = 0;//release lock

        temp_row->txn_id[version] = txn->get_txn_id() + 1;
		temp_row->rts[version] = txn->get_timestamp();
		temp_row->_tid_word = 0;
		DEBUG_T("txn %ld local %ld %ld write %ld release latch %ld\n",lock, temp_row->get_primary_key(), (char*)_row - rdma_global_buffer, temp_row->txn_id[version], _row->_tid_word);
		txn->cur_row->copy(temp_row);
		mem_allocator.free(temp_row,row_t::get_row_size(ROW_DEFAULT_SIZE));
		rc = RCOK;
		return rc;
    }
	rc = RCOK;
	return rc;
}

void Row_rdma_mvcc::local_write_back(TxnManager * txnMng , int num){
    Transaction *txn = txnMng->txn;
    int i,version_change;
    row_t * row = txn->accesses[ txnMng->write_set[num] ]->orig_row;
    if(row->version_num < HIS_CHAIN_NUM -1 ){
        version_change = row->version_num ;//=temp_row->version_num+1-1
    }
    else{
        version_change =(row->version_num) % HIS_CHAIN_NUM;
    }

    row->txn_id[version_change] = txnMng->get_txn_id();
    row->rts[version_change] = txnMng->txn->timestamp;
    int last_ver;
    if(version_change == 0){
        last_ver = HIS_CHAIN_NUM - 1;
    }
    else{
        last_ver = version_change - 1;
    } 
    row->txn_id[last_ver] = 0;
}

#endif