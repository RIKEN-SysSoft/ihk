#!/usr/bin/bash

. ${HOME}/.mck_test_config

bootopt="-m 256M"
mcexecopt=""
testopt=""
kill="n"
dryrun="n"
sleepopt="0.4"
home=$(eval echo \$\{HOME\})
groups=`groups | cut -d' ' -f 1`
cwd=`pwd`
boot_shutdown=0
mcexec_shutdown=0
ikc_map_by_func=0

#!/bin/sh
BOOTPARAM="-c 1-7,17-23,9-15,25-31 -m 10G@0,10G@1"
USELTP=1
USEOSTEST=



while getopts :bxm OPT
do
	case ${OPT} in
	    b) boot_shutdown=1 # Test shutdown just after boot in 001
		;;
	    x) mcexec_shutdown=1 # Test shutdown just after mcexec in 001
		;;
	    m) ikc_map_by_func=1 # 001
		;;
	    *)  echo "invalid option -${OPT}" >&2
		exit 1
	esac
done

shift $((OPTIND-1))
testname=$1
echo Executing ${testname}

case ${testname} in
    002 | 004 | 005 | 006 | 007 | 008 | 009 | 010 | 012 | 014 | 018)
	printf "*** Apply ${testname}.patch to enable syscall #900 and recompile IHK/McKernel.\n"
	;;
    011)
	printf "*** Apply ${testname}.patch to set kmsg buffer size to 256 and enable syscall #900 and recompile IHK/McKernel.\n"
	;;
    013)
	printf "*** Apply ${testname}.patch to set the size of the kmsg memory-buffer o 256 and enable syscall #900 and set the width of the kmsg file-buffer to 64 and its depth to 4 and then recompile IHK/McKernel.\n"
	;;
    014 | 015)
	printf "*** Apply ${testname}.patch to enable syscall #900 and recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    016)
	printf "*** Apply ${testname}.patch to enable syscall #900 and make ihkmond not release kmsg_buf and the number of stray kmsg_buf allowed in host_driver.c to 2 and then recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    017)
	printf "*** Apply ${testname}.patch to enable syscall #900 and then recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    019)
	printf "*** Apply ${testname}.patch to enable syscall #900 and recompile IHK/McKernel.\n"
	printf "*** Modify values of mck_dir, lastnode, nnodes, ssh, pjsub in ${testname}.sh.\n"
	;;
    *)
	;;
esac

case ${testname} in
    001 | 020 | 021 | 022 | 023 | 024)
	;;
    *)
	read -p "*** Hit return when ready!" key
	;;
esac

case ${testname} in
    001 | 020 | 021 | 023 | 024)
	bn_lin="${testname}_lin"
	make clean > /dev/null 2> /dev/null
	make ${bn_lin}
	;;
    022)
	rm -f ${testname}-pro_lin
	make ${testname}-pro_lin
	if [ $? -ne 0 ]; then echo "make failed"; exit 1; fi
	rm -f ${testname}-epi_lin
	make ${testname}-epi_lin
	if [ $? -ne 0 ]; then echo "make failed"; exit 1; fi
	;;
    002 | 003 | 004 | 005 | 006 | 007 | 008 | 009 | 010 | 011 | 012 | \
	013 | 014 | 015 | 016 | 017)
	bn_lin="${testname}_lin"
	bn_mck="${testname}_mck"
	make clean > /dev/null 2> /dev/null
	make ${bn_lin} ${bn_mck};
	;;
    019)
	bn_mck="${testname}_mck"
	make clean > /dev/null 2> /dev/null
	make CC=mpiicc ${bn_mck}
	;;
    *)
	bn_mck="${testname}_mck"
	make clean > /dev/null 2> /dev/null
	make ${bn_mck}
esac

pidof mcexec | xargs -r sudo kill -9 > /dev/null 2> /dev/null 
pidof $bn_lin | xargs -r sudo kill -9 > /dev/null 2> /dev/null 

