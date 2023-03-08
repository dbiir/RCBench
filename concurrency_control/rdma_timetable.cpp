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
#include "helper.h"
#include "txn.h"
#include "rdma_maat.h"
#include "manager.h"
#include "mem_alloc.h"
#include "rdma_timetable.h"
#include "rdma.h"
//#include "src/allocator_master.hh"
#include "qps/op.hh"
#include "string.h"

#if CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_MAAT || CC_ALG == RDMA_WOUND_WAIT || CC_ALG == RDMA_TS || CC_ALG == RDMA_TS1 || CC_ALG == RDMA_MAAT_H
void RdmaTxnTable::init() {
	table_size = (RDMA_TXNTABLE_MAX)*(sizeof(RdmaTxnTableNode)+1);
	assert(table_size < rdma_txntable_size);
	DEBUG_M("RdmaTxnTable::init table alloc\n");
	table = (RdmaTxnTableNode*)rdma_txntable_buffer; 

	//printf("%d",table[0].key);
	uint64_t i = 0;
	#if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
		for( i = 0; i < RDMA_TXNTABLE_MAX; i++) {
			table[i].init();
		}
	#else
		for( i = 0; i < RDMA_TXNTABLE_MAX; i++) {
			table[i].init(i);
		}		
	#endif
	// printf("%d",table[200].key);
	// printf("init %ld rdmaTxnTable\n", i);

}
uint64_t RdmaTxnTable::hash(uint64_t key) { return key % table_size; }
#if RDMA_SIT == SIT_HG
    // 单双边混合场景
    // txn-id 与 thdid不是一一对应的，所以只能使用轮询的方式
    // ! 注意目前TS1算法不适用于这个方式.
    bool RdmaTxnTable::init(uint64_t thd_id, uint64_t key) {
        int i = 0;
        for (; i < RDMA_TXNTABLE_MAX; i++) {
            // DEBUG_C("Time table %lu-%lu for %ld\n", i, table[i].nodes[0].key, key);
            if (table[i].nodes[0].key == key) break;
        }
        if (i == RDMA_TXNTABLE_MAX){ // 如果table中没有当前事务
            i = 0;
            for (; i < RDMA_TXNTABLE_MAX; i++) {
                // DEBUG_C("Time table %lu-%lu for %ld\n", i, table[i].nodes[0].key, key);
                if (table[i].nodes[0].key == UINT64_MAX) break;
            }
        }
        DEBUG_C("MAAT table find index %lu to init %ld\n", i, key);
        if (i == RDMA_TXNTABLE_MAX) {
            return false;// 如果table中没有空位 
        } 
        else {
            table[i].init(key);
            return true;
        }
    }
    void RdmaTxnTable::release(uint64_t thd_id, uint64_t key) {
        for (int i = 0; i < RDMA_TXNTABLE_MAX; i++) {
            if (table[i].nodes[0].key == key) {
                table[i].release(key);
            }
        }
    }
    uint64_t RdmaTxnTable::remote_get_timeNode_index(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t timenode_addr = (rdma_buffer_size - rdma_txntable_size);
        uint64_t thd_id = txnMng->get_thd_id();

        RdmaTxnTableNode * item = txnMng->read_remote_timetable_full(yield,node_id,timenode_addr,cor_id);
        uint64_t index = 0;
        for (; index < RDMA_TXNTABLE_MAX; index++) {
            if (item[index].nodes[0].key == key) {
                break;
            }
        }
        mem_allocator.free(item,(RDMA_TXNTABLE_MAX)*(sizeof(RdmaTxnTableNode)+1));
        if (index == RDMA_TXNTABLE_MAX) return UINT64_MAX;   
        else return index;     
    }
    bool RdmaTxnTable::remote_cas_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        #if !MAAT_CAS
            return true;
        #else
            uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
            uint64_t res = txnMng->cas_remote_content(yield,node_id,timenode_addr,0,txnMng->get_txn_id(),cor_id);
            if (res == 0 || res == txnMng->get_txn_id()) {
                return true;
            }
            return false;
        #endif
    }
    bool RdmaTxnTable::remote_release_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        #if !MAAT_CAS
            return true;
        #else
            uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
            uint64_t timenode_size = sizeof(uint64_t);
            // each thread uses only its own piece of client memory address
            uint64_t thd_id = txnMng->get_thd_id();    
            uint64_t lock = 0;
            uint64_t res = txnMng->cas_remote_content(yield,node_id,timenode_addr,txnMng->get_txn_id(),0,cor_id);
            return true;
        #endif
    }
    bool RdmaTxnTable::remote_set_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id) {
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        assert(timenode_addr >= rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();    
        value->_lock = 0;
        // ::memcpy(test_buf, (char *)value, timenode_size);

        assert(txnMng->write_remote_row(yield,node_id,timenode_size,timenode_addr,(char *)value,cor_id) == true);
        return true;
    }
    RdmaTxnTableNode * RdmaTxnTable::remote_get_timeNode(yield_func_t &yield, uint64_t node_id, uint64_t index, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();

        RdmaTxnTableNode * item = txnMng->read_remote_timetable(yield,node_id,timenode_addr,cor_id);
        return item;        
    } 
    bool RdmaTxnTable::remote_reset_timeNode(yield_func_t &yield, uint64_t node_id,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        RdmaTxnTableNode* node = (RdmaTxnTableNode*)mem_allocator.alloc(sizeof(RdmaTxnTableNode));
        node->init();
        uint64_t index = remote_get_timeNode_index(yield, node_id,txnMng,key,cor_id);
        if (index == UINT64_MAX) return false;
        DEBUG_C("MAAT table remote release node loc:%lu index:%lu txn %lu\n", node_id, index, key);
        remote_set_timeNode(yield, node_id,index,txnMng,key,node,cor_id);
        mem_allocator.free(node,sizeof(RdmaTxnTableNode));
        node = remote_get_timeNode(yield, node_id,index,txnMng,key,cor_id);
        DEBUG_C("MAAT table remote release node loc:%lu index:%lu txn:%lu key:%lu\n", node_id, index, key, node->nodes[0].key);
        return true;
    }
    uint64_t RdmaTxnTable::local_get_timeNode_index(uint64_t key) {
        for (int i = 0; i < RDMA_TXNTABLE_MAX; i++) {
            if (table[i].nodes[0].key == key) {
                return i;
            }
        }
        return RDMA_TXNTABLE_MAX;
    }
#else
    // txn-id 与 thdid 有对应关系，因此我们为每个线程开辟一部分内存空间。
    bool RdmaTxnTable::init(uint64_t thd_id, uint64_t key) {
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        #if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
        return table[index].init(key);
        #else
        table[index].init(key);
        return true;
        #endif
    }
    void RdmaTxnTable::release(uint64_t thd_id, uint64_t key) {
        //table[key]._lock = g_node_id;
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        #if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
            table[index].release(key);
            // lower = 0;
            // table[index].upper = UINT64_MAX;
            // table[index].key = key;
            // table[index].state = MAAT_RUNNING;
        #endif
        #if CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WOUND_WAIT
            table[index].key = key;
            table[index].state = WOUND_RUNNING;
        #endif
        #if CC_ALG == RDMA_TS
            table[index].key = key;
            table[index].state = TS_RUNNING;
        #endif
        //table[key]._lock = 0;
    }
    uint64_t RdmaTxnTable::remote_get_timeNode_index(yield_func_t &yield, uint64_t node_id, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        return index;     
    }
    bool RdmaTxnTable::remote_cas_timeNode(yield_func_t &yield,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        #if !MAAT_CAS
            return true;
        #else
            uint64_t node_id = key % g_node_cnt;
            // assert(node_id != g_node_id);
            uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
            uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
            uint64_t res = txnMng->cas_remote_content(yield,node_id,timenode_addr,0,txnMng->get_txn_id(),cor_id);
            if (res == 0 || res == txnMng->get_txn_id()) {
                return true;
            }
            return false;
        #endif
    }
    bool RdmaTxnTable::remote_release_timeNode(yield_func_t &yield,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        #if !MAAT_CAS
            return true;
        #else
            uint64_t node_id = key % g_node_cnt;
            // assert(node_id != g_node_id);
            uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
            // todo: here we need to get the corresponding index
            // uint64_t index_key = 0;
            uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
            uint64_t timenode_size = sizeof(uint64_t);
            // each thread uses only its own piece of client memory address
            uint64_t thd_id = txnMng->get_thd_id();    
            uint64_t lock = 0;
            uint64_t res = txnMng->cas_remote_content(yield,node_id,timenode_addr,txnMng->get_txn_id(),0,cor_id);
            return true;
        #endif
    }
    bool RdmaTxnTable::remote_set_timeNode(yield_func_t &yield, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        // assert(node_id != g_node_id);
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();    
        value->_lock = 0;
        // ::memcpy(test_buf, (char *)value, timenode_size);

        assert(txnMng->write_remote_row(yield,node_id,timenode_size,timenode_addr,(char *)value,cor_id) == true);
        return true;
    }
    RdmaTxnTableNode * RdmaTxnTable::remote_get_timeNode(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        assert(key % g_node_cnt != g_node_id);
        uint64_t node_id = key % g_node_cnt;
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);

        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();

        RdmaTxnTableNode * item = txnMng->read_remote_timetable(yield,node_id,timenode_addr,cor_id);
        return item;        
    }
    bool RdmaTxnTable::remote_reset_timeNode(yield_func_t &yield, uint64_t node_id,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        RdmaTxnTableNode* node = (RdmaTxnTableNode*)mem_allocator.alloc(sizeof(RdmaTxnTableNode));
        node->init();
        uint64_t index = remote_get_timeNode_index(yield, node_id,txnMng,key,cor_id);
        if (index == UINT64_MAX) return false;
        DEBUG_C("MAAT table remote release node loc:%lu index:%lu txn %lu\n", node_id, index, key);
        remote_set_timeNode(yield, node_id,index,txnMng,key,node,cor_id);
        mem_allocator.free(node,sizeof(RdmaTxnTableNode));
        node = remote_get_timeNode(yield, node_id,index,txnMng,key,cor_id);
        DEBUG_C("MAAT table remote release node loc:%lu index:%lu txn:%lu key:%lu\n", node_id, index, key, node->nodes[0].key);
        return true;
    }
    uint64_t RdmaTxnTable::local_get_timeNode_index(uint64_t key) {
        uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        return index;
    }
