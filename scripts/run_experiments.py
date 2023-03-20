#!/usr/bin/python

import os,sys,datetime,re
import shlex
import subprocess
from experiments import *
from helper import *
from run_config import *
import glob
import time

now = datetime.datetime.now()
strnow=now.strftime("%Y%m%d-%H%M%S")

os.chdir('..')

PATH=os.getcwd()

result_dir = PATH + "/results/" + strnow + '/'
perf_dir = result_dir + 'perf/'

cfgs = configs

execute = True
remote = False
cluster = None
skip = False

exps=[]
arg_cluster = False
merge_mode = False
perfTime = 60
fromtimelist=[]
totimelist=[]
cpu_usage_index=0

if len(sys.argv) < 2:
     sys.exit("Usage: %s [-exec/-e/-noexec/-ne] [-c cluster] experiments\n \
            -exec/-e: compile and execute locally (default)\n \
            -noexec/-ne: compile first target only \
            -c: run remote on cluster; possible values: istc, vcloud\n \
            " % sys.argv[0])

for arg in sys.argv[1:]:
    if arg == "--help" or arg == "-h":
        sys.exit("Usage: %s [-exec/-e/-noexec/-ne] [-skip] [-c cluster] experiments\n \
                -exec/-e: compile and execute locally (default)\n \
                -noexec/-ne: compile first target only \
                -skip: skip any experiments already in results folder\n \
                -c: run remote on cluster; possible values: istc, vcloud\n \
                " % sys.argv[0])
    if arg == "--exec" or arg == "-e":
        execute = True
    elif arg == "--noexec" or arg == "-ne":
        execute = False
    elif arg == "--skip":
        skip = True
    elif arg == "-c":
        remote = True
        arg_cluster = True
    elif arg == '-m':
        merge_mode = True
    elif arg_cluster:
        cluster = arg
        arg_cluster = False
    else:
        exps.append(arg)