case ${testname} in
    001)
	if [ $boot_shutdown -eq 1 ]; then
	    testopt="-b ${testopt}"
	fi
	if [ $mcexec_shutdown -eq 1 ]; then
	    testopt="-x ${testopt}"
	fi
	if [ $ikc_map_by_func -eq 1 ]; then
	    testopt="-m ${testopt}"
	fi
	;;
    003)
	bootopt="-m 256M -k 0 -i -1"
	;;
    004 | 005 | 018)
	bootopt="-k 1 -m 256M -i 2"
	;;
    002 | 006 | 007 | 008 | \
    009 | 010 | 011 | 012 | \
    013 | 014 | 015 | 016 | \
    017 | 019 | 020 | 021 | \
	022 | 023 | 024)
	;;
    *)
	echo Unknown test case
	exit 255
esac

if [ ${dryrun} == "y" ]; then
    exit
fi

case ${testname} in
    001 | 002 | 020 | 021 | 023 | 024)
	if ! sudo ${SBIN}/mcstop+release.sh 2>&1; then
	    exit 255
	fi
	;;
    003 | 004 | 005)
	if ! sudo ${SBIN}/mcstop+release.sh 2>&1; then
	    echo "mcstop+release.sh failed"
	    exit 255
	fi
	if ! grep /var/log/local6 /etc/rsyslog.conf &>/dev/null; then
	    echo "Insert a line of local6.* /var/log/local6 into /etc/rsyslog.conf"
	    exit 255
	fi
	sudo rm /var/log/local6
	sudo touch /var/log/local6
	sudo chmod 600 /var/log/local6
	sudo systemctl restart rsyslog
	if ! sudo ${SBIN}/mcreboot.sh ${bootopt} 2>&1; then
	    echo "mcreboot.sh failed"
	    exit 255
	fi
	;;
     006 | 007 | 008 | 009 | 010 | 011 | 013 | 014 | 015 | 016 | 017)
	if ! sudo ${SBIN}/mcstop+release.sh 2>&1; then
	    exit 255
	fi
	if ! grep /var/log/local5 /etc/rsyslog.conf &>/dev/null; then
	    echo "Insert a line of \"local5.* /var/log/local5\" into /etc/rsyslog.conf"
	    exit 255
	fi
	if ! grep /var/log/local6 /etc/rsyslog.conf &>/dev/null; then
	    echo "Insert a line of \"local6.* /var/log/local6\" into /etc/rsyslog.conf"
	    exit 255
	fi
	sudo rm /var/log/local5 /var/log/local6
	sudo touch /var/log/local5 /var/log/local6
	sudo chmod 600 /var/log/local5 /var/log/local6
	sudo systemctl restart rsyslog
	;;
    012)
	if ! sudo ${SBIN}/mcstop+release.sh 2>&1; then
	    exit 255
	fi
	;;
    019)
	;;
    022)
	;;
    *)
	if ! sudo ${SBIN}/mcstop+release.sh 2>&1; then
	    echo "mcstop+release.sh failed"
	    exit 255
	fi
	if ! sudo ${SBIN}/mcreboot.sh ${bootopt} 2>&1; then
	    echo "mcreboot.sh failed"
	    exit 255
	fi
	;;
esac

if [ ${kill} == "y" ]; then
    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
    sleep ${sleepopt}
    ${SBIN}/ihkosctl 0 kmsg > ./${testname}.log
    pidof mcexec | xargs -r sudo kill -9 > /dev/null 2> /dev/null
