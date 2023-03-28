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
#include "helper.h"
#include "rdma.h"
#include "rdma_mvcc.h"
#include "row_rdma_mvcc.h"
#include "mem_alloc.h"
#include "lib.hh"
#include "qps/op.hh"
#if CC_ALG == RDMA_MVCC
void rdma_mvcc::init(row_t * row) {

}

void  rdma_mvcc::local_write_back(TxnManager * txnMng , uint64_t num){
    row_t *temp_row = txnMng->txn->accesses[num]->orig_row;

     // 2.get all versions of the data item, find the oldest version, and replace it with new data to write
    int version_change;
    int last_ver = (temp_row->version_num) % HIS_CHAIN_NUM;//latest version
    if(last_ver == HIS_CHAIN_NUM -1 )version_change = 0;
    else version_change = last_ver + 1 ;//the version to be replaced

    //the version to be unlocked by txn_id
    int release_ver = txnMng->txn->accesses[num]->old_version_num % HIS_CHAIN_NUM;
    // printf("local %ld write commit %ld\n",temp_row->get_primary_key(),temp_row->txn_id[version_change]);
    // 3.writes the new version, RTS, start_ts, end_ts, txn-id, etc. by ONE SIDE WRITE, and unlocks the previous version's txn-id by setting it to 0
    temp_row->version_num = temp_row->version_num +1;

    temp_row->txn_id[version_change] = 0;// txnMng->get_txn_id();
    temp_row->rts[version_change] = txnMng->txn->timestamp;
    temp_row->start_ts[version_change] = txnMng->txn->timestamp;
    temp_row->end_ts[version_change] = UINT64_MAX;
    
    temp_row->txn_id[release_ver] = 0;
    temp_row->end_ts[release_ver] = txnMng->txn->timestamp;
    
    temp_row->_tid_word = 0;//release lock
}

void  rdma_mvcc::remote_write_back(yield_func_t &yield,TxnManager * txnMng , uint64_t num , row_t * remote_row, uint64_t cor_id){
    uint64_t off = txnMng->txn->accesses[num]->offset;
 	uint64_t loc = txnMng->txn->accesses[num]->location;
#if USE_DBPAOR
    row_t *temp_row = remote_row;
#else
    row_t *temp_row;
    RC rc = txnMng->read_remote_row(yield, loc, off, &temp_row,cor_id);
    if (rc == Abort) return;
#endif
	row_t * row = txnMng->txn->accesses[num]->orig_row;
    if(temp_row->get_primary_key() != row->get_primary_key()) return;
    // else if (temp_row->get_primary_key() != row->get_primary_key() ) assert(false);

    // 2.get all versions of the data item, find the oldest version, and replace it with new data to write
    int version_change;
    int last_ver = (temp_row->version_num) % HIS_CHAIN_NUM;//latest version
    if(last_ver == HIS_CHAIN_NUM -1 )version_change = 0;
    else version_change = last_ver + 1 ;//the version to be replaced

    //the version to be unlocked by txn_id
    int release_ver = txnMng->txn->accesses[num]->old_version_num % HIS_CHAIN_NUM;
    // printf("remote %ld write commit %ld\n",temp_row->get_primary_key(),temp_row->txn_id[version_change]);
    // 3.writes the new version, RTS, start_ts, end_ts, txn-id, etc. by ONE SIDE WRITE, and unlocks the previous version's txn-id by setting it to 0
    temp_row->version_num = temp_row->version_num +1;

    temp_row->txn_id[version_change] = 0;// txnMng->get_txn_id();
    temp_row->rts[version_change] = txnMng->txn->timestamp;
    temp_row->start_ts[version_change] = txnMng->txn->timestamp;
    temp_row->end_ts[version_change] = UINT64_MAX;
    
    temp_row->txn_id[release_ver] = 0;
    temp_row->end_ts[release_ver] = txnMng->txn->timestamp;

    temp_row->_tid_word = 0;//release lock
    
    //write back
    Transaction *txn = txnMng->txn;
	uint64_t thd_id = txnMng->get_thd_id();
	uint64_t lock = txnMng->get_txn_id() + 1;
    uint64_t operate_size = row_t::get_row_size(temp_row->tuple_size);

    // char *test_buf = Rdma::get_row_client_memory(thd_id);
    // memcpy(test_buf, (char*)temp_row , operate_size);
    txnMng->write_remote_row(yield, loc, operate_size, off, (char*)temp_row, cor_id);

    mem_allocator.free(temp_row, row_t::get_row_size(ROW_DEFAULT_SIZE));
}

