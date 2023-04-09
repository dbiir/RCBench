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

#include "global.h"
#include "table.h"
#include "catalog.h"
#include "row.h"
#include "txn.h"

#include "catalog.h"
#include "dli.h"
#include "global.h"
#include "mem_alloc.h"
#include "row_dli_based.h"
#include "row_dta.h"
#include "row_lock.h"
#include "row_maat.h"
#include "row_wkdb.h"
#include "row_mvcc.h"
#include "row_occ.h"
#include "row_si.h"
#include "row_ts.h"
#include "row_tictoc.h"
#include "row_ssi.h"
#include "row_wsi.h"
#include "row_cicada.h"
#include "row_null.h"
#include "row_silo.h"
#include "row_rdma_silo.h"
#include "row_rdma_mvcc.h"
#include "row_rdma_2pl.h"
#include "row_rdma_ts1.h"
#include "row_rdma_mocc.h"
#include "row_rdma_dslr_no_wait.h"
#include "rdma_ts1.h"
#include "row_rdma_ts.h"
#include "rdma_ts.h"
#include "mem_alloc.h"
#include "manager.h"
#include "wl.h"

#define SIM_FULL_ROW true

int row_t::get_row_size(int tuple_size){
#if RDMA_ONE_SIDE == true
	// int tuple_size = schema->get_tuple_size();
	uint64_t write_size = (uint64_t)tuple_size < ROW_DEFAULT_SIZE ? tuple_size : ROW_DEFAULT_SIZE;
	int size = sizeof(row_t) + write_size;
#else
	int size = sizeof(row_t);
#endif
	return size;
} 

RC row_t::init(table_t *host_table, uint64_t part_id, uint64_t row_id) {
	part_info = true;
	_row_id = row_id;
	_part_id = part_id;
	this->table = host_table;
    table_idx = host_table->table_id;

	Catalog * schema = host_table->get_schema();
	tuple_size = schema->get_tuple_size();
    
	memset(table_name, 0, 15);
	memcpy(table_name, host_table->get_table_name(), strlen(host_table->get_table_name()));
// #ifndef USE_RDMA// == false
#if RDMA_ONE_SIDE == false
	#if SIM_FULL_ROW
		data = (char *) mem_allocator.alloc(sizeof(char) * tuple_size);
	#else
		data = (char *) mem_allocator.alloc(sizeof(uint64_t) * 1);
	#endif
#endif
#if CC_ALG == RDMA_SILO
	_tid_word = 0;
	timestamp = 0;
#endif
#if CC_ALG == RDMA_MOCC
	_tid_word = 0;
  	timestamp = 0;
	lock_type = 0;
	is_hot = 0;
    for(int i = 0; i < LOCK_LENGTH; i++) {
		ts[i] = 0;
		lock_owner[i] = 0;
	}
#endif
#if CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2
	_tid_word = 0;
#endif
#if CC_ALG == RDMA_DSLR_NO_WAIT 
    _tid_word = 0;
	_reset_from = 0;
#endif
#if CC_ALG == RDMA_WOUND_WAIT2
	_tid_word = 0;
	lock_owner = 0;
#endif
#if CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT
    lock_type = 0;
    for(int i = 0; i < LOCK_LENGTH; i++) {
		ts[i] = 0;
		lock_owner[i] = 0;
	}
#endif
#if CC_ALG == RDMA_MVCC
   _tid_word = 0;
   version_num = 0;
   for (int i = 0;i < HIS_CHAIN_NUM;i++){
       rts[i] = 0;
       start_ts[i] = 0;
       end_ts[i] = UINT64_MAX;
       txn_id[i] = 0;
   }
#endif
#if CC_ALG ==RDMA_TS1
	mutx = 0;
	for(int i = 0; i < CASCADING_LENGTH; i++) {
		tid[i] = 0;
	}
	wts = 0;
	rts = 0;
#endif
#if CC_ALG ==RDMA_TS
	mutx = 0;
	wts = 0;
	rts = 0;
	memset(ur, 0, sizeof(ur));
	memset(up, 0, sizeof(ur));
#endif
#if CC_ALG == RDMA_MAAT

	_tid_word = 0;
	timestamp_last_read = 0;
	timestamp_last_write = 0;
	for (int i = 0; i < row_set_length; i++) {
		uncommitted_writes[i] = 0;
		uncommitted_reads[i] = 0;
	}
    ucreads_len = 0;
    ucwrites_len = 0;
#endif
#if CC_ALG == RDMA_CICADA
	for (int i = 0; i < HIS_CHAIN_NUM; i++) {
		cicada_version[i].init(i, 0, 0);
	}
	cicada_version[0].state = Cicada_COMMITTED;
	cicada_version[0].Wts = 1; 
	version_cnt = 0;
	_tid_word = 0;
#endif
	return RCOK;
}