else
    case ${testname} in
	001)
	    sudo MYGROUPS=${groups} ./${bn_lin} ${testopt}
	    ret=$?
	;;
	020 | 021 | 023 | 024)
	    sudo MYGROUPS=${groups} ./${bn_lin}
	    ret=$?
	;;
	022)
	    # Show what is the issue
	    sudo ${SBIN}/mcstop+release.sh
	    sudo ${SBIN}/mcreboot.sh ${bootopt}
	    ${BIN}/mcexec cat /proc/1/cmdline > cmdline.mcexec
	    cat /proc/1/cmdline > cmdline.jobsched
	    if [ "`diff cmdline.mcexec cmdline.jobsched | wc -l`" == "0" ]; then
		printf "[OK] "
	    else
		printf "[NG] "
	    fi
	    printf "job sees the same /proc/1/cmdline as "
	    printf "job-scheduler when not using ihk_os_create_pseudofs()\n"
	    sudo ${SBIN}/mcstop+release.sh

	    # Replace /proc
	    sudo setsid unshare --fork --pid --mount-proc ./newns.sh \
		&> /dev/null &

	    while ! pgrep -n newns.sh &> /dev/null; do :; done
	    pid=$(pgrep -n newns.sh)
	    sid=$(ps -ho sid:1 --pid $pid)

	    # ihk_os_create_pseudofs with /proc/$pid/ns/{mnt,pid}
	    sudo MYGROUPS=${groups} ./${testname}-pro_lin $pid

	    # ihk_os_destroy_pseudofs with /proc/$pid/ns/{mnt,pid}
	    sudo ./${testname}-epi_lin $pid

	    sudo kill -9 -$sid;
	;;
	003)
	    sudo ./${bn_lin} ${testopt}
	    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt}
	    sleep 4
	    pid=`pidof ${bn_lin}`
	    if [ "${pid}" != "" ]; then
		sudo kill -9 ${pid} > /dev/null 2> /dev/null 
	    fi
	    if grep OK ./${testname}.tmp > /dev/null; then
		echo "[OK] ihk_os_get_eventfd,OOM"
		ret=0
	    else
		echo "[NG] ihk_os_get_eventfd,OOM"
		ret=1
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	004)
	    sudo ./${bn_lin} ${testopt}
	    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
	    sleep 4
	    pid=`pidof mcexec`
	    if [ "${pid}" != "" ]; then
		sudo kill -9 ${pid} > /dev/null 2> /dev/null
	    fi
	    pid=`pidof ${bn_lin}`
	    if [ "${pid}" != "" ]; then
		sudo kill -9 ${pid} > /dev/null 2> /dev/null 
	    fi
	    if grep OK ./${testname}.tmp > /dev/null; then
		echo "[OK] ihk_os_get_eventfd,PANIC"
		ret=0
	    else
		echo "[NG] ihk_os_get_eventfd,PANIC"
		ret=1
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	005)
	    sudo ./${bn_lin} ${testopt}
	    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
	    sleep 6
	    pidof mcexec | xargs -r sudo kill -9 > /dev/null 2> /dev/null
	    pidof $bn_lin | xargs -r sudo kill -9 > /dev/null 2> /dev/null 
	    if grep OK ./${testname}.tmp > /dev/null; then
		echo "[OK] ihk_os_get_eventfd,HUNGUP"
		ret=0
	    else
		echo "[NG] ihk_os_get_eventfd,HUNGUP"
		ret=1
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	018)
	    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt} 2> ./${testname}.tmp &
	    sleep 6
	    pidof mcexec | xargs -r sudo kill -9  > /dev/null 2> /dev/null
	    if grep -E 'hang' ./${testname}.tmp > /dev/null; then
		echo "[OK] hang detection"
		ret=0
	    else
		echo "[NG] hang detection"
		ret=1
	    fi
	;;
	002 | 006 | 007 | 008 | 009 | 010 | 011 | 012 | 013 | 014 | 015 | \
	    016 | 017)
	    sudo ./${bn_lin} ${testopt}
	;;
	019)
	    ./${testname}.sh
	    ret=$?
	    ;;
	*)
	    ${BIN}/mcexec ${mcexecopt} ./${bn_mck} ${testopt}
	    ret=$?
	    ${SBIN}/ihkosctl 0 kmsg > ./${testname}.log
    esac
fi

case ${testname} in
    001 | 020 | 021 | 023 | 024)
	;;
    003)
	;;
    002 | 006 | 007 | 008 | 009 | 010 | 011 | 012 | 013 | 014 | 015 | 016 | \
	017 | 019)
	;;
    022)
	;;
    *)
	sudo ${SBIN}/mcstop+release.sh
	;;
esac

case ${testname} in
    002)
	printf "*** Check if the correct dumpfile is created by objdump -x and eclair\n"
	;;
    014 | 015)
	printf "*** See Linux kmsg to check if the kmsg buffer is acquired/released correctly.\n"
	;;
    016)
	printf "*** See Linux kmsg to check if the first kmsg buffer is deleted when the third one is created and the remaining two kmsg buffers are deleted when /dev/mcd0 is destroyed.\n"
	;;
    017)
	printf "*** See Linux stdout to check if panic is detected and ihkmond successfully relays kmsg of the next OS instance.\n"
	;;
    *)
	if [ "$ret" == "0" ]; then
	    printf "*** Summary: All tests are OK\n"
	else
	    printf "*** Summary: Some tests are NG\n"
	fi
	;;
esac
