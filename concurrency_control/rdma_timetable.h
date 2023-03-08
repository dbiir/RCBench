/*
*/

#ifndef _RDMA_TIMETABLE_H_
#define _RDMA_TIMETABLE_H_
#include "row.h"
#include "semaphore.h"
#include "maat.h"
#include "row_rdma_maat.h"
#include "routine.h"

enum WOUNDState {
  WOUND_RUNNING = 0,
  WOUND_WOUNDED,
  WOUND_WAITING,
  WOUND_COMMITTING,
  WOUND_ABORTING
};

enum TSState {
  TS_RUNNING = 0,
  TS_WAITING,
  TS_COMMITTING,
  TS_ABORTING,
  TS_NULL
};
struct MAATTableNode {
	uint64_t lower;
	uint64_t upper;
	uint64_t key;
	MAATState state;
};
struct WOUNDTableNode {
	uint64_t key;
	WOUNDState state;
};
struct TSTableNode {
	uint64_t key;
	TSState state;
};

struct RdmaTxnTableNode{
#if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
	uint64_t _lock;
	MAATTableNode nodes[MAAT_NODES_COUNT];
	uint64_t index;
	
	void init() {
		for (uint64_t i = 0; i < MAAT_NODES_COUNT; i++) {
			nodes[i].lower = 0;
			nodes[i].upper = UINT64_MAX;
			nodes[i].key = UINT64_MAX;
			nodes[i].state = MAAT_RUNNING;
			// nodes[i]._lock = 0;
		}
		index = 0;
		_lock = 0;
	}
	bool init(uint64_t key) {
		uint64_t i = find_key(key);
		if (i == MAAT_NODES_COUNT) {
			for (i = 0; i < MAAT_NODES_COUNT; i++) {
				DEBUG_C("MAAT table %lu-%lu\n", i, nodes[i].key);
				if (nodes[i].key == UINT64_MAX) {
					break;
				}
			}
		}
		if (i != MAAT_NODES_COUNT) {
			nodes[i].lower = 0;
			nodes[i].upper = UINT64_MAX;
			nodes[i].key = key;
			nodes[i].state = MAAT_RUNNING;
			return true;
		}
		// for (uint64_t i = 0; i < MAAT_NODES_COUNT; i++) {
		// 	if (nodes[i].key != UINT64_MAX) {
		// 		DEBUG_C("MAAT table %lu-%lu\n", i, nodes[i].key);
		// 		continue;
		// 	}
		// }
		return false;
	}
	uint64_t find_key(uint64_t key) {
		for (uint64_t i = 0; i < MAAT_NODES_COUNT; i++) {
			if (nodes[i].key == key) { return i;}
		}
		return MAAT_NODES_COUNT;
	}
	void release(uint64_t key) {
		for (uint64_t i = 0; i < MAAT_NODES_COUNT; i++) {
			if (nodes[i].key != key) {
				// DEBUG_C("MAAT table %lu-%lu\n", i, nodes[i].key);
				continue;
			}
			nodes[i].lower = 0;
			nodes[i].upper = UINT64_MAX;
			nodes[i].key = UINT64_MAX;
			nodes[i].state = MAAT_RUNNING;
			// DEBUG_C("txn %lu clean MAAT table success\n", key);
			// nodes[i]._lock = 0;
		}
	}	
#endif
#if CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WOUND_WAIT
	uint64_t _lock;
	WOUNDTableNode nodes[1];
	// uint64_t key;
	// WOUNDState state;

	void init() {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = UINT64_MAX;
		nodes[0].state = WOUND_RUNNING;
		_lock = 0;
	}

	void init(uint64_t key) {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = key;
		nodes[0].state = WOUND_RUNNING;
		_lock = 0;
	}

	void release(uint64_t key) {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = UINT64_MAX;
		nodes[0].state = WOUND_RUNNING;
		_lock = 0;
	}
#endif
#if CC_ALG == RDMA_TS
	uint64_t _lock;
	TSTableNode nodes[1];
	// uint64_t key;
	// TSState state;

	void init() {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = UINT64_MAX;
		nodes[0].state = TS_RUNNING;
		_lock = 0;
	}

	void init(uint64_t key) {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = key;
		nodes[0].state = TS_RUNNING;
		_lock = 0;
	}

