#!/bin/sh

cnt=0

function prepare() {
	./ihk_reset.sh
	(( cnt++ ))
}

# 設定値読み込み
. ./config.sh
conf="./test_ihkconfig.sh"
osct="./test_ihkosctl.sh"

#<< COMMENT
echo "【Test for ihkosctl assign mem】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${osct} ${cnt} 0 "" assign mem 0@0
${osct} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${osct} ${cnt} 0 "" assign mem 512M@0
${osct} ${cnt} 0 "536870912@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${osct} ${cnt} 0 "" assign mem 256M@0
${osct} ${cnt} 0 "268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${osct} ${cnt} 0 "" assign mem 128M@0,256M@0,128M@0
${osct} ${cnt} 0 "134217728@0,268435456@0,134217728@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${osct} ${cnt} not0 "error" assign mem 1024M@0
${osct} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${osct} ${cnt} 0 "" assign mem 0@0
${osct} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${osct} ${cnt} 0 "" assign mem 512M@0
${osct} ${cnt} 0 "268435456@0,268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${osct} ${cnt} 0 "" assign mem 256M@0
${osct} ${cnt} 0 "268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${osct} ${cnt} 0 "" assign mem 128M@0,256M@0,128M@0
${osct} ${cnt} 0 "134217728@0,134217728@0,268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${osct} ${cnt} not0 "error" assign mem 1024M@0
${osct} ${cnt} 0 "" query mem


echo "【Test for ihkosctl release mem】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 0@0
${osct} ${cnt} not0 "error" release mem 0@0
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
${osct} ${cnt} 0 "" release mem 512M@0
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${osct} ${cnt} 0 "" release mem 256M@0
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "268435456@0,268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 0@0
${osct} ${cnt} not0 "error" release mem 0@0
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "268435456@0,268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@1
${osct} ${cnt} 0 "" release mem 256M@0
${osct} ${cnt} 0 "" release mem 256M@1
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "268435456@0,268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${osct} ${cnt} 0 "" release mem 256M@0
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "268435456@0,268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@1
${osct} ${cnt} 0 "" release mem 256M@1
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "268435456@0,268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 128M@0,128M@1
${osct} ${cnt} 0 "" release mem 128M@0
${osct} ${cnt} 0 "" release mem 128M@1
${osct} ${cnt} 0 "" query mem
${conf} ${cnt} 0 "134217728@0,134217728@0,134217728@1,134217728@1" query mem

echo "【Test for ihkosctl assign cpu】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" assign cpu 25
${osct} ${cnt} 0 "25" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" assign cpu 20-29
${osct} ${cnt} 0 "20,21,22,23,24,25,26,27,28,29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" assign cpu 20,22,23-27
${osct} ${cnt} 0 "20,22,23,24,25,26,27" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} not0 "Usage" assign cpu
${osct} ${cnt} 0 "" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} not0 "error" assign cpu abc
${osct} ${cnt} 0 "" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} not0 "error" assign cpu 19
${osct} ${cnt} 0 "" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" assign cpu 20
${osct} ${cnt} 0 "20" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" assign cpu 29
${osct} ${cnt} 0 "29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} not0 "error" assign cpu 30
${osct} ${cnt} 0 "" query cpu

echo "【Test for ihkosctl query cpu】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${osct} ${cnt} 0 "" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 25
${osct} ${cnt} 0 "25" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 20-29
${osct} ${cnt} 0 "20,21,22,23,24,25,26,27,28,29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 20,22,23-27
${osct} ${cnt} 0 "20,22,23,24,25,26,27" query cpu

