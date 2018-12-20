#!/bin/sh

. ${HOME}/.mck_test_config

. ./ihkcmd_util.sh

prepare

echo "*** Test for ihkosctl CPU"
chk_ihkconfig 0 "" reserve cpu 1-7
chk_ihkconfig 0 "^1-7$" query cpu

chk_ihkosctl 0 "" assign cpu 2-4,6
chk_ihkosctl 0 "^2-4,6$" query cpu
chk_ihkconfig 0 "^1,5,7$" query cpu

chk_ihkosctl 0 "" release cpu 2,6
chk_ihkosctl 0 "^3-4$" query cpu
chk_ihkconfig 0 "^1-2,5-7$" query cpu

chk_ihkosctl 0 "" release cpu 3-4
chk_ihkosctl 0 "" query cpu
chk_ihkconfig 0 "" release cpu 1-7

chk_ihkosctl not0 "Usage.*" assign cpu
chk_ihkosctl not0 "Usage.*" release cpu

echo "*** Test for ihkosctl MEM"
chk_ihkconfig 0 "" reserve mem 512M@0,512M@1
chk_ihkosctl 0 "" assign mem 256M@0,256M@1
chk_ihkosctl 0 "^268435456@0,268435456@1$" query mem
chk_ihkconfig 0 "^268435456@0,268435456@1$" query mem

chk_ihkosctl 0 "" release mem 256M@1
chk_ihkosctl 0 "^268435456@0" query mem

chk_ihkosctl 0 "" release mem 256M@0
chk_ihkosctl 0 "" query mem

chk_ihkosctl 0 "" assign mem 256M@0,256M@1
chk_ihkosctl 0 "" release mem all
chk_ihkosctl 0 "" query mem

chk_ihkosctl not0 "Usage.*" assign mem
chk_ihkosctl not0 "Usage.*" release mem

echo "*** Test for ihkosctl ikc_map"
chk_ihkconfig 0 "" reserve cpu 1-3,5-7
chk_ihkosctl 0 "" assign cpu 1-3,5-7
chk_ihkosctl 0 "^1-3,5-7$" query cpu

chk_ihkosctl 0 "" set ikc_map 5-7:4+1-3:0
chk_ihkosctl 0 "1,2,3:0+5,6,7:4" get ikc_map

chk_ihkosctl 0 "" release cpu 1-3,5-7
chk_ihkconfig 0 "" release cpu 1-3,5-7
