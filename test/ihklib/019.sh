#!/bin/sh -x

# Modify these lines
mck_dir=/work/gg10/e29005/tmp/install
lastnode=8200
nnodes=2
ppn=64
nproc=$((ppn * nnodes))
ssh=0
pjsub=1
nloop=2

exe=ihklib019_mck

while getopts s:p:n: OPT
do
        case ${OPT} in
	    s) ssh=$OPTARG
		;;
	    p) pjsub=$OPTARG
		;;
	    n) nloop=$OPTARG
		;;
            *) echo "ERROR: Invalid option -${OPT}" >&2
                exit 1
        esac
done

mcexec="${mck_dir}/bin/mcexec"
mcexec_param="-t 4 -n $ppn --stack-premap=32M,32G -m 1"

if [ $ssh -eq 1 ]; then

    nodes=`echo $(seq -s ",c" $(($lastnode + 1 - $nnodes)) $lastnode) | sed 's/^/c/'`
    
    pdsh -S -t 2 -w $nodes hostname
    rc=$?
    if [ $rc -ne 0 ]; then
	echo pdsh failed. Check ssh-agent connection.
	exit
    fi

    PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -t 2 -w $nodes bash -c \'if \[ \"\`cat /etc/mtab \| while read line\; do cut -d\" \" -f 2\; done \| grep /work\`\" == \"\" \]\; then sudo mount /work\; fi\'

    for((count=0;count<nloop;count++)); do
	echo start $count $(date)

	TESTDIR=test.$nodes
	mkdir $TESTDIR
	cd $TESTDIR


	PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -S -t 2 -w $nodes sudo ${mck_dir}/sbin/mcstop+release.sh
	rc=$?
	if [ $rc -ne 0 ]; then
	    echo ERROR: mcstop+release.sh failed rc: $rc
	    exit 1
	fi
	
	PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -S -t 2 -w $nodes sudo ${mck_dir}/sbin/mcreboot.sh -i 4 -c 2-17,70-85,138-153,206-221,20-35,88-103,156-171,224-239,36-51,104-119,172-187,240-255,52-67,120-135,188-203,256-271 -r 2-5,70-73,138-141,206-209:0+6-9,74-77,142-145,210-213:1+10-13,78-81,146-149,214-217:68+14-17,82-85,150-153,218-221:69+20-23,88-91,156-159,224-227:136+24-27,92-95,160-163,228-231:137+28-31,96-99,164-167,232-235:204+32-35,100-103,168-171,236-239:205+36-39,104-107,172-175,240-243:18+40-43,108-111,176-179,244-247:19+44-47,112-115,180-183,248-251:86+48-51,116-119,184-187,252-255:87+52-55,120-123,188-191,256-259:154+56-59,124-127,192-195,260-263:155+60-63,128-131,196-199,264-267:222+64-67,132-135,200-203,268-271:223 -m 32G@0,12G@1
	
	rc=$?
	if [ $rc -ne 0 ]; then
	    echo ERROR: mcreboot.sh failed rc: $rc
	    exit 1
	fi

	PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -t 2 -w $nodes ulimit -u 16384; 
	PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -t 2 -w $nodes ulimit -s unlimited

	export I_MPI_HYDRA_BOOTSTRAP_EXEC=ssh
	export HYDRA_PROXY_RETRY_COUNT=30
	
	#export OMP_STACKSIZE=64M
	export KMP_BLOCKTIME=1
	export PSM2_RCVTHREAD=0
	
	export I_MPI_PIN=off
	export KMP_AFFINITY=disabled
	
	export HFI_NO_CPUAFFINITY=1
	export I_MPI_COLL_INTRANODE_SHM_THRESHOLD=4194304
	export I_MPI_FABRICS=shm:tmi
	export PSM2_RCVTHREAD=0
	export I_MPI_TMI_PROVIDER=psm2
	export I_MPI_FALLBACK=0
	export PSM2_MQ_RNDV_HFI_WINDOW=4194304
	export PSM2_MQ_EAGER_SDMA_SZ=65536
	export PSM2_MQ_RNDV_HFI_THRESH=200000
	
	export MCKERNEL_RLIMIT_STACK=32M,16G
	export KMP_STACKSIZE=64m
	
	export I_MPI_WAIT_MODE=on

	mpiexec -l -n $nproc -ppn $ppn -host $nodes $mcexec $mcexec_param ../$exe 2> ./log.txt&
	sleep $((nnodes * 7))

	cat ./log.txt
	if grep -E 'hang' ./log.txt > /dev/null; then
	    echo "[OK] hang detection"
	    ret=0
	else
	    echo "[NG] hang detection"
	    ret=1
	fi

	PDSH_SSH_ARGS_APPEND="-tt -q" pdsh -S -t 2 -w $nodes /usr/sbin/pidof mcexec \| xargs -r sudo kill -9

	cd ..
	rm -rf $TESTDIR
	echo end $count $(date)
    done
    exit ret
fi

if [ $pjsub -eq 1 ]; then
    for((count=0;count<nloop;count++)); do

	TESTDIR=test.$count
	mkdir $TESTDIR
	cd $TESTDIR

	sec=$((60 + nnodes * 7 + 20)) # mcreboot.sh + mpiexec + watchdog-timer
	elapse=`printf '%02d:%02d:%02d' $((sec / 3600)) $((sec % 3600 / 60)) $((sec % 60))`

	(
	    cat <<EOF
#!/bin/sh

#PJM -L rscgrp=MCK-FLAT-QUADRANT
#PJM -L node=$nnodes
#PJM --mpi proc=$nproc
#PJM -L elapse=$elapse
#PJM -L proc-crproc=16384 
#PJM -g gg10
#PJM -j
#PJM -s
#PJM -x MCK=$mck_dir
#PJM -x MCK_MEM=32G@0,8G@1

export HYDRA_PROXY_RETRY_COUNT=30

#export OMP_STACKSIZE=64M
export KMP_BLOCKTIME=1
export PSM2_RCVTHREAD=0

export I_MPI_PIN=off
export KMP_AFFINITY=disabled

export HFI_NO_CPUAFFINITY=1
export I_MPI_COLL_INTRANODE_SHM_THRESHOLD=4194304
export I_MPI_FABRICS=shm:tmi
export PSM2_RCVTHREAD=0
export I_MPI_TMI_PROVIDER=psm2
export I_MPI_FALLBACK=0
export PSM2_MQ_RNDV_HFI_WINDOW=4194304
export PSM2_MQ_EAGER_SDMA_SZ=65536
export PSM2_MQ_RNDV_HFI_THRESH=200000

export MCKERNEL_RLIMIT_STACK=32M,16G
export KMP_STACKSIZE=64m

export I_MPI_WAIT_MODE=on

mpiexec -l -n $nproc -ppn $ppn $mcexec $mcexec_param ../$exe

EOF
	) > ./job.sh
	
	PJM_MCK_AVAILABLE=1 pjsub --norestart ./job.sh
        jobid=$(pjstat |tail -1 | cut -f 1 -d " ")

        echo Trial No. $count jobid=$jobid started at $(date)

        pjwait $jobid

	cat ./job.sh.o$jobid
	if grep -E 'hang' ./job.sh.o$jobid > /dev/null; then
	    echo "[OK] hang detection"
	    ret=0
	else
	    echo "[NG] hang detection"
	    ret=1
	fi

        echo Trial No. $count jobid=$jobid ended at $(date)

	cd ..
	rm -rf $TESTDIR
    done
    exit ret
fi
