RCBench
=======

RCBench is an RDMA-enabled transaction testbed that aims to provide a unified evaluation tool for assessing the transaction scalability of various concurrency control algorithms. The usability and advancement of RCBench primarily come from the proposed RDMA-native primitives, which facilitate the convenient implementation of RDMA-enabled concurrency control algorithms. Additionally, various optimization principles are proposed to ensure that concurrency control algorithms in RCBench can fully benefit from the advantages offered by RDMA-capable networks.

RCBench consists of a set of distributed transactions that are executed on a set of nodes equipped with RDMA capabilities. The transactions are designed to involve a large number of data nodes, and the performance of the transactions is measured using various metrics, such as response time, CPU usage, and memory usage. The experiments conducted on RCBench show that by exploiting the capabilities of RDMA, concurrency control algorithms in RCBench can obtain 42× performance improvement, and transaction scalability can be achieved in RCBench.

The contributions of this paper include: (1) the proposal of an RDMA-native primitive that facilitates the convenient implementation of RDMA-enabled concurrency control algorithms; (2) the development of a set of distributed transactions that involve a large number of data nodes and are designed to test the scalability of various concurrency control algorithms; (3) the performance evaluation of mainstream concurrency control algorithms using RCBench; and (4) the demonstration of the effectiveness of the proposed optimization principles for improving the scalability of concurrency control algorithms in RCBench.

Overall, RCBench provides a valuable tool for the community to evaluate the performance and scalability of various concurrency control algorithms, and it can be used to test and compare the effectiveness of different approaches for solving the weak transaction scalability problem.

To illustrate the detailed re-implementation methods, we place the complete descriptions of re-implementations, including Wait-Die, Wound-Wait, MVCC, MaaT, and Cicada, in the technical report https://github.com/dbiir/RCBench/blob/master/RCBench.pdf.
    
The most recent code of each algorithms is in branch rdma-4.9.

The hybird approach is in branch RCBench-hg

Dependencies
------------
To ensure that the code works properly, the server needs to have the following dependencies:
- g++ >= 6.4.0
- Boost = 1.6.1
- jemalloc >= 5.2.1
- nanomsg >= 1.1.5
- libevent >= 1.2
- libibverbs

Build
--------------
- `git clone https://github.com/dbiir/RCBench.git`
- `make clean`
- `make deps`
- `make -j16`

Configuration
-------------
In `scripts\run_config.py`, the `vcloud_uname` and `vcloud_machines` need to be changed for running. and the experiments specific configuration can be found in `scripts\experiments.py`.

Run
-------------
### Transaction scalbility
To test the transaction scalbility, run the following command:
- `cd scripts`
- `python run_experiments.py -e -c vcloud ycsb_partitions`

### System scalbility
To test the system scalbility, run the following command:
- `cd scripts`
- `python run_experiments.py -e -c vcloud ycsb_scaling`

### The impact of contention level
To test the impact of contention level, run the following command:
- `cd scripts`
- `python run_experiments.py -e -c vcloud ycsb_skew`

### the impact of read-write ratio
To test the impact of read-write ratio, run the following command:
- `cd scripts`
- `python run_experiments.py -e -c vcloud ycsb_write`

