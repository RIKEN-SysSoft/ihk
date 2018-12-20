#!/bin/sh

. ${HOME}/.mck_test_config

. ./ihkcmd_util.sh

prepare

echo "*** Test for ihkconfig CPU"
chk_ihkconfig 0 "" query cpu
chk_ihkconfig 0 "" reserve cpu 5-7,1,3
chk_ihkconfig 0 "^1,3,5-7$" query cpu

chk_ihkconfig 0 "" release cpu 5-6
chk_ihkconfig 0 "^1,3,7$" query cpu

chk_ihkconfig 0 "" release cpu 1,3,7
chk_ihkconfig 0 "" query cpu

chk_ihkconfig not0 "Usage.*" reserve cpu
chk_ihkconfig not0 "Usage.*" release cpu

echo "*** Test for ihkconfig MEM"
chk_ihkconfig 0 "" query mem
chk_ihkconfig 0 "" reserve mem 256M@0,256M@1
chk_ihkconfig 0 "^268435456@0,268435456@1$" query mem

chk_ihkconfig 0 "" release mem 256M@1
chk_ihkconfig 0 "^268435456@0" query mem

chk_ihkconfig 0 "" release mem 256M@0
chk_ihkconfig 0 "" query mem

chk_ihkconfig 0 "" reserve mem 256M@0,256M@1
chk_ihkconfig 0 "^268435456@0,268435456@1$" query mem

chk_ihkconfig 0 "" release mem all
chk_ihkconfig 0 "" query mem

chk_ihkconfig not0 "Usage.*" reserve mem
chk_ihkconfig not0 "Usage.*" release mem
