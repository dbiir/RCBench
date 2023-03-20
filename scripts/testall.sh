
# python run_experiments.py -e -c vcloud ycsb_scaling_h
# sleep 10
# python run_experiments.py -e -c vcloud tpcc_scaling_p
# sleep 10
# python run_experiments.py -e -c vcloud tpcc_scaling_n
# sleep 10


# python run_experiments.py -e -c vcloud ycsb_partitions
# sleep 10
# python run_experiments.py -e -c vcloud ycsb_partitions_h
# sleep 10
# python run_experiments.py -e -c vcloud ycsb_scaling_l 
# sleep 10
# python run_experiments.py -e -c vcloud ycsb_scaling_m
# sleep 10
# python run_experiments.py -e -c vcloud ycsb_scaling_h
# sleep 10
# python run_experiments.py -e -c vcloud ycsb_partitions
# sleep 10
# python run_experiments.py -e -c vcloud tpcc_scaling_p
# sleep 10
# python run_experiments.py -e -c vcloud tpcc_scaling_n
# sleep 10

python run_experiments.py -e -c vcloud ycsb_partitions_8h
sleep 10
python run_experiments.py -e -c vcloud ycsb_partitions_8
sleep 10
python run_experiments.py -e -c vcloud ycsb_scaling_h
sleep 10