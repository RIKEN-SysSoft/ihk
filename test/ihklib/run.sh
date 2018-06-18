#!/usr/bin/bash

# Modify this line
install=${HOME}/project/os/install

testname=$1
bootopt="-m 256M"
mcexecopt=""
testopt=""
kill="n"
dryrun="n"
sleepopt="0.4"
home=$(eval echo \$\{HOME\})
groups=`groups | cut -d' ' -f 1`

echo Executing ${testname}

case ${testname} in
    ihklib004 | ihklib005 | ihklib006 | ihklib007 | ihklib008 | ihklib009 | ihklib010 | ihklib012 | ihklib014 | ihklib018)
	printf "*** Apply ${testname}.patch with -p 1 to enable syscall #900 and recompile IHK/McKernel.\n"
	;;
    ihklib011)
	printf "*** Apply ${testname}.patch with -p 1 to set kmsg buffer size to 256 and enable syscall #900 and recompile IHK/McKernel.\n"
	;;
    ihklib013)
	printf "*** Apply ${testname}.patch with -p 1 to set the size of the kmsg memory-buffer o 256 and enable syscall #900 and set the width of the kmsg file-buffer to 64 and its depth to 4 and then recompile IHK/McKernel.\n"
	;;
    ihklib014 | ihklib015)
	printf "*** Apply ${testname}.patch with -p 1 to enable syscall #900 and recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    ihklib016)
	printf "*** Apply ${testname}.patch with -p 1 to enable syscall #900 and make ihkmond not release kmsg_buf and the number of stray kmsg_buf allowed in host_driver.c to 2 and then recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    ihklib017)
	printf "*** Apply ${testname}.patch with -p 1 to enable syscall #900 and then recompile IHK/McKernel.\n"
	printf "*** Let host_driver.c outputs debug messages by defining DEBUG_IKC.\n"
	;;
    ihklib019)
	printf "*** Apply ${testname}.patch with -p 1 to enable syscall #900 and recompile IHK/McKernel.\n"
	printf "*** Modify values of mck_dir, lastnode, nnodes, ssh, pjsub in ${testname}.sh.\n"
	;;
esac

case ${testname} in
    ihklib001 | ihklib020 | ihklib021)
	;;
    *)
	read -p "*** Hit return when ready!" key
	;;
esac

case ${testname} in
    ihklib001 | ihklib020 | ihklib021)
	bn_lin="${testname}_lin"
	make clean > /dev/null 2> /dev/null
	make ${bn_lin}
	;;
     ihklib002 | ihklib003 | ihklib004 | ihklib005 | ihklib006 | ihklib007 | ihklib008 | ihklib009 | ihklib010 | ihklib011 | ihklib012 | ihklib013 | ihklib014 | ihklib015 | ihklib016 | ihklib017)
	bn_lin="${testname}_lin"
	bn_mck="${testname}_mck"
	make clean > /dev/null 2> /dev/null
	make ${bn_lin} ${bn_mck};
	;;
    ihklib019)
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
    ihklib001)
	testopt="0"
	;;
    ihklib003)
	bootopt="-m 256M -k 0 -i -1"
	;;
    ihklib004 | ihklib005 | ihklib018)
	bootopt="-k 1 -m 256M -i 2"
	;;
    ihklib002 | ihklib006 | ihklib007 | ihklib008 | \
    ihklib009 | ihklib010 | ihklib011 | ihklib012 | \
    ihklib013 | ihklib014 | ihklib015 | ihklib016 | \
    ihklib017 | ihklib019 | ihklib020 | ihklib021)
	;;
    *)
	echo Unknown test case 
	exit 255
esac

if [ ${dryrun} == "y" ]; then
    exit
fi

case ${testname} in
    ihklib001 | ihklib002 | ihklib020 | ihklib021)
	if ! sudo ${install}/sbin/mcstop+release.sh 2>&1; then 
	    exit 255
	fi
	;;
    ihklib003 | ihklib004 | ihklib005)
	if ! sudo ${install}/sbin/mcstop+release.sh 2>&1; then 
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
	if ! sudo ${install}/sbin/mcreboot.sh ${bootopt} 2>&1; then 
	    echo "mcreboot.sh failed"
	    exit 255
	fi
	;;
     ihklib006 | ihklib007 | ihklib008 | ihklib009 | ihklib010 | ihklib011 | ihklib013 | ihklib014 | ihklib015 | ihklib016 | ihklib017)
	if ! sudo ${install}/sbin/mcstop+release.sh 2>&1; then 
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
    ihklib012)
	if ! sudo ${install}/sbin/mcstop+release.sh 2>&1; then 
	    exit 255
	fi
	;;
    ihklib019)
	;;
    *)
	if ! sudo ${install}/sbin/mcstop+release.sh 2>&1; then 
	    echo "mcstop+release.sh failed"
	    exit 255
	fi
	if ! sudo ${install}/sbin/mcreboot.sh ${bootopt} 2>&1; then 
	    echo "mcreboot.sh failed"
	    exit 255
	fi
	;;