void rdma_mvcc::abort_release_local_lock(TxnManager * txnMng , uint64_t num){
    Transaction *txn = txnMng->txn;
    row_t * temp_row = txn->accesses[num]->orig_row;
    int version = txn->accesses[num]->old_version_num % HIS_CHAIN_NUM ;//version be locked
    //int version = temp_row->version_num % HIS_CHAIN_NUM;
    // printf("local %ld write abort %ld\n",temp_row->get_primary_key(),temp_row->txn_id[version]);
    temp_row->txn_id[version] = 0;
        
}

void rdma_mvcc::abort_release_remote_lock(yield_func_t &yield, TxnManager * txnMng , uint64_t num, uint64_t cor_id){
    Transaction *txn = txnMng->txn;
    row_t * temp_row = txn->accesses[num]->orig_row;
    int version = txn->accesses[num]->old_version_num % HIS_CHAIN_NUM ;//version be locked
    //int version = temp_row->version_num % HIS_CHAIN_NUM;
    // printf("remote %ld write abort %ld\n",temp_row->get_primary_key(),temp_row->txn_id[version]);
    temp_row->txn_id[version] = 0;

    uint64_t off = txn->accesses[num]->offset;
    uint64_t loc = txn->accesses[num]->location;
    uint64_t thd_id = txnMng->get_thd_id();
    uint64_t lock = txnMng->get_txn_id() + 1;
    uint64_t operate_size = row_t::get_row_size(temp_row->tuple_size);

    // char *test_buf = Rdma::get_row_client_memory(thd_id);
    // memcpy(test_buf, (char*)temp_row , operate_size);
    txnMng->write_remote_row(yield,loc,operate_size,off,(char*)temp_row,cor_id);
}