for exp in exps:
    fmt,experiments = experiment_map[exp]()

    for e in experiments:
        cfgs = get_cfgs(fmt,e)
        if remote:
            cfgs["TPORT_TYPE"], cfgs["TPORT_TYPE_IPC"], cfgs["TPORT_PORT"] = "tcp", "false", 7100
        output_f = get_outfile_name(cfgs, fmt)
        output_dir = output_f + "/"
        output_f += strnow

        f = open("config.h", 'r')
        lines = f.readlines()
        f.close()
        with open("config.h", 'w') as f_cfg:
            for line in lines:
                found_cfg = False
                for c in cfgs:
                    found_cfg = re.search("#define " + c + "\t", line) or re.search("#define " + c + " ", line)
                    if found_cfg:
                        f_cfg.write("#define " + c + " " + str(cfgs[c]) + "\n")
                        break
                if not found_cfg:
                    f_cfg.write(line)

        cmd = "make clean; make deps; make -j32"
        print cmd
        os.system(cmd)
        if not execute:
            exit()

        if execute:
            cmd = "mkdir -p {}".format(perf_dir)
            print cmd
            os.system(cmd)
            cmd = "cp config.h {}{}.cfg".format(result_dir,output_f)
            print cmd
            os.system(cmd)

            if remote:
                if cluster == 'istc':
                    machines_ = istc_machines
                    uname = istc_uname
                    cfg_fname = "istc_ifconfig.txt"
                elif cluster == 'vcloud':
                    machines_ = vcloud_machines
                    uname = vcloud_uname
                    uname2 = username
                    cfg_fname = "vcloud_ifconfig.txt"
                else:
                    assert(False)

                # machines = machines_[:(cfgs["NODE_CNT"]+1)]
                machines = machines_[:(cfgs["NODE_CNT"]+cfgs["CLIENT_NODE_CNT"])]
                with open("ifconfig.txt", 'w') as f_ifcfg:
                    for m in machines:
                        f_ifcfg.write(m + "\n")

                if cfgs["WORKLOAD"] == "TPCC":
                    files = ["rundb", "runcl", "ifconfig.txt", "./benchmarks/TPCC_short_schema.txt", "./benchmarks/TPCC_full_schema.txt"]
                elif cfgs["WORKLOAD"] == "YCSB":
                    files = ["rundb", "runcl", "ifconfig.txt", "benchmarks/YCSB_schema.txt"]
                for m, f in itertools.product(machines, files):
                    if cluster == 'istc':
                        cmd = 'scp {}/{} {}.csail.mit.edu:/{}/'.format(PATH, f, m, uname)
                    elif cluster == 'vcloud':
                        os.system('./scripts/kill.sh {}'.format(m))
                        cmd = 'scp {}/{} {}:/{}'.format(PATH, f, m, uname)
                    print cmd
                    os.system(cmd)

                # for m in machines:
                #     os.system('./scripts/kill.sh {}'.format(m))

                # for f in files:
                #     cmd = 'cp {}/{} /{}'.format(PATH, f, uname)
                #     print cmd
                #     os.system(cmd)
                # cmd = 'sleep 1'
                # os.system(cmd)

                print("Deploying: {}".format(output_f))
                os.chdir('./scripts')
                if cluster == 'istc':
                    cmd = 'sh deploy.sh \'{}\' /{}/ {}'.format(' '.join(machines), uname, cfgs["NODE_CNT"])
                elif cluster == 'vcloud':
                    cmd = 'sh vcloud_deploy.sh \'{}\' /{}/ {} {} {}'.format(' '.join(machines), uname, cfgs["NODE_CNT"], perfTime, uname2)
                print cmd
                fromtimelist.append(str(int(time.time())) + "000")
                os.system(cmd)
                totimelist.append(str(int(time.time())) + "000")
                perfip = machines[0]
                cmd = "scp getFlame.sh {}:/{}/".format(perfip, uname)
                print cmd
                os.system(cmd)
                cmd = 'ssh {} "bash /{}/getFlame.sh"'.format(perfip, uname)
                print cmd
                os.system(cmd)
                cmd = "scp {}:/{}/perf.svg {}{}.svg".format(perfip, uname, perf_dir, output_f)
                print cmd
                os.system(cmd)
                os.chdir('..')
                cpu_usage_path=PATH + "/results/" + strnow + '/cpu_usage_' + str(cpu_usage_index)
                # cpu_usage_avg_path = PATH + "/results/" + strnow + '/cpu_usage_avg'
                os.mkdir(cpu_usage_path)
                cpu_usage_index+=1
                for m, n in zip(machines, range(len(machines))):
                    if cluster == 'istc':
                        cmd = 'scp {}.csail.mit.edu:/{}/results.out {}{}_{}.out'.format(m,uname,result_dir,n,output_f)
                        print cmd
                        os.system(cmd)
                    elif cluster == 'vcloud':
                        cmd = 'scp {}:/{}/dbresults{}.out results/{}/{}_{}.out'.format(m,uname,n,strnow,n,output_f)
                        print cmd
                        os.system(cmd)
                        cmd = 'scp {}:/tmp/{}* {}/'.format(m,uname2,cpu_usage_path)
                        print cmd
                        os.system(cmd)

            else:
                nnodes = cfgs["NODE_CNT"]
                nclnodes = cfgs["NODE_CNT"]
                pids = []
                print("Deploying: {}".format(output_f))
                for n in range(nnodes+nclnodes):
                    if n < nnodes:
                        cmd = "sh rundb -nid{}".format(n)
                    else:
                        cmd = "sh runcl -nid{}".format(n)
                    print(cmd)
                    cmd = shlex.split(cmd)
                    ofile_n = "{}{}_{}.out".format(result_dir,n,output_f)
                    ofile = open(ofile_n,'w')
                    p = subprocess.Popen(cmd,stdout=ofile,stderr=ofile)
                    pids.insert(0,p)
                for n in range(nnodes + nclnodes):
                    pids[n].wait()

    al = []
    for e in experiments:
        al.append(e[2])
    al = sorted(list(set(al)))

    tcnt = []
    for e in experiments:
        tcnt.append(e[-2])
    tcnt = sorted(list(set(tcnt)))

    cocnt = []
    for e in experiments:
        cocnt.append(e[-1])
    cocnt = sorted(list(set(cocnt)))
    sk = []
    for e in experiments:
        sk.append(e[-2])
    sk = sorted(list(set(sk)))

    wr = []
    for e in experiments:
        wr.append(e[-5])
    wr = sorted(list(set(wr)))

    cn = []
    for e in experiments:
        cn.append(e[1])
    cn = sorted(list(set(cn)))

    ld = []
    for e in experiments:
        ld.append(e[-3])
    ld = sorted(list(set(ld)))

    part = []
    for e in experiments:
        part.append(e[4])
    part = sorted(list(set(part)))

    tpcc_ld = []
    for e in experiments:
        tpcc_ld.append(e[-1])
    tpcc_ld = sorted(list(set(tpcc_ld)))

    ccnt = []
    for e in experiments:
        ccnt.append(e[-1])
    ccnt = sorted(list(set(ccnt)))

    cmd = ''
    os.chdir('./scripts')
    if exp == 'ycsb_skew' or exp == 'ycsb_skew1':
        cmd = 'sh result.sh -a ycsb_skew -n {} -c {} -s {} -t {}'.format(str(cn[0]), ','.join([str(x) for x in al]), ','.join([str(x) for x in sk]), strnow)
    elif exp == 'ycsb_writes':
        cmd='sh result.sh -a ycsb_writes -n {} -c {} --wr {} -t {}'.format(cn[0], ','.join([str(x) for x in al]), ','.join([str(x) for x in wr]), strnow)
    elif 'ycsb_scaling' in exp:
        cmd='sh result.sh -a ycsb_scaling -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_tcp' in exp:
        cmd='sh result.sh -a ycsb_scaling_tcp -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_two_sided' in exp:
        cmd='sh result.sh -a ycsb_scaling_two_sided -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_one_sided' in exp:
        cmd='sh result.sh -a ycsb_scaling_one_sided -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_coroutine' in exp:
        cmd='sh result.sh -a ycsb_scaling_coroutine -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_dbpa' in exp:
        cmd='sh result.sh -a ycsb_scaling_dbpa -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_scaling_all' in exp:
        cmd='sh result.sh -a ycsb_scaling_all -n {} -c {} -t {} --ft {} --tt {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'tpcc_scaling' in exp:
        cmd='sh result.sh -a tpcc_scaling -n {} -c {} -t {}'.format(','.join([str(x) for x in cn]), ','.join([str(x) for x in al]), strnow)
    elif 'ycsb_stress' in exp:
        cmd='sh result.sh -a ycsb_stress -n {} -c {} -s {} -l {} -t {}'.format(str(cn[0]), ','.join([str(x) for x in al]), str(sk[0]), ','.join([str(x) for x in ld]), strnow)
    elif 'tpcc_stress' in exp:
        cmd='sh result.sh -a tpcc_stress -n {} -c {} -l {} -t {}'.format(str(cn[0]), ','.join([str(x) for x in al]), ','.join([str(x) for x in tpcc_ld]), strnow)
    elif 'tpcc_cstress' in exp:
        cmd='sh result.sh -a tpcc_stress_ctx -n {} -c {} -l {} -C {} -t {} --ft {} --tt {}'.format(str(cn[0]), ','.join([str(x) for x in al]), ','.join([str(x) for x in ld]), ','.join([str(x) for x in ccnt]), strnow, ','.join(fromtimelist), ','.join(totimelist))
    elif 'ycsb_thread' in exp:
        cmd='sh result.sh -a ycsb_thread -n {} -c {} -t {} -T {}'.format(str(cn[0]), ','.join([str(x) for x in al]), strnow, ','.join([str(x) for x in tcnt]))
    elif 'tpcc_thread' in exp:
        cmd='sh result.sh -a tpcc_thread -n {} -c {} -t {} -T {}'.format(str(cn[0]), ','.join([str(x) for x in al]), strnow, ','.join([str(x) for x in tcnt]))
    elif 'ycsb_partitions' in exp:
        cmd='sh result.sh -a ycsb_partitions -n {} -c {} -t {} -P {}'.format(str(cn[0]), ','.join([str(x) for x in al]), strnow, ','.join([str(x) for x in part]))
    elif 'ycsb_coroutine' in exp:
        cmd='sh result.sh -a ycsb_coroutine -n {} -c {} -t {} -CO {}'.format(str(cn[0]), ','.join([str(x) for x in al]), strnow, ','.join([str(x) for x in cocnt]))
    print cmd
    os.system(cmd)
    print cmd

    cmd=''
    os.chdir('../draw')
    if exp == 'ycsb_skew':
       cmd='./deneva-plot.sh -a ycsb_skew -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif exp == 'ycsb_writes':
       cmd='./deneva-plot.sh -a ycsb_writes -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif 'ycsb_scaling' in exp:
       cmd='./deneva-plot.sh -a ycsb_scaling -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif 'tpcc_scaling' in exp:
       cmd='./deneva-plot.sh -a tpcc_scaling2 -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif 'ycsb_stress' in exp:
       cmd='./deneva-plot.sh -a ycsb_stress -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif 'tpcc_stress' in exp:
       cmd='./deneva-plot.sh -a tpcc_stress -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    elif 'tpcc_cstress' in exp:
       cmd='./deneva-plot-his.sh -a tpcc_cstress -c {} -t {}'.format(','.join([str(x) for x in al]), strnow)
    print cmd
    os.system(cmd)