#endif

#if CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == RDMA_WOUND_WAIT
    WOUNDState RdmaTxnTable::local_get_state(uint64_t thd_id, uint64_t key) {
        WOUNDState state = WOUND_RUNNING;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        state = table[index].nodes[0].state;   
        // printf("index: %ld, key: %ld\n", index, table[index].key);
        //table[key]._lock = 0;
        return state;
    }
    bool RdmaTxnTable::local_set_state(TxnManager *txnMng, uint64_t thd_id, uint64_t key, WOUNDState value) {
        uint64_t node_id = key % g_node_cnt;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        uint64_t timenode_addr = (char*)(&table[index]) - rdma_global_buffer + sizeof(uint64_t) * 2;
        // table[index].state = value;

        return txnMng->cas_remote_content(node_id,timenode_addr,WOUND_RUNNING,value);
    }
    char * RdmaTxnTable::remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = remote_get_timeNode_index(yield,node_id,txnMng,key,cor_id);
        
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        // printf("init table index: %ld\n", key);
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();
        char * item = txnMng->read_remote_txntable(yield,node_id,timenode_addr,cor_id);
        // printf("WOUNDState:%ld\n", value);
        return item; 
    }
    bool RdmaTxnTable::remote_set_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, WOUNDState value, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        // assert(node_id != g_node_id);
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = remote_get_timeNode_index(yield,node_id,txnMng,key,cor_id);
        if (index == UINT64_MAX ) return false;
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size) + sizeof(uint64_t) * 2;
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();
    
        assert(txnMng->write_remote_row(yield,node_id,timenode_size,timenode_addr,(char *)&value,cor_id) == true);
        return true;
    }
