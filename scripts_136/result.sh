set -x

PHASE=5

while [[ $# -gt 0 ]]
do
    case $1 in
        -a)
            TEST_TYPE=$2
            shift
            shift
            ;;
        -c)
            CC=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        -C)
            CT=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        --ft)
            FT=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        --tt)
            TT=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        -n)
            NUMBEROFNODE=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        -p)
            PHASE=$2
            shift
            shift
            ;;
        -s)
            SKEW=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        -t)
            RESULT_PATH=../results/$2
            shift
            shift
            ;;
        --wr)
            WR=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        -l)
            LOAD=($(echo $2 | tr ',' ' '))
            shift
            shift
            ;;
        *)
            shift
            ;;
    esac
done

addContent() {
    echo $1 >> ${RESULT_PATH}/index.html
}

addHeading() {
    addContent "<h$1>$2</h$1>"
}

addParagraph() {
    addContent "<p>$1</p>"
}

addTableTitle() {
    addContent '<table border=1>'
}

addTableTuple() {
    addContent '<tr>'
    for arg in "$@"
    do
        addContent "<td>${arg}</td>"
    done
    addContent "</tr>"
}

addTableTail() {
    addContent '</table>'
}

addLabel() {
    addContent "<label>$1</label>"
}

initHTMLFile() {
    rm -rf ${RESULT_PATH}/index.html
    addContent '<!DOCTYPE html>'
    addContent '<html lang="zh-CN">'
    addContent '<head><meta charset="UTF-8"><title>Report</title><style type="text/css">
            td{
                text-align: center;
            }
        </style></head>'
    addContent '<body>'
    if [[ ${TEST_TYPE} == "tpcc_scaling" ]]
    then
        addHeading 1 'Deneva TPCC性能测试报告'
        addParagraph '本次测试是TPC-C测试'
    elif [[ ${TEST_TYPE} == "ycsb_scaling" ]]
    then
        addHeading 1 'Deneva YCSB性能测试报告'
        addParagraph '本次测试是YCSB测试'
    else
        addHeading 1 'TDSQLV3 Sysbench性能测试报告'
        addParagraph "本次测试是Sysbench-${TESTTYPENAME}测试(Provided by Sysbench 1.1.0)"
    fi
}

EndHtmlFile() {
    addContent "</body></html>"
}

initHTMLFile
addHeading 2 "测试结果"