RC row_t::switch_schema(table_t *host_table) {
	this->table = host_table;
	return RCOK;
}

void row_t::init_manager(row_t * row) {
#if MODE==NOCC_MODE || MODE==QRY_ONLY_MODE
	return;
#endif
	DEBUG_M("row_t::init_manager alloc \n");
#if CC_ALG == NO_WAIT || CC_ALG == WAIT_DIE || CC_ALG == CALVIN || CC_ALG == RDMA_CALVIN || CC_ALG == WOUND_WAIT
	manager = (Row_lock *) mem_allocator.align_alloc(sizeof(Row_lock));
#elif CC_ALG == TIMESTAMP
	manager = (Row_ts *) mem_allocator.align_alloc(sizeof(Row_ts));
#elif CC_ALG == MVCC
	manager = (Row_mvcc *) mem_allocator.align_alloc(sizeof(Row_mvcc));
#elif CC_ALG == OCC || CC_ALG == BOCC || CC_ALG == FOCC
	manager = (Row_occ *) mem_allocator.align_alloc(sizeof(Row_occ));
#elif CC_ALG == DLI_BASE || CC_ALG == DLI_OCC
	manager = (Row_dli_base *)mem_allocator.align_alloc(sizeof(Row_dli_base));
#elif CC_ALG == DLI_MVCC_OCC || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == DLI_MVCC
	manager = (Row_si *)mem_allocator.align_alloc(sizeof(Row_si));
#elif CC_ALG == MAAT
	manager = (Row_maat *) mem_allocator.align_alloc(sizeof(Row_maat));
#elif CC_ALG == DTA
	manager = (Row_dta *)mem_allocator.align_alloc(sizeof(Row_dta));
#elif CC_ALG == WOOKONG
	manager = (Row_wkdb *) mem_allocator.align_alloc(sizeof(Row_wkdb));
#elif CC_ALG == TICTOC
	manager = new Row_tictoc(this);
#elif CC_ALG == SSI
	manager = (Row_ssi *) mem_allocator.align_alloc(sizeof(Row_ssi));
#elif CC_ALG == WSI
	manager = (Row_wsi *) mem_allocator.align_alloc(sizeof(Row_wsi));
#elif CC_ALG == CNULL
	manager = (Row_null *) mem_allocator.align_alloc(sizeof(Row_null));
#elif CC_ALG == SILO
  manager = (Row_silo *) mem_allocator.align_alloc(sizeof(Row_silo));
#elif CC_ALG == RDMA_SILO
  manager = (Row_rdma_silo *) mem_allocator.align_alloc(sizeof(Row_rdma_silo));
#elif CC_ALG == RDMA_MOCC
  manager = (Row_rdma_mocc *) mem_allocator.align_alloc(sizeof(Row_rdma_mocc));
#elif CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2 || CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT
  manager = (Row_rdma_2pl *) mem_allocator.align_alloc(sizeof(Row_rdma_2pl));
#elif CC_ALG == RDMA_DSLR_NO_WAIT
  manager = (Row_rdma_dslr_no_wait *)mem_allocator.align_alloc(sizeof(Row_rdma_dslr_no_wait));
#elif CC_ALG == RDMA_MVCC
  manager = (Row_rdma_mvcc *) mem_allocator.align_alloc(sizeof(Row_rdma_mvcc));

#elif CC_ALG == RDMA_MAAT
  manager = (Row_rdma_maat *) mem_allocator.align_alloc(sizeof(Row_rdma_maat));
#elif CC_ALG ==RDMA_TS1
  manager = (Row_rdma_ts1 *) mem_allocator.align_alloc(sizeof(Row_rdma_ts1));
#elif CC_ALG ==RDMA_TS
  manager = (Row_rdma_ts *) mem_allocator.align_alloc(sizeof(Row_rdma_ts));
#elif CC_ALG == RDMA_CICADA
  manager = (Row_rdma_cicada *) mem_allocator.align_alloc(sizeof(Row_rdma_cicada));
#elif CC_ALG == CICADA
	manager = (Row_cicada *) mem_allocator.align_alloc(sizeof(Row_cicada));
#endif

#if CC_ALG != HSTORE && CC_ALG != HSTORE_SPEC && CC_ALG != RDMA_CNULL
	manager->init(this);
#endif
}



#if RDMA_ONE_SIDE == true
table_t *row_t::get_table() { 
	// std::string tbl_name(table_name);

	// table_t * tbl = m_wl->get_table(tbl_name);
	table_t * tbl = m_wl->get_table(table_idx);
	return tbl;
}
// Catalog *row_t::get_schema() { return &table->schema; }
#else
table_t *row_t::get_table() { return table; }
#endif
Catalog *row_t::get_schema() { return get_table()->get_schema(); }