#endif

#if CC_ALG == RDMA_TS
    TSState RdmaTxnTable::local_get_state(uint64_t thd_id, uint64_t key) {
        TSState state = TS_RUNNING;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        state = table[index].state;   
        // printf("index: %ld, key: %ld\n", index, table[index].key);
        //table[key]._lock = 0;
        return state;
    }
    void RdmaTxnTable::local_set_state(uint64_t thd_id, uint64_t key, TSState value) {
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        table[index].state = value;
    }
    char * RdmaTxnTable::remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = remote_get_timeNode_index(yield,node_id,txnMng,key,cor_id);
        
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        // printf("init table index: %ld\n", key);
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();
        char * item = txnMng->read_remote_txntable(yield,node_id,timenode_addr,cor_id);
        // printf("WOUNDState:%ld\n", value);
        return item; 
    }
    void RdmaTxnTable::remote_set_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, RdmaTxnTableNode * value, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        assert(node_id != g_node_id);
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;//get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = remote_get_timeNode_index(yield,node_id,txnMng,key,cor_id);
        if (index == UINT64_MAX) return false;
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();
        // uint64_t try_lock = txnMng->cas_remote_content(yield,node_id,timenode_addr,0,key,cor_id);

        assert(txnMng->write_remote_row(yield,node_id,timenode_size,timenode_addr,(char *)value,cor_id) == true);
    }
