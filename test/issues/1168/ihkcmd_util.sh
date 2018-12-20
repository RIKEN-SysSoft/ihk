#!/bin/sh

. ${HOME}/.mck_test_config

ihk_smp_opt="ihk_start_irq=240 ihk_ikc_irq_core=0"

function prepare {
	sudo ${SBIN}/mcstop+release.sh
	sleep 1
	sudo insmod ${MCK_DIR}/kmod/ihk.ko
	sudo insmod ${MCK_DIR}/kmod/ihk-smp-x86_64.ko ${ihk_smp_opt}

	sudo ${SBIN}/ihkconfig 0 create
}

# $1: expect_ret
# $2: expect_output
# $3-: command to pass to ihkcmd
function chk_ihkconfig {
	errcnt=0

	expect_ret=$1
	expect_out=$2
	shift 2

	out=`sudo ${SBIN}/ihkconfig 0 $* 2>&1`
	ihk_ret=$?

	if [ "X${expect_ret}" == "Xnot0" ]; then
		if [ ${ihk_ret} -eq 0 ]; then
			(( errcnt++ ))
		fi
	else
		if [ ${ihk_ret} -ne ${expect_ret} ]; then
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
		echo "[ OK ] ihkconfig $*   =>  ret:${ihk_ret} out:\"${out}\""
	else
		echo "[ NG ] ihkconfig $*   =>  ret:${ihk_ret} out:\"${out}\""
	fi

}

# $1: expect_ret
# $2: expect_output
# $3-: command to pass to ihkcmd
function chk_ihkosctl {
	errcnt=0

	expect_ret=$1
	expect_out=$2
	shift 2

	out=`sudo ${SBIN}/ihkosctl 0 $* 2>&1`
	ihk_ret=$?

	if [ "X${expect_ret}" == "Xnot0" ]; then
		if [ ${ihk_ret} -eq 0 ]; then
			(( errcnt++ ))
		fi
	else
		if [ ${ihk_ret} -ne ${expect_ret} ]; then
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
		echo "[ OK ] ihkosctl $*   =>  ret:${ihk_ret} out:\"${out}\""
	else
		echo "[ NG ] ihkosctl $*   =>  ret:${ihk_ret} out:\"${out}\""
	fi

}

