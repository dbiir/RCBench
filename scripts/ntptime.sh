set -x
ps -aux | grep runcl | awk '{print $2}' | xargs kill -9 2>/dev/null 1>/dev/null
ps -aux | grep rundb | awk '{print $2}' | xargs kill -9 2>/dev/null 1>/dev/null

ssh 10.77.110.144 "sudo ntpdate 10.77.110.146" 2>/dev/null 1>/dev/null
ssh 10.77.110.145 "sudo ntpdate 10.77.110.146" 2>/dev/null 1>/dev/null
ssh 10.77.110.147 "sudo ntpdate 10.77.110.146" 2>/dev/null 1>/dev/null
ssh 10.77.110.148 "sudo ntpdate 10.77.110.146" 2>/dev/null 1>/dev/null

