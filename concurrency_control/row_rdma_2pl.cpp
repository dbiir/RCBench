#include "helper.h"
#include "manager.h"
#include "mem_alloc.h"
#include "row.h"
#include "txn.h"
#include "rdma.h"
#include "qps/op.hh"
#include "row_rdma_2pl.h"
#include "global.h"

#if CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2

void Row_rdma_2pl::init(row_t * row){
	_row = row;
}

void Row_rdma_2pl::info_decode(uint64_t lock_info,uint64_t& lock_type,uint64_t& lock_num){
    lock_type = (lock_info)&1;  //第1位为锁类型信息 0：shared_lock 1: mutex_lock
    lock_num = (lock_info>>1); //第2位及以后为锁数量信息
}
void Row_rdma_2pl::info_encode(uint64_t& lock_info,uint64_t lock_type,uint64_t lock_num){
    lock_info = (lock_num<<1)+lock_type;
}

bool Row_rdma_2pl::conflict_lock(uint64_t lock_info, lock_t l2, uint64_t& new_lock_info) {
    uint64_t lock_type; 
    uint64_t lock_num;
    info_decode(lock_info,lock_type,lock_num);
    
    if(lock_num == 0) {//无锁
        if(l2 == DLOCK_EX) info_encode(new_lock_info, 1, 1);
        else info_encode(new_lock_info, 0, 1);
        return false;
    }
    if(l2 == DLOCK_EX || lock_type == 1)  return true;
    else{ //l2==DLOCK_SH&&lock_type == 0
        uint64_t new_lock_num = lock_num + 1;
        //if(new_lock_num != 1) printf("---new_lock_num:%lu\n",new_lock_num);
        info_encode(new_lock_info,lock_type,new_lock_num);
		if(new_lock_info == 0) {
        printf("---lock_info:%lu, lock_type:%lu, lock_num:%lu\n",lock_info,lock_type,lock_num);
        printf("---new_lock_info:%lu, lock_type:%lu, new_lock_num:%lu\n",new_lock_info, lock_type, new_lock_num);
        }
        return false;
    }
}

