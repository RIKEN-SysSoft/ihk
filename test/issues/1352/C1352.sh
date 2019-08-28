#/bin/sh

USELTP=1
USEOSTEST=0

. ../../../../mckernel/test/common.sh

issue=1352
tid=01

for tp in futex_wait01 futex_wait02 futex_wait03 futex_wait04 futex_wait05 \
	futex_wait_bitset01 futex_wait_bitset02 \
	futex_wake01 futex_wake02 futex_wake03
do
	tname=`printf "C${issue}T%02d" ${tid}`
	echo "*** ${tname} start *******************************"
	$MCEXEC $LTPBIN/$tp 2>&1 | tee $tp.txt
	ok=`grep TPASS $tp.txt | wc -l`
	ng=`grep TFAIL $tp.txt | wc -l`
	if [ $ng = 0 ]; then
		echo "*** ${tname} PASSED ($ok)"
	else
		echo "*** ${tname} FAILED (ok=$ok ng=%ng)"
	fi
	let tid++
	echo ""
done