esac

if [ ${kill} == "y" ]; then
    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
    sleep ${sleepopt}
    ${install}/sbin/ihkosctl 0 kmsg > ./${testname}.log
    pidof mcexec | xargs -r sudo kill -9 > /dev/null 2> /dev/null
else
    case ${testname} in
	ihklib001)
	    sudo GROUPS=${groups} HOME=${home} ./${bn_lin} ${testopt}
	;;
	ihklib020 | ihklib021)
	    sudo GROUPS=${groups} HOME=${home} ./${bn_lin}
	;;
	ihklib003)
	    sudo ./${bn_lin} ${testopt}
	    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt}
	    sleep 4
	    pid=`pidof ${bn_lin}`
	    if [ "${pid}" != "" ]; then
		sudo kill -9 ${pid} > /dev/null 2> /dev/null 
	    fi
	    if grep OK ./${testname}.tmp > /dev/null; then
		echo "[OK] ihk_os_get_eventfd,OOM"
	    else
		echo "[NG] ihk_os_get_eventfd,OOM"
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	ihklib004)
	    sudo ./${bn_lin} ${testopt}
	    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
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
	    else
		echo "[NG] ihk_os_get_eventfd,PANIC"
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	ihklib005)
	    sudo ./${bn_lin} ${testopt}
	    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt} &
	    sleep 6
	    pidof mcexec | xargs -r sudo kill -9 > /dev/null 2> /dev/null
	    pidof $bn_lin | xargs -r sudo kill -9 > /dev/null 2> /dev/null 
	    if grep OK ./${testname}.tmp > /dev/null; then
		echo "[OK] ihk_os_get_eventfd,HUNGUP"
	    else
		echo "[NG] ihk_os_get_eventfd,HUNGUP"
	    fi
	    sudo cat /var/log/local6 > ./${testname}.log
	;;
	ihklib018)
	    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt} 2> ./${testname}.tmp &
	    sleep 6
	    pidof mcexec | xargs -r sudo kill -9  > /dev/null 2> /dev/null
	    if grep -E 'hang' ./${testname}.tmp > /dev/null; then
		echo "[OK] hang detection"
	    else
		echo "[NG] hang detection"
	    fi
	    echo "All tests finished"
	;;
	ihklib002 | ihklib006 | ihklib007 | ihklib008 | ihklib009 | ihklib010 | ihklib011 | ihklib012 | ihklib013 | ihklib014 | ihklib015 | ihklib016 | ihklib017)
	    sudo ./${bn_lin} ${testopt}
	;;
	ihklib019)
	    ./${testname}.sh
	    ;;
	ihklib020 | ihklib021)
	    sudo ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt}
	    ${install}/sbin/ihkosctl 0 kmsg > ./${testname}.log
	    ;;
	*)
	    ${install}/bin/mcexec ${mcexecopt} ./${bn_mck} ${testopt}
	    ${install}/sbin/ihkosctl 0 kmsg > ./${testname}.log
    esac
fi

case ${testname} in
    ihklib001 | ihklib020 | ihklib021)
	;;
    ihklib003)
	;;
    ihklib002 | ihklib006 | ihklib007 | ihklib008 | ihklib009 | ihklib010 | ihklib011 | ihklib012 | ihklib013 | ihklib014 | ihklib015 | ihklib016 | ihklib017 | ihklib019)
	;;
    *)
	sudo ${install}/sbin/mcstop+release.sh
	;;
esac

case ${testname} in
    ihklib002)
	printf "*** Check the dumpfile is correct by objdump -x and eclair\n"
	;;
    ihklib014 | ihklib015)
	printf "*** See Linux kmsg to the kmsg buffer is acquired/released correctly.\n"
	;;
    ihklib016)
	printf "*** See Linux kmsg to the first kmsg buffer is deleted when the third one is created and the remaining two kmsg buffers are deleted when /dev/mcd0 is destroyed.\n"
	;;
    ihklib017)
	printf "*** See Linux stdout to confirm panic is detected and ihkmond successfully relay kmsg of the next OS instance.\n"
	;;
    *)
	printf "*** It behaves as expected when there's no [NG] and \"All tests finished\" is shown\n"
	;;
esac
