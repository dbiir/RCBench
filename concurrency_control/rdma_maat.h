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

#ifndef _RDMA_MAAT_H_
#define _RDMA_MAAT_H_

#include "row.h"
#include "semaphore.h"
#include "maat.h"
#include "row_rdma_maat.h"
#include "routine.h"

#if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H

class TxnManager;

class RDMA_Maat {
public:
	void init();
	RC validate(yield_func_t &yield, TxnManager * txn, uint64_t cor_id);
	RC finish(yield_func_t &yield, RC rc, TxnManager *txnMng, uint64_t cor_id);
	RC find_bound(TxnManager * txn);
	RC remote_abort(yield_func_t &yield, TxnManager * txn, Access * data, uint64_t cor_id);
	RC remote_commit(yield_func_t &yield, TxnManager * txn, Access * data, uint64_t cor_id);
	// RdmaTxnTableNode * read_remote_timetable(yield_func_t &yield, TxnManager * txn, uint64_t node_id, uint64_t cor_id);
	// RdmaTxnTableNode * read_remote_timetable(TxnManager * txn, uint64_t node_id);
private:
	sem_t 	_semaphore;
};
#endif

#endif