RC rdma_mvcc::finish(yield_func_t &yield, RC rc,TxnManager * txnMng, uint64_t cor_id){
    Transaction *txn = txnMng->txn;

    int wr = 0; 
  	//int read_set[10];
	int read_set[txn->row_cnt - txn->write_cnt];
	int rd = 0;
	for (uint64_t rid = 0; rid < txn->row_cnt; rid ++) {
		if (txn->accesses[rid]->type == WR)
			txnMng->write_set[wr ++] = rid;
		else
			read_set[rd ++] = rid;
	}

    uint64_t wr_cnt = txn->write_cnt;

    vector<vector<uint64_t>> remote_access(g_node_cnt);
    if(rc == Abort){
        for (uint64_t i = 0; i < wr_cnt; i++) {
           uint64_t num = txnMng->write_set[i];
           uint64_t remote_offset = txn->accesses[num]->offset;
           uint64_t loc = txn->accesses[num]->location;
	       uint64_t lock_num = txnMng->get_txn_id() + 1;
        #if USE_DBPAOR == true
           if(loc != g_node_id) remote_access[loc].push_back(num);
           else{ //local
               abort_release_local_lock(txnMng,num);
           }
        #else
           //lock in loop
            // assert(txnMng->loop_cas_remote(yield,loc,remote_offset,0,lock_num,cor_id) == true);

           //release lock
           if(txn->accesses[num]->location == g_node_id){
               abort_release_local_lock(txnMng,num);
           }else{
               abort_release_remote_lock(yield,txnMng,num,cor_id);
           }
        //    uint64_t release_lock = txnMng->cas_remote_content(yield,loc,remote_offset,lock_num,0,cor_id);
        //    assert(release_lock == lock_num);
        #endif
        }
        #if USE_DBPAOR == true
        uint64_t starttime = get_sys_clock(), endtime;
        for(int i=0;i<g_node_cnt;i++){ //for the same node, batch unlock remote
            if(remote_access[i].size() > 0){
                txnMng->batch_unlock_remote(yield, cor_id, i, Abort, txnMng, remote_access);
            }
        }
        for(int i=0;i<g_node_cnt;i++){ //poll result
            if(remote_access[i].size() > 0){
            	//to do: add coroutine
                INC_STATS(txnMng->get_thd_id(), worker_oneside_cnt, 1);
            #if USE_COROUTINE
                uint64_t waitcomp_time;
                std::pair<int,ibv_wc> dbres1;
                INC_STATS(txnMng->get_thd_id(), worker_process_time, get_sys_clock() - txnMng->h_thd->cor_process_starttime[cor_id]);
                
                do {
                    txnMng->h_thd->start_wait_time = get_sys_clock();
                    txnMng->h_thd->last_yield_time = get_sys_clock();
                    // printf("do\n");
                    yield(txnMng->h_thd->_routines[((cor_id) % COROUTINE_CNT) + 1]);
                    uint64_t yield_endtime = get_sys_clock();
                    INC_STATS(txnMng->get_thd_id(), worker_yield_cnt, 1);
                    INC_STATS(txnMng->get_thd_id(), worker_yield_time, yield_endtime - txnMng->h_thd->last_yield_time);
                    INC_STATS(txnMng->get_thd_id(), worker_idle_time, yield_endtime - txnMng->h_thd->last_yield_time);
                    dbres1 = rc_qp[i][txnMng->get_thd_id() + cor_id * g_thread_cnt]->poll_send_comp();
                    waitcomp_time = get_sys_clock();
                    
                    INC_STATS(txnMng->get_thd_id(), worker_idle_time, waitcomp_time - yield_endtime);
                    INC_STATS(txnMng->get_thd_id(), worker_waitcomp_time, waitcomp_time - yield_endtime);
                } while (dbres1.first == 0);
                txnMng->h_thd->cor_process_starttime[cor_id] = get_sys_clock();
                // RDMA_ASSERT(res_p == rdmaio::IOCode::Ok);
            #else
                starttime = get_sys_clock();
                auto dbres1 = rc_qp[i][txnMng->get_thd_id() + cor_id * g_thread_cnt]->wait_one_comp();
                RDMA_ASSERT(dbres1 == IOCode::Ok);
                endtime = get_sys_clock();
                INC_STATS(txnMng->get_thd_id(), worker_waitcomp_time, endtime-starttime);
                INC_STATS(txnMng->get_thd_id(), worker_idle_time, endtime-starttime);
                DEL_STATS(txnMng->get_thd_id(), worker_process_time, endtime-starttime);
            #endif 
            }
        }
        #endif
    }
    else{ //COMMIT
       for (uint64_t i = 0; i < wr_cnt; i++) {
           uint64_t num = txnMng->write_set[i];
           uint64_t remote_offset = txn->accesses[num]->offset;
           uint64_t loc = txn->accesses[num]->location;
	       uint64_t lock_num = txnMng->get_txn_id() + 1;
           row_t* remote_row = NULL;
        #if USE_DBPAOR == true
            if(loc !=  g_node_id){ //remote
                uint64_t try_lock;
                remote_row = txnMng->cas_and_read_remote(yield, try_lock,loc,remote_offset,remote_offset,0,lock_num, cor_id);
                while(try_lock!=0){ //lock fail
                    mem_allocator.free(remote_row, row_t::get_row_size(ROW_DEFAULT_SIZE));
                    remote_row = txnMng->cas_and_read_remote(yield, try_lock,loc,remote_offset,remote_offset,0,lock_num, cor_id);
                }
            }
            else{ //local
                assert(txnMng->loop_cas_remote(loc,remote_offset,0,lock_num) == RCOK);
            }
        #else
           //lock in loop
           assert(txnMng->loop_cas_remote(yield,loc,remote_offset,0,lock_num,cor_id) == RCOK);
        #endif

           if(txn->accesses[num]->location == g_node_id){//local
                local_write_back(txnMng, num);
           }
           else{
                remote_write_back(yield, txnMng, num, remote_row, cor_id);
                // uint64_t release_lock = txnMng->cas_remote_content(loc,remote_offset,lock_num,0);
           }  
            // remote_release_lock(txnMng,txnMng->write_set[i]);//release lock
            //uint64_t release_lock = txnMng->cas_remote_content(loc,remote_offset,lock_num,0);
            //assert(release_lock == lock_num);
       }
    }



    for (uint64_t i = 0; i < txn->row_cnt; i++) {
        if(txn->accesses[i]->location != g_node_id){
        //remote
        mem_allocator.free(txn->accesses[i]->orig_row,0);
        txn->accesses[i]->orig_row = NULL;
        }
    }
    
    return rc;
}

#endif
