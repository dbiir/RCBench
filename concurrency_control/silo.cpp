#include "txn.h"
#include "row.h"
#include "row_silo.h"

#if CC_ALG == SILO

RC
TxnManager::validate_silo()
{
	RC rc = RCOK;
	// lock write tuples in the primary key order.
	uint64_t wr_cnt = txn->write_cnt;
	// write_set = (int *) mem_allocator.alloc(sizeof(int) * wr_cnt);
	int cur_wr_idx = 0;
	int read_set[txn->row_cnt - txn->write_cnt];
	int cur_rd_idx = 0;
	for (uint64_t rid = 0; rid < txn->row_cnt; rid ++) {
		if (txn->accesses[rid]->type == WR)
			write_set[cur_wr_idx ++] = rid;
		else 
			read_set[cur_rd_idx ++] = rid;
	}

	// bubble sort the write_set, in primary key order 
	if (wr_cnt > 1)
	{
		for (uint64_t i = wr_cnt - 1; i >= 1; i--) {
			for (uint64_t j = 0; j < i; j++) {
				if (txn->accesses[ write_set[j] ]->orig_row->get_primary_key() > 
					txn->accesses[ write_set[j + 1] ]->orig_row->get_primary_key())
				{
					int tmp = write_set[j];
					write_set[j] = write_set[j+1];
					write_set[j+1] = tmp;
				}
			}
		}
	}

	num_locks = 0;
	ts_t max_tid = 0;
	bool done = false;
	if (_pre_abort) {
		for (uint64_t i = 0; i < wr_cnt; i++) {
			row_t * row = txn->accesses[ write_set[i] ]->orig_row;
			if (row->manager->get_tid() != txn->accesses[write_set[i]]->tid) {
	            INC_STATS(get_thd_id(),silo_lock_write_abort,get_sys_clock() );
				rc = Abort;
				return rc;
			}	
		}	
		for (uint64_t i = 0; i < txn->row_cnt - wr_cnt; i ++) {
			Access * access = txn->accesses[ read_set[i] ];
			if (access->orig_row->manager->get_tid() != txn->accesses[read_set[i]]->tid) {
	            INC_STATS(get_thd_id(),silo_lock_read_abort,get_sys_clock());
				rc = Abort;
				return rc;
			}
		}
	}

	// lock all rows in the write set.
	if (_validation_no_wait) {
		while (!done) {
			num_locks = 0;
			for (uint64_t i = 0; i < wr_cnt; i++) {
				row_t * row = txn->accesses[ write_set[i] ]->orig_row;
				if (!row->manager->try_lock())
				{
					// rc = Abort;
					// return rc;
					break;
				}
				DEBUG("silo %ld write lock row %ld \n", this->get_txn_id(), row->get_primary_key());
				row->manager->assert_lock();
				num_locks ++;
				if (row->manager->get_tid() != txn->accesses[write_set[i]]->tid)
				{
					rc = Abort;
					return rc;
				}
			}
			if (num_locks == wr_cnt) {
				DEBUG("TRY LOCK true %ld\n", get_txn_id());
				done = true;
			} else {
                INC_STATS(get_thd_id(), cnt_un_abort, 1);
				rc = Abort;
				return rc;

				// for (uint64_t i = 0; i < num_locks; i++)
				// {
				// 	txn->accesses[ write_set[i] ]->orig_row->manager->release();
				// }
				// if (_pre_abort) {
				// 	num_locks = 0;
				// 	for (uint64_t i = 0; i < wr_cnt; i++) {
				// 		row_t * row = txn->accesses[ write_set[i] ]->orig_row;
				// 		if (row->manager->get_tid() != txn->accesses[write_set[i]]->tid) {
				// 			rc = Abort;
				// 			return rc;
				// 		}	
				// 	}	
				// 	for (uint64_t i = 0; i < txn->row_cnt - wr_cnt; i ++) {
				// 		Access * access = txn->accesses[ read_set[i] ];
				// 		if (access->orig_row->manager->get_tid() != txn->accesses[read_set[i]]->tid) {
				// 			rc = Abort;
				// 			return rc;
				// 		}
				// 	}
				// }
                // PAUSE_SILO
			}
		}
	} else {
		for (uint64_t i = 0; i < wr_cnt; i++) {
			row_t * row = txn->accesses[ write_set[i] ]->orig_row;
			row->manager->lock();
			DEBUG("silo %ld write lock row %ld \n", this->get_txn_id(), row->get_primary_key());
			num_locks++;
			if (row->manager->get_tid() != txn->accesses[write_set[i]]->tid) {
				INC_STATS(get_thd_id(), silo_127_abort, 1);
				rc = Abort;
				return rc;
			}
		}
	}

	COMPILER_BARRIER

	// validate rows in the read set
	// for repeatable_read, no need to validate the read set.
	for (uint64_t i = 0; i < txn->row_cnt - wr_cnt; i ++) {
		Access * access = txn->accesses[ read_set[i] ];
		bool success = access->orig_row->manager->validate(access->tid, false);
		if (!success) {
			rc = Abort;
			return rc;
		}
		if (access->tid > max_tid)
			max_tid = access->tid;
	}
	// validate rows in the write set
	for (uint64_t i = 0; i < wr_cnt; i++) {
		Access * access = txn->accesses[ write_set[i] ];
		bool success = access->orig_row->manager->validate(access->tid, true);
		if (!success) {
			INC_STATS(get_thd_id(), silo_155_abort, 1);
			rc = Abort;
			return rc;
		}
		if (access->tid > max_tid)
			max_tid = access->tid;
	}

	this->max_tid = max_tid;
	find_tid_silo(max_tid);
	// if (this->max_tid > this->_cur_tid)
		// this->_cur_tid = this->max_tid;
	return rc;
}

RC
TxnManager::finish(RC rc)
{
	if (rc == Abort) {
		if (this->num_locks > get_access_cnt()) 
			return rc;
		// DEBUG("silo abort finish %ld", txn->get_txn_id());
		for (uint64_t i = 0; i < this->num_locks; i++) {
			txn->accesses[ write_set[i] ]->orig_row->manager->release();
			DEBUG("silo %ld abort release row %ld \n", this->get_txn_id(), txn->accesses[ write_set[i] ]->orig_row->get_primary_key());
		}
	} else {
		
		for (uint64_t i = 0; i < txn->write_cnt; i++) {
			Access * access = txn->accesses[ write_set[i] ];
			access->orig_row->manager->write( 
				access->data, this->commit_timestamp );
			txn->accesses[ write_set[i] ]->orig_row->manager->release();
			DEBUG("silo %ld commit release row %ld \n", this->get_txn_id(), txn->accesses[ write_set[i] ]->orig_row->get_primary_key());
		}
	}
	num_locks = 0;
	memset(write_set, 0, 100);
	// mem_allocator.free(write_set, sizeof(int) * txn->write_cnt);
	return rc;
}

RC
TxnManager::find_tid_silo(ts_t max_tid)
{
	if (max_tid > _cur_tid)
		_cur_tid = max_tid;
	return RCOK;
}

#endif
