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

#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "global.h"
#include "helper.h"
#include "logger.h"
#include "array.h"

class ycsb_request;
class LogRecord;
struct Item_no;

class Message {
public:
  virtual ~Message(){}
  static Message * create_message(char * buf);
  static Message * create_message(BaseQuery * query, RemReqType rtype);
  static Message * create_message(TxnManager * txn, RemReqType rtype);
  static Message * create_message(uint64_t txn_id, RemReqType rtype);
  static Message * create_message(uint64_t txn_id,uint64_t batch_id, RemReqType rtype);
  static Message * create_message(LogRecord * record, RemReqType rtype);
  static Message * create_message(RemReqType rtype);
  static std::vector<Message*> * create_messages(char * buf);
  static void release_message(Message * msg);
  RemReqType rtype;
  uint64_t txn_id;
  uint64_t batch_id;
  uint64_t return_node_id;
  uint64_t dest_node;

  uint64_t wq_time;
  uint64_t mq_time;
  uint64_t ntwk_time;
  //uint64_t txn_type;
  //uint64_t seq_id;
  //uint64_t trans_id;
  // network part
  uint64_t send_time;

  // Collect other stats
  double lat_work_queue_time;
  double lat_msg_queue_time;
  double lat_cc_block_time;
  double lat_cc_time;
  double lat_process_time;
  double lat_network_time;
  double lat_other_time;

  uint64_t mget_size();
  uint64_t get_txn_id() {return txn_id;}
  uint64_t get_batch_id() {return batch_id;}
  uint64_t get_return_id() {return return_node_id;}
  uint64_t set_dest_node(uint64_t dest_id) {dest_node = dest_id;}
  void mcopy_from_buf(char * buf);
  void mcopy_to_buf(char * buf);
  void mcopy_from_txn(TxnManager * txn);
  void mcopy_to_txn(TxnManager * txn);
  RemReqType get_rtype() {return rtype;}

  virtual uint64_t get_size() = 0;
  virtual void copy_from_buf(char * buf) = 0;
  virtual void copy_to_buf(char * buf) = 0;
  virtual void copy_to_txn(TxnManager * txn) = 0;
  virtual void copy_from_txn(TxnManager * txn) = 0;
  virtual void init() = 0;
  virtual void release() = 0;
};

// Message types
class InitDoneMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}
};

class FinishMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release();
  bool is_abort() { return rc == Abort;}

  uint64_t pid;
  RC rc;
  //uint64_t txn_id;
  //uint64_t batch_id;
  bool readonly;
#if CC_ALG == MAAT || CC_ALG == WOOKONG || CC_ALG == SSI || CC_ALG == WSI || \
    CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == DLI_MVCC_OCC || \
    CC_ALG == DLI_MVCC || CC_ALG == SILO || CC_ALG == RDMA_MAAT_H || CC_ALG == RDMA_SILO  || CC_ALG == RDMA_TS1|| CC_ALG == RDMA_MVCC
  uint64_t commit_timestamp;
#endif
#if ! RDMA_ONE_SIDED_CO && RDMA_ONE_SIDED_RW
  Array<Access*> accesses;
#endif
};

class LogMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release();
  void copy_from_record(LogRecord * record);

  //Array<LogRecord*> log_records;
  LogRecord record;
};

class LogRspMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}
};

class LogFlushedMessage : public Message {
public:
  void copy_from_buf(char * buf) {}
  void copy_to_buf(char * buf) {}
  void copy_from_txn(TxnManager * txn) {}
  void copy_to_txn(TxnManager * txn) {}
  uint64_t get_size() {return sizeof(LogFlushedMessage);}
  void init() {}
  void release() {}

};


class QueryResponseMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release();

  RC rc;
  uint64_t pid;
#if CC_ALG == TICTOC
  uint64_t _min_commit_ts;
#endif
#if CC_ALG == RDMA_MAAT_H
  Array<uint64_t> uncommitted_reads;
  Array<uint64_t> uncommitted_writes;
  Array<uint64_t> uncommitted_writes_y;
  uint64_t greatest_write_timestamp;
  uint64_t greatest_read_timestamp;
#endif
#if RDMA_ONE_SIDED_CO
  Array<Access*> accesses;
#endif
};

class AckMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}

  RC rc;
#if CC_ALG == MAAT || CC_ALG == WOOKONG || CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == RDMA_MAAT_H
  uint64_t lower;
  uint64_t upper;
#endif
#if CC_ALG == SILO || CC_ALG == RDMA_SILO
  uint64_t max_tid;
#endif

  // For Calvin PPS: part keys from secondary lookup for sequencer response
  Array<uint64_t> part_keys;
};

class PrepareMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release();

  uint64_t pid;
  RC rc;
#if CC_ALG == TICTOC
  uint64_t _min_commit_ts;
#endif
  uint64_t txn_id;
#if CC_ALG == RDMA_MAAT_H
  Array<uint64_t> uncommitted_reads;
  Array<uint64_t> uncommitted_writes;
  Array<uint64_t> uncommitted_writes_y;
  uint64_t greatest_write_timestamp;
  uint64_t greatest_read_timestamp;
#endif
#if ! RDMA_ONE_SIDED_CO
  Array<Access*> accesses;
#endif
};

class ForwardMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}

  RC rc;
#if WORKLOAD == TPCC
	uint64_t o_id;
#endif
};


class DoneMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}
  uint64_t batch_id;
};

class ClientResponseMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}

  RC rc;
  uint64_t client_startts;
};

class ClientQueryMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_query(BaseQuery * query);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  uint64_t pid;
  uint64_t ts;
#if CC_ALG == CALVIN || CC_ALG == RDMA_CALVIN
  uint64_t batch_id;
  uint64_t txn_id;
#endif
  uint64_t client_startts;
  uint64_t first_startts;
  Array<uint64_t> partitions;
};

class YCSBClientQueryMessage : public ClientQueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_query(BaseQuery * query);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  Array<ycsb_request*> requests;

};

class TPCCClientQueryMessage : public ClientQueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_query(BaseQuery * query);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  uint64_t txn_type;
	// common txn input for both payment & new-order
  uint64_t w_id;
  uint64_t d_id;
  uint64_t c_id;

  // payment
  uint64_t d_w_id;
  uint64_t c_w_id;
  uint64_t c_d_id;
	char c_last[LASTNAME_LEN];
  uint64_t h_amount;
  bool by_last_name;

  // new order
  Array<Item_no*> items;
  bool rbk;
  bool remote;
  uint64_t ol_cnt;
  uint64_t o_entry_d;

};

class PPSClientQueryMessage : public ClientQueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_query(BaseQuery * query);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  uint64_t txn_type;

  // getparts
  uint64_t part_key;
  // getproducts / getpartbyproduct
  uint64_t product_key;
  // getsuppliers / getpartbysupplier
  uint64_t supplier_key;

  // part keys from secondary lookup
  Array<uint64_t> part_keys;

  bool recon;
};
class DAClientQueryMessage : public ClientQueryMessage {
 public:
  void copy_from_buf(char* buf);//ok
  void copy_to_buf(char* buf);//ok
  void copy_from_query(BaseQuery* query);//ok
  void copy_from_txn(TxnManager* txn);//ok
  void copy_to_txn(TxnManager* txn);
  uint64_t get_size();
  void init();
  void release();

  DATxnType txn_type;
	uint64_t trans_id;
	uint64_t item_id;
	uint64_t seq_id;
	uint64_t write_version;
	uint64_t state;
	uint64_t next_state;
	uint64_t last_state;
};

class QueryMessage : public Message {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init() {}
  void release() {}

  uint64_t pid;
  bool is_readonly;
#if CC_ALG == WAIT_DIE || CC_ALG == RDMA_WAIT_DIE2 || CC_ALG == TIMESTAMP || CC_ALG == MVCC || CC_ALG == DTA || CC_ALG == WOOKONG || CC_ALG == RDMA_WOUND_WAIT2 || CC_ALG == CICADA || CC_ALG == WOUND_WAIT || CC_ALG == RDMA_WAIT_DIE || CC_ALG == RDMA_WOUND_WAIT || CC_ALG == RDMA_MOCC || CC_ALG == RDMA_TS1|| CC_ALG == RDMA_MVCC
  uint64_t ts;
#endif
#if CC_ALG == MVCC || CC_ALG == WOOKONG || CC_ALG == DTA || CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3
  uint64_t thd_id;
#endif
#if CC_ALG == OCC || CC_ALG == FOCC || CC_ALG == BOCC || CC_ALG == SSI || CC_ALG == WSI || \
    CC_ALG == DLI_BASE || CC_ALG == DLI_OCC || CC_ALG == DLI_MVCC_OCC || \
    CC_ALG == DLI_DTA || CC_ALG == DLI_DTA2 || CC_ALG == DLI_DTA3 || CC_ALG == DLI_MVCC
  uint64_t start_ts;
#endif
#if MODE==QRY_ONLY_MODE
  uint64_t max_access;
#endif
#if RDMA_ONE_SIDED_CO
  uint64_t abort_cnt;
#endif
};

class YCSBQueryMessage : public QueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  Array<ycsb_request*> requests;

};

class TPCCQueryMessage : public QueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  uint64_t txn_type;
  uint64_t state;

	// common txn input for both payment & new-order
  uint64_t w_id;
  uint64_t d_id;
  uint64_t c_id;

  // payment
  uint64_t d_w_id;
  uint64_t c_w_id;
  uint64_t c_d_id;
	char c_last[LASTNAME_LEN];
  uint64_t h_amount;
  bool by_last_name;

  // new order
  Array<Item_no*> items;
	bool rbk;
  bool remote;
  uint64_t ol_cnt;
  uint64_t o_entry_d;

};

class PPSQueryMessage : public QueryMessage {
public:
  void copy_from_buf(char * buf);
  void copy_to_buf(char * buf);
  void copy_from_txn(TxnManager * txn);
  void copy_to_txn(TxnManager * txn);
  uint64_t get_size();
  void init();
  void release();

  uint64_t txn_type;
  uint64_t state;

  // getparts
  uint64_t part_key;
  // getproducts / getpartbyproduct
  uint64_t product_key;
  // getsuppliers / getpartbysupplier
  uint64_t supplier_key;

  // part keys from secondary lookup
  Array<uint64_t> part_keys;
};

class DAQueryMessage : public QueryMessage {
 public:
  void copy_from_buf(char* buf);
  void copy_to_buf(char* buf);
  void copy_from_txn(TxnManager* txn);
  void copy_to_txn(TxnManager* txn);
  uint64_t get_size();
  void init();
  void release();

  DATxnType txn_type;
	uint64_t trans_id;
	uint64_t item_id;
	uint64_t seq_id;
	uint64_t write_version;
	uint64_t state;
	uint64_t next_state;
	uint64_t last_state;
};

#endif