#endif

#if CC_ALG == RDMA_TS1
    TSState RdmaTxnTable::local_get_state(uint64_t thd_id, uint64_t key) {
        TSState state = TS_RUNNING;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;/
        uint64_t index = local_get_timeNode_index(key);
        int i = 0;
        for (i = 0; i < RDMA_TSSTATE_COUNT; i++) {
            if (table[index].nodes[i].key == key) break;
        }
        if (i == RDMA_TSSTATE_COUNT) return TS_ABORTING;
        state = table[index].nodes[i].state;   
        return state;
    }
    void RdmaTxnTable::local_set_state(uint64_t thd_id, uint64_t key, TSState value) {
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        uint64_t min_key = 0, min_idx = 0;
        int i = 0;
        for (i = 0; i < RDMA_TSSTATE_COUNT; i++) {
            if (table[index].nodes[i].key == key) break;
        }
        if (i == RDMA_TSSTATE_COUNT) return;
        table[index].nodes[i].state = value;
    }
    TSState RdmaTxnTable::remote_get_state(yield_func_t &yield, TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        uint64_t node_id = key % g_node_cnt;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);//key;
        uint64_t index = remote_get_timeNode_index(yield,node_id,txnMng,key,cor_id);
        
        // todo: here we need to get the corresponding index
        // uint64_t index_key = 0;
        // printf("init table index: %ld\n", key);
        uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
        uint64_t timenode_size = sizeof(RdmaTxnTableNode);
        // each thread uses only its own piece of client memory address
        uint64_t thd_id = txnMng->get_thd_id();
        char * item = txnMng->read_remote_txntable(yield,node_id,timenode_addr,cor_id);
        RdmaTxnTableNode* node = (RdmaTxnTableNode*)item;
        int i = 0;
        TSState state;
        for (i = 0; i < RDMA_TSSTATE_COUNT; i++) {
            if (node->nodes[i].key == key) break;
        }
        if (i == RDMA_TSSTATE_COUNT) state = TS_ABORTING;
        else state = node->nodes[i].state;  
        mem_allocator.free(item,0);
        return state; 
    }
#endif

