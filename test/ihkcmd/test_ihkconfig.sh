#!/bin/sh
. ./config.sh

errcnt=0

case_num=$1
expect_ret=$2
expect_out=$3
shift 3

out=`sudo ${SBIN_DIR}/ihkconfig 0 $* 2>&1`
prev_ret=$?

if [ "X${expect_ret}" == "Xnot0" ]; then
	if [ ${prev_ret} -eq 0 ]; then
		(( errcnt++ ))
	fi
else
	if [ ${prev_ret} -ne ${expect_ret} ]; then
		(( errcnt++ ))
	fi
fi

if [ "X${out}" != "X" -a "X${expect_out}" != "X" ]; then
	echo ${out} | grep ${expect_out} &> /dev/null
	if [ $? -ne 0 ]; then
		(( errcnt++ ))
	fi
else 
	if [ "X${out}" != "X${expect_out}" ]; then
		(( errcnt ++ ))
	fi
fi

if [ ${errcnt} -eq 0 ]; then
	echo "[`printf %03d ${case_num}`:OK] ihkconfig 0 $* , ret:${prev_ret} out:\"${out}\""
else
	echo "[`printf %03d ${case_num}`:NG] ihkconfig 0 $* , ret:${prev_ret} out:\"${out}\""
fi

exit ${errcnt}