RC Row_rdma_2pl::lock_get(yield_func_t &yield,lock_t type, TxnManager * txn, row_t * row,uint64_t cor_id) {  //本地加锁
	assert(CC_ALG == RDMA_NO_WAIT || CC_ALG == RDMA_NO_WAIT2 || CC_ALG == RDMA_WAIT_DIE2);
    RC rc;
#if CC_ALG == RDMA_NO_WAIT
atomic_retry_lock:
    uint64_t lock_info = row->_tid_word;
    uint64_t new_lock_info = 0;

    //检测是否有冲突，在conflict=false的情况下得到new_lock_info
    bool conflict = conflict_lock(lock_info, type, new_lock_info);
    if (conflict) {
        rc = Abort;
	    return rc;
    }
    if(new_lock_info == 0){
        printf("---线程号：%lu, 本地加锁失败!!!!!!锁位置: %u; %p , 事务号: %lu, 原lock_info: %lu, new_lock_info: %lu\n", txn->get_thd_id(), g_node_id, &row->_tid_word, txn->get_txn_id(), lock_info, new_lock_info);    
    }
    assert(new_lock_info != 0);
    //RDMA CAS，不用本地CAS
        uint64_t loc = g_node_id;
        uint64_t thd_id = txn->get_thd_id();

        uint64_t try_lock = -1;
        try_lock = txn->cas_remote_content(yield,loc,(char*)row - rdma_global_buffer,lock_info,new_lock_info,cor_id);

		// uint64_t *tmp_buf2 = (uint64_t *)Rdma::get_row_client_memory(thd_id);
		// auto mr = client_rm_handler->get_reg_attr().value();

		// rdmaio::qp::Op<> op;
		// op.set_atomic_rbuf((uint64_t*)(remote_mr_attr[loc].buf + (char*)row - rdma_global_buffer), remote_mr_attr[loc].key).set_cas(lock_info,new_lock_info);
		// assert(op.set_payload(tmp_buf2, sizeof(uint64_t), mr.key) == true);
		// auto res_s2 = op.execute(rc_qp[loc][thd_id], IBV_SEND_SIGNALED);

		// RDMA_ASSERT(res_s2 == IOCode::Ok);
		// auto res_p2 = rc_qp[loc][thd_id]->wait_one_comp();
		// RDMA_ASSERT(res_p2 == IOCode::Ok);

		//if(*tmp_buf2 != lock_info){ //如果CAS失败，原子性被破坏	
		if(try_lock != lock_info){ //如果CAS失败，原子性被破坏	
#if DEBUG_PRINTF
            printf("---atomic_retry_lock\n");
#endif 
            total_num_atomic_retry++;
            txn->num_atomic_retry++;
            if(txn->num_atomic_retry > max_num_atomic_retry) max_num_atomic_retry = txn->num_atomic_retry;
            goto atomic_retry_lock;
            //rc = Abort; 
        }         
        else{   //加锁成功
#if DEBUG_PRINTF
        printf("---线程号：%lu, 本地加锁成功，锁位置: %u; %p , 事务号: %lu, 原lock_info: %lu, new_lock_info: %lu\n", txn->get_thd_id(), g_node_id, &row->_tid_word, txn->get_txn_id(), lock_info, new_lock_info);
#endif
        rc = RCOK;
        } 
/*
    //本地CAS，成功返回1，失败返回0
    auto res = __sync_bool_compare_and_swap(&row->_tid_word, lock_info, new_lock_info);
    //if(res != 0 && res != 1) printf(res);
    assert(res == 0 || res == 1);
    if(res){
#if DEBUG_PRINTF
        printf("---线程号：%lu, 本地加锁成功，锁位置: %u; %p , 事务号: %lu, 原lock_info: %lu, new_lock_info: %lu\n", txn->get_thd_id(), g_node_id, &row->_tid_word, txn->get_txn_id(), lock_info, new_lock_info);
#endif
        rc = RCOK;
    }
    else    rc = Abort;  //原子性被破坏
*/
#endif
#if CC_ALG == RDMA_NO_WAIT2
    //RDMA CAS，不用本地CAS
        uint64_t loc = g_node_id;
        uint64_t try_lock = -1;
        try_lock = txn->cas_remote_content(yield,loc,(char*)row - rdma_global_buffer,0,1,cor_id);

        // uint64_t thd_id = txn->get_thd_id();
		// uint64_t *tmp_buf2 = (uint64_t *)Rdma::get_row_client_memory(thd_id);
		// auto mr = client_rm_handler->get_reg_attr().value();

		// rdmaio::qp::Op<> op;
		// op.set_atomic_rbuf((uint64_t*)(remote_mr_attr[loc].buf + (char*)row - rdma_global_buffer), remote_mr_attr[loc].key).set_cas(0,1);
		// assert(op.set_payload(tmp_buf2, sizeof(uint64_t), mr.key) == true);
		// auto res_s2 = op.execute(rc_qp[loc][thd_id], IBV_SEND_SIGNALED);

		// RDMA_ASSERT(res_s2 == IOCode::Ok);
		// auto res_p2 = rc_qp[loc][thd_id]->wait_one_comp();
		// RDMA_ASSERT(res_p2 == IOCode::Ok);

		// if(*tmp_buf2 != 0) rc = Abort; //加锁冲突，Abort	
		if(try_lock != 0) rc = Abort; //加锁冲突，Abort	
        else{   //加锁成功
#if DEBUG_PRINTF
        printf("---线程号：%lu, 本地加锁成功，锁位置: %u; %p , 事务号: %lu, 原lock_info: 0, new_lock_info: 1\n", txn->get_thd_id(), g_node_id, &row->_tid_word, txn->get_txn_id());
#endif
        rc = RCOK;
        } 
/*
    //本地CAS，成功返回1，失败返回0
    if(__sync_bool_compare_and_swap(&row->_tid_word, 0, 1)){
        //printf("---线程号：%lu, 本地加锁成功，锁位置: %u; %p , 事务号: %lu, 原lock_info: %lu, new_lock_info: %lu\n", txn->get_thd_id(), g_node_id, &row->_tid_word, txn->get_txn_id(), lock_info, new_lock_info);
        rc = RCOK;
    }
    else    rc = Abort;  //加锁冲突
*/
#endif
#if CC_ALG == RDMA_WAIT_DIE2
    		//直接RDMA CAS加锁
local_retry_lock:
        uint64_t loc = g_node_id;
        uint64_t try_lock = -1;
        try_lock = txn->cas_remote_content(yield,loc,(char*)row - rdma_global_buffer,0,tts,cor_id);

        // uint64_t thd_id = txn->get_thd_id();
		// uint64_t *tmp_buf2 = (uint64_t *)Rdma::get_row_client_memory(thd_id);
		// auto mr = client_rm_handler->get_reg_attr().value();
        // uint64_t tts = txn->get_timestamp();

		// rdmaio::qp::Op<> op;
		// op.set_atomic_rbuf((uint64_t*)(remote_mr_attr[loc].buf + (char*)row - rdma_global_buffer), remote_mr_attr[loc].key).set_cas(0,tts);
		// assert(op.set_payload(tmp_buf2, sizeof(uint64_t), mr.key) == true);
		// auto res_s2 = op.execute(rc_qp[loc][thd_id], IBV_SEND_SIGNALED);

		// RDMA_ASSERT(res_s2 == IOCode::Ok);
		// auto res_p2 = rc_qp[loc][thd_id]->wait_one_comp();
		// RDMA_ASSERT(res_p2 == IOCode::Ok);

		// if(*tmp_buf2 != 0){ //如果CAS失败
		if(try_lock != 0){ //如果CAS失败
			if(tts <= *tmp_buf2){  //wait
#if DEBUG_PRINTF
            printf("---local_retry_lock\n");
#endif 
                total_num_atomic_retry++;
                txn->num_atomic_retry++;
                if(txn->num_atomic_retry > max_num_atomic_retry) max_num_atomic_retry = txn->num_atomic_retry;
				//sleep(1);
				goto local_retry_lock;			
			}	
			else{ //abort
				rc = Abort;
			}
		}
        else{ //加锁成功
            rc = RCOK;
        }
#endif
	return rc;
}
#endif