	void release(uint64_t key) {
		// printf("init table index: %ld\n", key);
		this->nodes[0].key = UINT64_MAX;
		nodes[0].state = TS_RUNNING;
		_lock = 0;
	}
#endif
#if CC_ALG == RDMA_TS1
	uint64_t _lock;
	TSTableNode nodes[RDMA_TSSTATE_COUNT];
	// uint64_t key[RDMA_TSSTATE_COUNT];
	// TSState state[RDMA_TSSTATE_COUNT];

	void init() {
		for (int i = 0; i < RDMA_TSSTATE_COUNT; i++) {
			nodes[i].key = UINT64_MAX;
			nodes[i].state = TS_RUNNING;
		}
		_lock = 0;
	}

	void init(uint64_t key_) {
		uint64_t min_key = 0, min_idx = 0;
		for (int i = 0; i < RDMA_TSSTATE_COUNT; i++) {
			if (nodes[i].key < min_key) {
				min_key = nodes[i].key;
				min_idx = i;
			}
		}
		nodes[min_idx].key = key_;
		nodes[min_idx].state = TS_RUNNING;
	}
	void release(uint64_t key) {
		for (int i = 0; i < RDMA_TSSTATE_COUNT; i++) {
			this->nodes[0].key = UINT64_MAX;
			nodes[0].state = TS_RUNNING;
		}
	}
#endif
};

class RdmaTxnTable {
public:
	void init();
	bool init(uint64_t thd_id, uint64_t key);
	void release(uint64_t thd_id, uint64_t key);
	uint64_t local_get_timeNode_index(uint64_t key);
#if RDMA_SIT == SIT_HG
	uint64_t remote_get_timeNode_index(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	RdmaTxnTableNode * remote_get_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_set_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id);
	bool remote_cas_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index,TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_release_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index,TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_reset_timeNode(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
#else 
	uint64_t remote_get_timeNode_index(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	RdmaTxnTableNode * remote_get_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_set_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id);
	bool remote_cas_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index,TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_release_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index,TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_reset_timeNode(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
#endif
#if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
	bool local_is_key(uint64_t key);
	uint64_t local_get_lower(uint64_t key);
	uint64_t local_get_upper(uint64_t key);
	MAATState local_get_state(uint64_t key);
	bool local_set_lower(TxnManager *txnMng, uint64_t key, uint64_t value);
	bool local_set_upper(TxnManager *txnMng, uint64_t key, uint64_t value);
	bool local_set_state(TxnManager *txnMng, uint64_t key, MAATState value);

	bool remote_is_key(RdmaTxnTableNode * root,uint64_t key);
	uint64_t remote_get_lower(RdmaTxnTableNode * root,uint64_t key);
	uint64_t remote_get_upper(RdmaTxnTableNode * root,uint64_t key);
	MAATState remote_get_state(RdmaTxnTableNode * root,uint64_t key);
	bool remote_set_lower(RdmaTxnTableNode * root, uint64_t key, uint64_t value);
	bool remote_set_upper(RdmaTxnTableNode * root, uint64_t key, uint64_t value);
	bool remote_set_state(RdmaTxnTableNode * root, uint64_t key, MAATState value);
	bool remote_release(RdmaTxnTableNode * root, uint64_t key);

	bool local_cas_timeNode(yield_func_t &yield,TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	void local_release_timeNode(TxnManager *txnMng, uint64_t key);
#endif
#if CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WOUND_WAIT
	WOUNDState local_get_state(uint64_t thd_id, uint64_t key);
	bool local_set_state(TxnManager *txnMng, uint64_t thd_id, uint64_t key, WOUNDState value);

	char * remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	bool remote_set_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, WOUNDState value, uint64_t cor_id);
#endif
#if CC_ALG == RDMA_TS
	TSState local_get_state(uint64_t thd_id, uint64_t key);
	void local_set_state(uint64_t thd_id, uint64_t key, TSState value);

	char * remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
	void remote_set_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id);
#endif
#if CC_ALG == RDMA_TS1
	TSState local_get_state(uint64_t thd_id, uint64_t key);
	void local_set_state(uint64_t thd_id, uint64_t key, TSState value);
	TSState remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id);
#endif
private:
	// hash table
	uint64_t hash(uint64_t key);
	uint64_t table_size;
	RdmaTxnTableNode *table;
	sem_t 	_semaphore;
};

#endif