#if CC_ALG == RDMA_MAAT || CC_ALG == RDMA_MAAT_H
    bool RdmaTxnTable::local_is_key(uint64_t key) {
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) return true;
        }
        return false;
    }
    uint64_t RdmaTxnTable::local_get_lower(uint64_t key) {
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                DEBUG_C("%lu get lower %ld: [%lu,%lu]\n",key,table[index].nodes[i].key,table[index].nodes[i].lower,table[index].nodes[i].upper);
                return table[index].nodes[i].lower;
            }
        }
        return UINT64_MAX;
    }
    uint64_t RdmaTxnTable::local_get_upper(uint64_t key) {
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                DEBUG_C("%lu get upper %ld: [%lu,%lu]\n",key,table[index].nodes[i].key,table[index].nodes[i].lower,table[index].nodes[i].upper);
                return table[index].nodes[i].upper;
            }
        }
        return 0;
    }
    bool RdmaTxnTable::local_set_lower(TxnManager *txnMng, uint64_t key, uint64_t value) {
        //table[key]._lock = g_node_id;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                table[index].nodes[i].lower = value;
                return true;
            }
        }
        return false;
    }
    bool RdmaTxnTable::local_set_upper(TxnManager *txnMng, uint64_t key, uint64_t value) {
        //table[key]._lock = g_node_id;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                table[index].nodes[i].upper = value;
                return true;
            }
        }
        return false;
    }
    MAATState RdmaTxnTable::local_get_state(uint64_t key) {
        //table[key]._lock = g_node_id;
        MAATState state = MAAT_ABORTED;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                state = table[index].nodes[i].state;
            }
        }
        return state;
    }
    bool RdmaTxnTable::local_set_state(TxnManager *txnMng, uint64_t key, MAATState value) {
        //table[key]._lock = g_node_id;
        // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
        uint64_t index = local_get_timeNode_index(key);
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (table[index].nodes[i].key == key) {
                table[index].nodes[i].state = value;
                return true;
            }
        }
        return false;
    }
    bool RdmaTxnTable::remote_is_key(RdmaTxnTableNode * root, uint64_t key) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) return true;
        }
        return false;
    }
    uint64_t RdmaTxnTable::remote_get_lower(RdmaTxnTableNode * root, uint64_t key) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) return root->nodes[i].lower;
        }
        return UINT64_MAX;
    }
    uint64_t RdmaTxnTable::remote_get_upper(RdmaTxnTableNode * root, uint64_t key) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) return root->nodes[i].upper;
        }
        return 0;
    }
    bool RdmaTxnTable::remote_set_lower(RdmaTxnTableNode * root, uint64_t key, uint64_t value) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) {
                root->nodes[i].lower = value;
                return true;
            }
        }
        return false;
    }
    bool RdmaTxnTable::remote_set_upper(RdmaTxnTableNode * root, uint64_t key, uint64_t value) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) {
                root->nodes[i].upper = value;
                return true;
            }
        }
        return false;
    }
    bool RdmaTxnTable::remote_release(RdmaTxnTableNode * root, uint64_t key) {
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) {
                root->nodes[i].lower = 0;
                root->nodes[i].upper = UINT64_MAX;
                root->nodes[i].key = UINT64_MAX;
                root->nodes[i].state = MAAT_RUNNING;
                return true;
            }
        }
        return false;
    }
    MAATState RdmaTxnTable::remote_get_state(RdmaTxnTableNode * root, uint64_t key) {
        MAATState state = MAAT_ABORTED;
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) {
                state = root->nodes[i].state;
            }
        }
        return state;
    }
    bool RdmaTxnTable::remote_set_state(RdmaTxnTableNode * root, uint64_t key, MAATState value) {
        //table[key]._lock = g_node_id;
        for (int i = 0; i < MAAT_NODES_COUNT; i++) {
            if (root->nodes[i].key == key) {
                root->nodes[i].state = value;
                return true;
            }
        }
        return false;
    }
    bool RdmaTxnTable::local_cas_timeNode(yield_func_t &yield,TxnManager *txnMng, uint64_t key, uint64_t cor_id) {
        #if !MAAT_CAS
            return true;
        #else
            // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
            uint64_t index = local_get_timeNode_index(key);
            uint64_t timenode_addr = (index) * sizeof(RdmaTxnTableNode) + (rdma_buffer_size - rdma_txntable_size);
            uint64_t res = txnMng->cas_remote_content(yield,g_node_id,timenode_addr,0,txnMng->get_txn_id(),cor_id);
            
            if (res == 0 || res == txnMng->get_txn_id()) {
                return true;
            }
            return false;
        #endif
    }
    void RdmaTxnTable::local_release_timeNode(TxnManager *txnMng, uint64_t key) {
        #if !MAAT_CAS
            return;
        #else
            // uint64_t index = get_cor_id_from_txn_id(key) * g_thread_cnt + get_thd_id_from_txn_id(key);
            uint64_t index = local_get_timeNode_index(key);
            table[index]._lock = 0;
        #endif
    }
#endif
#endif