const char *row_t::get_table_name() { return get_table()->get_table_name(); };
uint64_t row_t::get_tuple_size() { return tuple_size; }

uint64_t row_t::get_field_cnt() { return get_schema()->field_cnt; }

void row_t::set_value(int id, void * ptr) {
	// table* table = get_table();
	int datasize = get_schema()->get_field_size(id);
	int pos = get_schema()->get_field_index(id);
	DEBUG("set_value pos %d datasize %d -- %lx\n", pos, datasize, (uint64_t)this);
#if SIM_FULL_ROW
	memcpy( &data[pos], ptr, datasize);
#else
	char d[tuple_size];
	memcpy( &d[pos], ptr, datasize);
#endif
}

void row_t::set_value(int id, void * ptr, int size) {
	int pos = get_schema()->get_field_index(id);
#if SIM_FULL_ROW
	memcpy( &data[pos], ptr, size);
#else
	char d[tuple_size];
	memcpy( &d[pos], ptr, size);
#endif
}

void row_t::set_value(const char * col_name, void * ptr) {
	uint64_t id = get_schema()->get_field_id(col_name);
	set_value(id, ptr);
}

SET_VALUE(uint64_t);
SET_VALUE(int64_t);
SET_VALUE(double);
SET_VALUE(UInt32);
SET_VALUE(SInt32);

GET_VALUE(uint64_t);
// GET_VALUE(int64_t);
void row_t::get_value(int col_id, int64_t & value) {
	int pos = get_schema()->get_field_index(col_id);
	DEBUG("get_value pos %d -- %lx\n",pos,(uint64_t)this); 
	assert(pos <= tuple_size);
	value = *(int64_t *)&data[pos];
}
GET_VALUE(double);
GET_VALUE(UInt32);
GET_VALUE(SInt32);

char * row_t::get_value(int id) {
	int pos __attribute__ ((unused));
	pos = get_schema()->get_field_index(id);
	DEBUG("get_value pos %d -- %lx\n",pos,(uint64_t)this);
#if SIM_FULL_ROW
	return &data[pos];
#else
	return data;
#endif
}

char * row_t::get_value(char * col_name) {
	uint64_t pos __attribute__ ((unused));
	pos = get_schema()->get_field_index(col_name);
#if SIM_FULL_ROW
	return &data[pos];
#else
	return data;
#endif
}

char *row_t::get_data() { return data; }

void row_t::set_data(char * data) {
	int ts = tuple_size;
#if SIM_FULL_ROW
	memcpy(this->data, data, ts);
#else
	char d[ts];
	memcpy(d, data, ts);
#endif
}
// copy from the src to this
void row_t::copy(row_t * src) {
    assert(src!=NULL);
    //printf("src->table_name = %s ; this->table_name = %s\n",src->table_name,this->table_name);
	assert(src->get_schema() == this->get_schema());
#if CC_ALG ==RDMA_TS1
	set_primary_key(src->get_primary_key());
#endif
#if SIM_FULL_ROW
	set_data(src->get_data());
#else
	char d[tuple_size];
	set_data(d);
#endif
}

void row_t::free_row() {
	DEBUG_M("row_t::free_row free\n");

// #ifndef USE_RDMA // == false
#if RDMA_ONE_SIDE == false

#if SIM_FULL_ROW
	mem_allocator.free(data, sizeof(char) * get_tuple_size());
#else
	mem_allocator.free(data, sizeof(uint64_t) * 1);
#endif

#endif

}

RC row_t::get_lock(access_t type, TxnManager * txn) {
	RC rc = RCOK;
#if CC_ALG == CALVIN || CC_ALG == RDMA_CALVIN
	lock_t lt = (type == RD || type == SCAN)? DLOCK_SH : DLOCK_EX;
	rc = this->manager->lock_get(lt, txn);
#endif
	return rc;
}


RC row_t::remote_copy_row(row_t* remote_row, TxnManager * txn, Access *access) {
  RC rc = RCOK;
  uint64_t init_time = get_sys_clock();
  uint64_t size = (uint64_t)remote_row->tuple_size < RDMA_ACCESS_ROW_SIZE ? row_t::get_row_size(remote_row->tuple_size) : row_t::get_row_size(RDMA_ACCESS_ROW_SIZE);
  txn->cur_row = (row_t *) mem_allocator.alloc(size);
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
  
  uint64_t copy_time = get_sys_clock();
  memcpy((char*)txn->cur_row, (char*)remote_row, size);
  access->data = txn->cur_row;
//   printf("remote_copy_row.cpp:286】table_name = %s operate_size = %ld tuple_size = %ld sizeof(row_t)=%d\n",txn->cur_row->table_name,row_t::get_row_size(remote_row->tuple_size),txn->cur_row->tuple_size,sizeof(row_t));
  //access->orig_row = txn->cur_row;

  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
  return rc;
}

