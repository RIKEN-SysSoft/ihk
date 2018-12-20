#!/bin/sh

. ${HOME}/.mck_test_config

tid=001
echo "*** CT{$tid} start *************************************"
orig_dir=`pwd`

cd ../../ihklib

./run.sh -m 001 | tee ${orig_dir}/CT${tid}.log

cd ${orig_dir}

ok=`grep "^\[ OK \]" ./CT${tid}.log | wc -l`
ng=`grep "^\[ NG \]" ./CT${tid}.log | wc -l`

if [ $ng = 0 ]; then
	echo "*** CT${tid}: PASSED (ok:$ok, ng:$ng)"
else
	echo "*** CT${tid}: FAILED (ok:$ok, ng:$ng)"
fi

tid=002
echo "*** CT${tid} start *************************************"
sh ./CT${tid}.sh | tee ./CT${tid}.log
ok=`grep "^\[ OK \]" ./CT${tid}.log | wc -l`
ng=`grep "^\[ NG \]" ./CT${tid}.log | wc -l`

if [ $ng = 0 ]; then
	echo "*** CT${tid}: PASSED (ok:$ok, ng:$ng)"
else
	echo "*** CT${tid}: FAILED (ok:$ok, ng:$ng)"
fi

tid=003
echo "*** CT${tid} start *************************************"
sh ./CT${tid}.sh | tee ./CT${tid}.log
ok=`grep "^\[ OK \]" ./CT${tid}.log | wc -l`
ng=`grep "^\[ NG \]" ./CT${tid}.log | wc -l`

if [ $ng = 0 ]; then
	echo "*** CT${tid}: PASSED (ok:$ok, ng:$ng)"
else
	echo "*** CT${tid}: FAILED (ok:$ok, ng:$ng)"
fi