if [[ "${TEST_TYPE}" == 'ycsb_skew' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    for cc in ${CC[@]}
    do
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for skew in ${SKEW[@]}
        do
            echo -n ${skew}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep ${cc} | grep _SKEW-${skew}_ | grep ^${i}_)
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done

            python parse_results.py $AS >> ${TMPFILE}
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
    done
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/latency.png
elif [[ "${TEST_TYPE}" == 'ycsb_scaling' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    addTableTitle
    addContent '<tr>'
    addContent "<td>AlgoName\\NodeCount</td>"
    for arg in ${NUMBEROFNODE[@]}
    do
        addContent "<td colspan=\"3\">${arg}</td>"
    done
    addContent '</tr>'
    for cc in ${CC[@]}
    do
        addContent '<tr>'
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        addContent "<td>${cc}</td>"
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for nn in ${NUMBEROFNODE[@]}
        do
            echo -n ${nn}" " >> ${TMPFILE}
            AS=''

            TMPN=${nn}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep [0-9]_${cc}_ | grep _N-${nn}_ | grep ^${i}_)
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done
            tmpresult=$(python parse_results.py $AS)
            echo ${tmpresult} >> ${TMPFILE}
            tput=$(echo ${tmpresult} | awk '{print $1}')
            ar=$(echo ${tmpresult} | awk '{print $2}')
            dr=$(echo ${tmpresult} | awk '{print $3}')
            addContent "<td>${tput}</td>"
            addContent "<td>${ar}</td>"
            addContent "<td>${dr}</td>"
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
        addContent "</tr>"
    done
    addTableTail
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/
    addHeading 2 "时间使用占比分析图"
    addContent "<img src=\"./1.png\" />"
    addHeading 2 "资源使用分析"
    indexlen=${#FT[@]}
    let indexlen--
    indexofcc=0
    indexofnn=0
    addTableTitle
    addTableTuple "实验参数" "rundbCPU使用率" "rundb内存使用率" "runclCPU使用率" "runcl内存使用率" "性能监控"
    for index in $(seq 0 ${indexlen})
    do
        ft=${FT[${index}]}
        ft0=$(echo $ft | cut -b -10)
        tt=${TT[${index}]}
        tt0=$(echo $tt | cut -b -10)
        let pt0=tt0-ft0
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbmem=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclmem=${a[-2]}
        addTableTuple ${CC[${indexofcc}]}"\\"${NUMBEROFNODE[${indexofnn}]} $rundbcpu $rundbmem $runclcpu $runclmem "<a href=\"http://9.39.242.189:8081/d/e2HCbA5Gk/denevamonitor?orgId=1&from=${ft}&to=${tt}\">性能监控</a>"
        let indexofcc++
        if [[ "$indexofcc" == "${#CC[@]}" ]]
        then
            indexofcc=0
            let indexofnn++
        fi
    done
    addTableTail
    addHeading 2 "rundb Perf 图"
    for f in $(ls ${RESULT_PATH}/perf)
    do
        addParagraph "$f"
        addContent "<img src=\"./perf/$f\" />"
    done
    EndHtmlFile
elif [[ "${TEST_TYPE}" == 'ycsb_writes' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    for cc in ${CC[@]}
    do
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for wr in ${WR[@]}
        do
            echo -n ${wr}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep ${cc} | grep _WR-${wr}_ | grep ^${i}_)
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done

            python parse_results.py $AS >> ${TMPFILE}
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
    done
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/latency.png
elif [[ "${TEST_TYPE}" == 'tpcc_scaling' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    addTableTitle
    addContent '<tr>'
    addContent "<td>AlgoName\\NodeCount</td>"
    for arg in ${NUMBEROFNODE[@]}
    do
        addContent "<td colspan=\"3\">${arg}</td>"
    done
    addContent '</tr>'
    for cc in ${CC[@]}
    do
        addContent '<tr>'
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        addContent "<td>${cc}</td>"
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for nn in ${NUMBEROFNODE[@]}
        do
            echo -n ${nn}" " >> ${TMPFILE}
            AS=''

            TMPN=${nn}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep [0-9]_${cc}_ | grep _N-${nn}_ | grep ^${i}_)
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done
            tmpresult=$(python parse_results.py $AS)
            echo ${tmpresult} >> ${TMPFILE}
            tput=$(echo ${tmpresult} | awk '{print $1}')
            ar=$(echo ${tmpresult} | awk '{print $2}')
            dr=$(echo ${tmpresult} | awk '{print $3}')
            addContent "<td>${tput}</td>"
            addContent "<td>${ar}</td>"
            addContent "<td>${dr}</td>"
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
        addContent "</tr>"
    done
    addTableTail
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/
    addHeading 2 "时间使用占比分析图"
    addContent "<img src=\"./1.png\" />"
    addHeading 2 "资源使用分析"
    indexlen=${#FT[@]}
    let indexlen--
    indexofcc=0
    indexofnn=0
    addTableTitle
    addTableTuple "实验参数" "rundbCPU使用率" "rundb内存使用率" "runclCPU使用率" "runcl内存使用率" "性能监控"
    for index in $(seq 0 ${indexlen})
    do
        ft=${FT[${index}]}
        ft0=$(echo $ft | cut -b -10)
        tt=${TT[${index}]}
        tt0=$(echo $tt | cut -b -10)
        let pt0=tt0-ft0
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbmem=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclmem=${a[-2]}
        addTableTuple ${CC[${indexofcc}]}"\\"${NUMBEROFNODE[${indexofnn}]} $rundbcpu $rundbmem $runclcpu $runclmem "<a href=\"http://9.39.242.189:8081/d/e2HCbA5Gk/denevamonitor?orgId=1&from=${ft}&to=${tt}\">性能监控</a>"
        let indexofcc++
        if [[ "$indexofcc" == "${#CC[@]}" ]]
        then
            indexofcc=0
            let indexofnn++
        fi
    done
    addTableTail
    addHeading 2 "rundb Perf 图"
    for f in $(ls ${RESULT_PATH}/perf)
    do
        addParagraph "$f"
        addContent "<img src=\"./perf/$f\" />"
    done
    EndHtmlFile
elif [[ "${TEST_TYPE}" == 'ycsb_stress' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    for cc in ${CC[@]}
    do
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for load in ${LOAD[@]}
        do
            echo -n ${load}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep ${cc}_TIF-${load}_ | grep _SKEW-${SKEW[0]}_ | grep ^${i}_)
                echo $f
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done

            python parse_results.py $AS >> ${TMPFILE}
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
    done
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/latency.png
elif [[ "${TEST_TYPE}" == 'tpcc_stress' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    for cc in ${CC[@]}
    do
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for load in ${LOAD[@]}
        do
            echo -n ${load}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep ${cc}_TIF-${load}_ | grep ^${i}_)
                echo $f
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done

            python parse_results.py $AS >> ${TMPFILE}
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
    done
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/latency.png
elif [[ "${TEST_TYPE}" == 'tpcc_stress_ctx' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    addTableTitle
    addContent '<tr>'
    addContent "<td>AlgoName\\Load</td>"
    for arg in ${LOAD[@]}
    do
        addContent "<td colspan=\"3\">${arg}</td>"
    done
    addContent '</tr>'
    for cc in ${CC[@]}
    do
        addContent '<tr>'
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        addContent "<td>${cc}</td>"
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}

        for load in ${LOAD[@]}
        do
            echo -n ${nn}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep [0-9]_${cc}_ | grep _CT-${CT}_TIF-${load}_ | grep ^${i}_)
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done
            tmpresult=$(python parse_results.py $AS)
            echo ${tmpresult} >> ${TMPFILE}
            tput=$(echo ${tmpresult} | awk '{print $1}')
            ar=$(echo ${tmpresult} | awk '{print $2}')
            dr=$(echo ${tmpresult} | awk '{print $3}')
            addContent "<td>${tput}</td>"
            addContent "<td>${ar}</td>"
            addContent "<td>${dr}</td>"
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
        addContent "</tr>"
    done
    addTableTail
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/
    addHeading 2 "时间使用占比分析图"
    addContent "<img src=\"./1.png\" />"
    addHeading 2 "资源使用分析"
    indexlen=${#FT[@]}
    let indexlen--
    indexofcc=0
    indexofnn=0
    addTableTitle
    addTableTuple "实验参数" "rundbCPU使用率" "rundb内存使用率" "runclCPU使用率" "runcl内存使用率" "性能监控"
    for index in $(seq 0 ${indexlen})
    do
        ft=${FT[${index}]}
        ft0=$(echo $ft | cut -b -10)
        tt=${TT[${index}]}
        tt0=$(echo $tt | cut -b -10)
        let pt0=tt0-ft0
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"rundb\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        rundbmem=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_cpu{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclcpu=${a[-2]}
        a=($(curl -g "http://9.39.242.189:8080/api/v1/query?query=avg_over_time(KPI_PROCESS_mem{pname=\"runcl\"}[${pt0}s])&time=${tt0}" | tr "\"" "\n"))
        runclmem=${a[-2]}
        addTableTuple ${CC[${indexofcc}]}"\\"${LOAD[${indexofnn}]} $rundbcpu $rundbmem $runclcpu $runclmem "<a href=\"http://9.39.242.189:8081/d/e2HCbA5Gk/denevamonitor?orgId=1&from=${ft}&to=${tt}\">性能监控</a>"
        let indexofcc++
        if [[ "$indexofcc" == "${#CC[@]}" ]]
        then
            indexofcc=0
            let indexofnn++
        fi
    done
    addTableTail
    addHeading 2 "rundb Perf 图"
    for f in $(ls ${RESULT_PATH}/perf)
    do
        addParagraph "$f"
        addContent "<img src=\"./perf/$f\" />"
    done
    EndHtmlFile
elif [[ "${TEST_TYPE}" == 'tpcc_client_stress' ]]
then
    LATFILE=lat
    LTFILE=lt
    rm -rf ${LATFILE} ${LTFILE}
    touch ${LATFILE} ${LTFILE}
    for cc in ${CC[@]}
    do
        LS=''
        echo -n ${cc}" " >> ${LATFILE}
        TMPFILE=tmp-${cc}
        rm -rf ${TMPFILE}
        touch ${TMPFILE}
        for ct in ${CT[@]}
        do
            echo -n ${ct}" " >> ${TMPFILE}
            AS=''

            TMPN=${NUMBEROFNODE[0]}
            load=${LOAD[0]}
            let TMPN--
            for i in $(seq 0 $TMPN)
            do
                f=$(ls ${RESULT_PATH} | grep -v .cfg | grep ${cc}_CT-${ct}_TIF-${load}_ | grep ^${i}_)
                echo $f
                AS=${AS}$(readlink -f ${RESULT_PATH}/$f)" "
                LS=${LS}$(readlink -f ${RESULT_PATH}/$f)" "
            done

            python parse_results.py $AS >> ${TMPFILE}
        done
        python parse_latency.py $LS >> ${LTFILE}
        mv ${TMPFILE} ${RESULT_PATH}/
    done
    echo >> ${LATFILE}
    echo "abort_time txn_manager_time txn_validate_time txn_cleanup_time txn_total_process_time" >> ${LATFILE}
    awk -F' ' '{for(i=1;i<=NF;i=i+1){a[NR,i]=$i}}END{for(j=1;j<=NF;j++){str=a[1,j];for(i=2;i<=NR;i++){str=str " " a[i,j]}print str}}' ${LTFILE} >> ${LATFILE}
    /data1/deneva/anaconda3/bin/python3 getLATENCY.py ${LATFILE} ${PHASE}
    mv 1.png ${RESULT_PATH}/latency.png
fi