RC row_t::get_row(yield_func_t &yield,access_t type, TxnManager *txn, Access *access,uint64_t cor_id) {
  RC rc = RCOK;
#if MODE==NOCC_MODE || MODE==QRY_ONLY_MODE
	access->data = this;
		return rc;
#endif
#if ISOLATION_LEVEL == NOLOCK
	access->data = this;
		return rc;
#endif

#if CC_ALG == CNULL
  	uint64_t init_time = get_sys_clock();
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

	rc = this->manager->access(type,txn);

  	uint64_t copy_time = get_sys_clock();
	// txn->cur_row->copy(this);
	access->data = txn->cur_row;
  	INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#endif
#if CC_ALG == RDMA_CNULL
   txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
   txn->cur_row->init(get_table(), get_part_id());
   access->data = txn->cur_row;
   goto end;
#endif
#if CC_ALG == MAAT || CC_ALG == RDMA_MAAT
  uint64_t init_time = get_sys_clock();
  DEBUG_M("row_t::get_row MAAT alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

  rc = this->manager->access(type,txn);

  uint64_t copy_time = get_sys_clock();
  txn->cur_row->copy(this);
	access->data = txn->cur_row;
	//assert(rc == RCOK);
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#endif
#if CC_ALG == CICADA
	uint64_t init_time = get_sys_clock();
	DEBUG_M("row_t::get_row CICADA alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(sizeof(row_t));
	txn->cur_row->init(get_table(), get_part_id());
	INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
	
	rc = this->manager->access(txn, type, access);
	
	uint64_t copy_time = get_sys_clock();
	if (rc == RCOK ) {
		txn->cur_row->copy(this);
		access->data = txn->cur_row;
	} else if (rc == WAIT) {
		rc = WAIT;
		INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
		goto end;
	} else if (rc == Abort) {
	}
	goto end;
#endif
#if CC_ALG == RDMA_CICADA
  uint64_t init_time = get_sys_clock();
  DEBUG_M("row_t::get_row RDMA_CICADA alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

  rc = this->manager->access(yield, type, txn, txn->cur_row, cor_id);

  uint64_t copy_time = get_sys_clock();
  txn->cur_row->copy(this);
  access->data = txn->cur_row;
	//assert(rc == RCOK);
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#endif

#if CC_ALG == TICTOC
		DEBUG_M("row_t::get_row TICTOC alloc \n");
	// txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(RDMA_ACCESS_ROW_SIZE));
	// txn->cur_row->init(get_table(), get_part_id());
		// rc = this->manager->access(type,txn);
		// txn->cur_row->copy(this);
	// access->data = txn->cur_row;
		// assert(rc == RCOK);
	goto end;
#endif
#if CC_ALG == WOOKONG
  uint64_t init_time = get_sys_clock();
  uint64_t copy_time = get_sys_clock();
	if ((type == WR && rc == RCOK) || type == RD || type == SCAN) {
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
		if (type == WR)
			rc = this->manager->access(P_REQ, txn, NULL);
		else
			rc = this->manager->access(R_REQ, txn, NULL);

    copy_time = get_sys_clock();
		if (rc == RCOK ) {
			access->data = txn->cur_row;
		} else if (rc == WAIT) {
			rc = WAIT;
      INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
			goto end;
		} else if (rc == Abort) {
		}
		if (rc != Abort) {
			assert(access->data->get_data() != NULL);
			assert(access->data->get_table() != NULL);
			assert(access->data->get_schema() == this->get_schema());
			assert(access->data->get_table_name() != NULL);
		}
	}

	if (rc != Abort && type == WR) {
		DEBUG_M("row_t::get_row WKDB alloc \n");
		row_t * newr = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
		newr->init(this->get_table(), this->get_part_id());
		newr->copy(access->data);
		access->data = newr;
	}
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;

#endif
#if CC_ALG == DTA
	if (type == WR) {
		rc = this->manager->access(P_REQ, txn, NULL, access->version);
		if (rc != RCOK) goto end;
	}
	if ((type == WR && rc == RCOK) || type == RD || type == SCAN) {
		rc = this->manager->access(R_REQ, txn, NULL, access->version);
		if (type == WR) access->version = UINT64_MAX;
		if (rc == RCOK ) {
			access->data = txn->cur_row;
		} else if (rc == WAIT) {
					rc = WAIT;
					goto end;

		} else if (rc == Abort) {
		}
				if (rc != Abort) {
			assert(access->data->get_data() != NULL);
			assert(access->data->get_table() != NULL);
			assert(access->data->get_schema() == this->get_schema());
			assert(access->data->get_table_name() != NULL);
				}
	}

	if (rc != Abort && type == WR) {
		DEBUG_M("row_t::get_row DTA alloc \n");
		row_t * newr = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
		newr->init(this->get_table(), get_part_id());
		newr->copy(access->data);
		access->data = newr;
	}
	goto end;

#endif
#if CC_ALG == WAIT_DIE || CC_ALG == NO_WAIT || CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2 || CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == WOUND_WAIT || CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT || CC_ALG == RDMA_DSLR_NO_WAIT 
  	uint64_t init_time = get_sys_clock();
	//uint64_t thd_id = txn->get_thd_id();
	lock_t lt = (type == RD || type == SCAN) ? DLOCK_SH : DLOCK_EX; // ! this wrong !!
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
	#if CC_ALG!=RDMA_NO_WAIT && CC_ALG!=RDMA_NO_WAIT2 && CC_ALG!=RDMA_WAIT_DIE2 && CC_ALG != RDMA_WOUND_WAIT2 && CC_ALG != RDMA_WAIT_DIE && CC_ALG != RDMA_WOUND_WAIT && CC_ALG != RDMA_DSLR_NO_WAIT
		rc = this->manager->lock_get(lt, txn);
	#elif CC_ALG == RDMA_DSLR_NO_WAIT
		rc = this->manager->lock_get(yield,type,txn,this,cor_id);
	#else
		rc = this->manager->lock_get(yield,lt,txn,this,cor_id);
	#endif
  	uint64_t copy_time = get_sys_clock();
	access->data = this;
	if (rc == RCOK) {
	} else if (rc == Abort) {
		// total_num_atomic_retry++;
	} else if (rc == WAIT) {
		ASSERT(CC_ALG == WAIT_DIE || CC_ALG == RDMA_WAIT_DIE2 || CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == WOUND_WAIT || CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT);
	}
  	INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG == TIMESTAMP || CC_ALG == MVCC || CC_ALG == SSI || CC_ALG == WSI
	//uint64_t thd_id = txn->get_thd_id();
// For TIMESTAMP RD, a new copy of the access->data will be returned.

	// for MVCC RD, the version will be returned instead of a copy
	// So for MVCC RD-WR, the version should be explicitly copied.
	// row_t * newr = NULL;*/
  uint64_t init_time = get_sys_clock();
#if CC_ALG == TIMESTAMP
	DEBUG_M("row_t::get_row TIMESTAMP alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), this->get_part_id());
	assert(txn->cur_row->get_schema() == this->get_schema());
#endif
	INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
	uint64_t copy_time = get_sys_clock();
	// row_t * row;

	if (type == WR) {
		rc = this->manager->access(txn, P_REQ, NULL);
		if (rc != RCOK) goto end;
	}
	if ((type == WR && rc == RCOK) || type == RD || type == SCAN) {
		rc = this->manager->access(txn, R_REQ, NULL);

    	copy_time = get_sys_clock();

		if (rc == RCOK ) {
			access->data = txn->cur_row;
		} else if (rc == WAIT) {
			rc = WAIT;
			INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
			goto end;
		} else if (rc == Abort) {
		}
		if (rc != Abort) {
			assert(access->data->get_data() != NULL);
			assert(access->data->get_table() != NULL);
			assert(access->data->get_schema() == this->get_schema());
			assert(access->data->get_table_name() != NULL);
		}
	}
	if (rc != Abort && (CC_ALG == MVCC || CC_ALG == SSI || CC_ALG == WSI) && type == WR) {
			DEBUG_M("row_t::get_row MVCC alloc \n");
		row_t * newr = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
		newr->init(this->get_table(), get_part_id());
		newr->copy(access->data);
		access->data = newr;
	}
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG ==RDMA_TS1 || CC_ALG == RDMA_TS
	uint64_t init_time = get_sys_clock();
	DEBUG_M("row_t::get_row RDMA_TS1 alloc \n");
	// txn->cur_row = (row_t *) mem_allocator.alloc(sizeof(row_t));
    txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), this->get_part_id());
	assert(txn->cur_row->get_schema() == this->get_schema());

	INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
	uint64_t copy_time = get_sys_clock();

	rc = this->manager->access(yield, txn, access, type, cor_id);
	if (rc == RCOK) {
		access->data = txn->cur_row;
		assert(access->data->get_data() != NULL);
		assert(access->data->get_table() != NULL);
		assert(access->data->get_schema() == this->get_schema());
		assert(access->data->get_table_name() != NULL);
	}
	goto end;
#elif CC_ALG == OCC || CC_ALG == FOCC || CC_ALG == BOCC
	// OCC always make a local copy regardless of read or write
  uint64_t init_time = get_sys_clock();
	DEBUG_M("row_t::get_row OCC alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

	rc = this->manager->access(txn, R_REQ);

  uint64_t copy_time = get_sys_clock();
	access->data = txn->cur_row;
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG == DLI_BASE || CC_ALG == DLI_OCC
  uint64_t init_time = get_sys_clock();
	// DLI always make a local copy regardless of read or write
	DEBUG_M("row_t::get_row DLI alloc \n");
	txn->cur_row = (row_t *)mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

	rc = this->manager->access(txn, type == WR ? P_REQ : R_REQ, access->version);

  uint64_t copy_time = get_sys_clock();
	access->data = txn->cur_row;
#if CC_ALG == DLI_BASE
	if (rc == RCOK) {
		rc = dli_man.validate(txn, false);
	}
#endif
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG == DLI_MVCC_OCC || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == DLI_MVCC
	rc = this->manager->access(txn, R_REQ, access->version);

  uint64_t copy_time = get_sys_clock();
	if (type == WR) {
		DEBUG_M("row_t::get_row SI alloc \n");
		row_t *newer = (row_t *)mem_allocator.alloc(row_t::get_row_size(tuple_size));
		newer->init(get_table(), get_part_id());
		newer->copy(txn->cur_row);
		txn->cur_row = newer;
	}

	access->data = txn->cur_row;
#if CC_ALG == DLI_MVCC
	rc = dli_man.validate(txn, false);
#endif
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;

#elif CC_ALG == SILO || CC_ALG == RDMA_SILO
	// like OCC, tictoc also makes a local copy for each read/write
 	uint64_t init_time = get_sys_clock();
 	DEBUG_M("row_t::get_row SILO alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
	TsType ts_type = (type == RD)? R_REQ : P_REQ;
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

	rc = this->manager->access(txn, ts_type, txn->cur_row);

  	uint64_t copy_time = get_sys_clock();
  	access->data = txn->cur_row;
  	INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG == RDMA_MOCC
	// like OCC, tictoc also makes a local copy for each read/write
 	uint64_t init_time = get_sys_clock();
 	DEBUG_M("row_t::get_row SILO alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
	TsType ts_type = (type == RD)? R_REQ : P_REQ;
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);

	rc = this->manager->access(yield, txn, ts_type, txn->cur_row, cor_id);

  	uint64_t copy_time = get_sys_clock();
  	access->data = txn->cur_row;
  	INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	goto end;
#elif CC_ALG == RDMA_MVCC
   // rc = this->manager->access(txn, type, txn->cur_row);
   txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
   txn->cur_row->init(get_table(), get_part_id());
   rc = this->manager->access(yield, txn, access, type, cor_id);

   access->data = txn->cur_row;
#elif CC_ALG == HSTORE || CC_ALG == HSTORE_SPEC || CC_ALG == CALVIN || CC_ALG == RDMA_CALVIN
#if CC_ALG == HSTORE_SPEC
	if(txn_table.spec_mode) {
		DEBUG_M("row_t::get_row HSTORE_SPEC alloc \n");
		txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
		txn->cur_row->init(get_table(), get_part_id());
		rc = this->manager->access(txn, R_REQ);
		access->data = txn->cur_row;
		goto end;
	}
#endif
	access->data = this;
	goto end;
#else
	assert(false);
#endif

end:
	return rc;
}



RC row_t::get_ts(uint64_t &orig_wts, uint64_t &orig_rts) {
	RC rc = RCOK;
#if CC_ALG == TICTOC
	this->manager->get_ts(orig_wts, orig_rts);
#endif
	return rc;
}

RC row_t::get_row(access_t type, TxnManager * txn, row_t *& row, uint64_t &orig_wts, uint64_t &orig_rts) {
		RC rc = RCOK;
#if MODE==NOCC_MODE || MODE==QRY_ONLY_MODE
		row = this;
		return rc;
#endif
#if CC_ALG == TICTOC
  uint64_t init_time = get_sys_clock();
  DEBUG_M("row_t::get_row tictoc alloc \n");
	txn->cur_row = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
	txn->cur_row->init(get_table(), get_part_id());
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
  rc = this->manager->access(type,txn,row,orig_wts,orig_rts);
  uint64_t copy_time = get_sys_clock();
  txn->cur_row->copy(this);
	row = txn->cur_row;
  assert(rc == RCOK);
  INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
#endif
	return rc;
}
// Return call for get_row if waiting
RC row_t::get_row_post_wait(access_t type, TxnManager * txn, row_t *& row) {
	RC rc = RCOK;
  uint64_t init_time = get_sys_clock();
	assert(CC_ALG == WAIT_DIE || CC_ALG == MVCC || CC_ALG == WOOKONG || CC_ALG == TIMESTAMP || CC_ALG == TICTOC || CC_ALG == SSI || CC_ALG == WSI || CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 ||
				 CC_ALG == TIMESTAMP || CC_ALG == WOUND_WAIT);
#if CC_ALG == WAIT_DIE || CC_ALG == WOUND_WAIT
	assert(txn->lock_ready);
	rc = RCOK;
	//ts_t endtime = get_sys_clock();
	row = this;

#elif CC_ALG == MVCC || CC_ALG == TIMESTAMP || CC_ALG == WOOKONG || CC_ALG == TICTOC || CC_ALG == SSI || CC_ALG == WSI || CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3
	assert(txn->ts_ready);
	//INC_STATS(thd_id, time_wait, t2 - t1);
	row = txn->cur_row;

	assert(row->get_data() != NULL);
	assert(row->get_table() != NULL);
	assert(row->get_schema() == this->get_schema());
	assert(row->get_table_name() != NULL);
	if (( CC_ALG == MVCC || CC_ALG == WOOKONG || CC_ALG == TICTOC || CC_ALG == SSI || CC_ALG == WSI) && type == WR) {
		DEBUG_M("row_t::get_row_post_wait MVCC alloc \n");
		row_t * newr = (row_t *) mem_allocator.alloc(row_t::get_row_size(tuple_size));
		newr->init(this->get_table(), get_part_id());
    INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
    uint64_t copy_time = get_sys_clock();
		newr->copy(row);
		row = newr;
    INC_STATS(txn->get_thd_id(), trans_cur_row_copy_time, get_sys_clock() - copy_time);
	}
	if ((CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3) && type == WR) {
		//DEBUG_M("row_t::get_row_post_wait MVCC alloc \n");
		//row_t *newr = (row_t *)mem_allocator.alloc(row_t::get_row_size(ROW_DEFAULT_SIZE));
		//newr->init(this->get_table(), get_part_id());

		//newr->copy(row);
		//row = newr;
	}
#endif
  INC_STATS(txn->get_thd_id(), trans_cur_row_init_time, get_sys_clock() - init_time);
	return rc;
}

// the "row" is the row read out in get_row(). For locking based CC_ALG,
// the "row" is the same as "this". For timestamp based CC_ALG, the
// "row" != "this", and the "row" must be freed.
// For MVCC, the row will simply serve as a version. The version will be
// delete during history cleanup.
// For TIMESTAMP, the row will be explicity deleted at the end of access().
// (c.f. row_ts.cpp)
#if CC_ALG ==RDMA_TS1 || CC_ALG == RDMA_TS
uint64_t row_t::return_row(yield_func_t &yield, access_t type, TxnManager *txn, Access *access, uint64_t cor_id) {
#if MODE==NOCC_MODE || MODE==QRY_ONLY_MODE
	return 0;
#endif
#if ISOLATION_LEVEL == NOLOCK
	return 0;
#endif
	if (type == RD || type == SCAN) {
		access->data->free_row();
		DEBUG_M("row_t::return_row RDMA_TS1 RD free \n");
		// mem_allocator.free(access->data, sizeof(row_t));
        mem_allocator.free(access->data, row_t::get_row_size(access->data->tuple_size));
	}
	else if (type == WR || type == XP) {
#if DEBUG_PRINTF
		// printf("[return_row本地提交]事务号：%d，主键：%d\n",txn->get_txn_id(),this->get_primary_key());
#endif
		RC rc =  this->manager->local_commit(yield, txn, access, type, cor_id);
		assert(rc == RCOK);
	}
	else
		assert(false);
	return 0;
}
#endif
uint64_t row_t::return_row(RC rc, access_t type, TxnManager *txn, row_t *row) {
#if MODE==NOCC_MODE || MODE==QRY_ONLY_MODE
	return 0;
#endif
#if ISOLATION_LEVEL == NOLOCK
	return 0;
#endif

#if CC_ALG == WAIT_DIE || CC_ALG == NO_WAIT || CC_ALG == CALVIN || CC_ALG == RDMA_CALVIN || CC_ALG == WOUND_WAIT
	assert (row == NULL || row == this || type == XP);
	if (CC_ALG != CALVIN && CC_ALG != RDMA_CALVIN && ROLL_BACK &&
			type == XP) {  // recover from previous writes. should not happen w/ Calvin
		this->copy(row);
	}
	this->manager->lock_release(txn);
	return 0;
#elif CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2 || CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT || CC_ALG == RDMA_DSLR_NO_WAIT
	assert (row == NULL || row == this || type == XP || CC_ALG == RDMA_DSLR_NO_WAIT);
	if (CC_ALG != RDMA_DSLR_NO_WAIT && ROLL_BACK && type == XP) {  // recover from previous writes.
		this->copy(row);  //for abort of local txn ABORT, copy orig_data to orig_row. remote ABORT dont need this operate
	}
	return 0;
#elif CC_ALG == TIMESTAMP || CC_ALG == MVCC || CC_ALG == SSI || CC_ALG == WSI
	// for RD or SCAN or XP, the row should be deleted.
	// because all WR should be companied by a RD
	// for MVCC RD, the row is not copied, so no need to free.
	if (CC_ALG == TIMESTAMP && (type == RD || type == SCAN)) {
		row->free_row();
			DEBUG_M("row_t::return_row TIMESTAMP free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	}
	if (type == XP) {
		row->free_row();
			DEBUG_M("row_t::return_row XP free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
		this->manager->access(txn, XP_REQ, NULL);
	} else if (type == WR) {
		assert (type == WR && row != NULL);
		assert (row->get_schema() == this->get_schema());
		RC rc = this->manager->access(txn, W_REQ, row);
		assert(rc == RCOK);
	}
	return 0;
#elif CC_ALG == OCC || CC_ALG == FOCC || CC_ALG == BOCC
	assert (row != NULL);
	if (type == WR) manager->write(row, txn->get_end_timestamp());
	row->free_row();
	DEBUG_M("row_t::return_row OCC free \n");
	mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	manager->release();
	return 0;
#elif CC_ALG == DLI_BASE || CC_ALG == DLI_OCC
	assert (row != NULL);
	uint64_t version = 0;
	version = manager->write(row, txn, type);
	row->free_row();
	DEBUG_M("row_t::return_row DLT1 free \n");
	mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	return version;
#elif CC_ALG == DLI_MVCC_OCC || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == DLI_MVCC
	assert(row != NULL);
	uint64_t version = 0;
	if (type == WR) {
		version = manager->write(row, txn, type);
	} else if (type == XP) {
		manager->write(row, txn, type);
		row->free_row();
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	}
	return version;
#elif CC_ALG == CNULL
	assert (row != NULL);
	if (rc == Abort) {
		manager->abort(type,txn);
	} else {
		manager->commit(type,txn);
	}

		row->free_row();
	DEBUG_M("row_t::return_row Maat free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	return 0;
#elif CC_ALG == RDMA_CNULL
    assert (row != NULL);
    row->free_row();
	DEBUG_M("row_t::return_row Maat free \n");
	mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	return 0;
#elif CC_ALG == MAAT || CC_ALG == RDMA_MAAT || CC_ALG == RDMA_CICADA
	if (row != NULL) {
		row->free_row();
	    DEBUG_M("row_t::return_row Maat free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
    }
	return 0;
#elif CC_ALG == TICTOC
	assert (row != NULL);
	if (rc == Abort) {
		manager->abort(type,txn);
	} else {
		manager->commit(type,txn,row);
	}

	if (type == XP) {
		row->free_row();
			DEBUG_M("row_t::return_row XP free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	}
	return 0;
#elif CC_ALG == WOOKONG
	assert (row != NULL);

	if (rc == Abort) {
		manager->abort(type,txn);
	} else {
		manager->commit(type,txn,row);
	}

	if (type == XP) {
		row->free_row();
		DEBUG_M("row_t::return_row XP free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	}
	return 0;
#elif CC_ALG == DTA
	assert (row != NULL);
	uint64_t version = 0;
	if (rc == Abort) {
		manager->abort(type,txn);
	} else {
		manager->commit(type, txn, row, version);
	}

	if (type == XP) {
		row->free_row();
			DEBUG_M("row_t::return_row XP free \n");
		mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	}
	return version;
#elif CC_ALG == HSTORE || CC_ALG == HSTORE_SPEC
	assert (row != NULL);
	if (ROLL_BACK && type == XP) {// recover from previous writes.
		this->copy(row);
	}
	return 0;
#elif CC_ALG == SILO || CC_ALG == RDMA_SILO || CC_ALG == RDMA_MVCC || CC_ALG == RDMA_MOCC
	assert (row != NULL);
	row->free_row();
  DEBUG_M("row_t::return_row XP free \n");
	mem_allocator.free(row, row_t::get_row_size(ROW_DEFAULT_SIZE));
	return 0;

#else
	assert(false);
#endif
}
