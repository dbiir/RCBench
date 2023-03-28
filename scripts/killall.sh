set -x
for i in $(seq 1 12)
do
    ssh 192.168.1.$i "ps -aux | grep rundb | awk '{print \$2}' | xargs kill -9" 2>/dev/null 1>/dev/null
    ssh 192.168.1.$i "ps -aux | grep runcl | awk '{print \$2}' | xargs kill -9" 2>/dev/null 1>/dev/null
    # ssh 192.168.1.$i "rm -rf /data/core/*" 2>/dev/null 1>/dev/null
    # ssh 192.168.1.$i "rm -rf /home/core/*" 2>/dev/null 1>/dev/null
    # ssh 192.168.1.$i "rm -rf /home/u2021000884/core*" 2>/dev/null 1>/dev/null
done
# for i in $(seq 7 16)
# do
#     ssh 192.168.1.$i "ps -aux | grep rundb | awk '{print \$2}' | xargs kill -9" 2>/dev/null 1>/dev/null
#     ssh 192.168.1.$i "ps -aux | grep runcl | awk '{print \$2}' | xargs kill -9" 2>/dev/null 1>/dev/null
#     # ssh 192.168.1.$i "rm -rf /data/core/*" 2>/dev/null 1>/dev/null
#     # ssh 192.168.1.$i "rm -rf /home/core/*" 2>/dev/null 1>/dev/null
#     ssh 192.168.1.$i "rm -rf /home/u2021000884/core*" 2>/dev/null 1>/dev/null
# done
