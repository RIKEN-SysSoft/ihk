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
# ihkconfg_1
echo "【Test for ihkconfig reserve mem】"
###
prepare
${conf} ${cnt} 0 "" reserve mem 536870912@0
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 524288K@0
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 512M@0
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 512M@1
${conf} ${cnt} 0 "536870912@1" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 1G@0
${conf} ${cnt} 0 "1073741824@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 512M
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 512M@
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 1Z
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 0.5G
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 1024G
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "Usage" reserve mem ""
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 511M@0
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 511M@A
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 536870911@0
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 536870913@0
${conf} ${cnt} 0 "" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 128M@0,128M@0
${conf} ${cnt} 0 "134217728@0,134217728@0" query mem

###
prepare
${conf} ${cnt} 0 "" reserve mem 128M@0,128M@1
${conf} ${cnt} 0 "134217728@0,134217728@1" query mem

###
prepare
${conf} ${cnt} not0 "error" reserve mem 128M@0,128M@1,127M@0
${conf} ${cnt} 0 "" query mem

# ihkconfig_2
echo "【Test for ihkconfig release mem】"
###
prepare
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${conf} ${cnt} 0 "" release mem 512M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${conf} ${cnt} 0 "" release mem 256M@0,256M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
${conf} ${cnt} 0 "" release mem 256M@0,256M@1
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${conf} ${cnt} not0 "error" release mem 256M@0
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${conf} ${cnt} not0 "error" release mem 512M@0
${conf} ${cnt} 0 "268435456@0,268435456@0" query mem


# ihkconfig_3
echo "【Test for ihkconfig release mem (after assign)】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 512M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0,256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem  256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0,256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0,256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 release mem 256M@0,256M@1
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@1
${conf} ${cnt} 0 "" release mem all
${conf} ${cnt} 0 "" query mem 

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 release mem 512M@0
${conf} ${cnt} not0 "Usage" release mem ""
${conf} ${cnt} 0 "536870912@0" query mem 

echo "【Test for ihkconfig query mem】"
###
prepare
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
${conf} ${cnt} 0 "536870912@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 512M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${conf} ${cnt} 0 "268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
${conf} ${cnt} 0 "268435456@0,268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 512M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@0
${conf} ${cnt} 0 "" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@0
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${conf} ${cnt} 0 "268435456@0" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
${conf} ${cnt} 0 "268435456@0,268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0
${conf} ${cnt} 0 "268435456@1" query mem

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@1
${conf} ${cnt} 0 "268435456@0" query mem
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve mem 256M@0,256M@1
sudo ${SBIN_DIR}/ihkosctl 0 assign mem 256M@0,256M@1
${conf} ${cnt} 0 "" query mem

echo "【Test for ihkconfig reserve cpu】"
###
prepare
${conf} ${cnt} 0 "" reserve cpu 20
${conf} ${cnt} 0 "20" query cpu

###
prepare
${conf} ${cnt} 0 "" reserve cpu 20-29
${conf} ${cnt} 0 "20-29" query cpu

###
prepare
${conf} ${cnt} 0 "" reserve cpu 20,22,23-27
${conf} ${cnt} 0 "20,22-27" query cpu

###
prepare
${conf} ${cnt} not0 "Usage" reserve cpu 
${conf} ${cnt} 0 "" query cpu

###
prepare
${conf} ${cnt} not0 "error" reserve cpu abc
${conf} ${cnt} 0 "" query cpu

###
prepare
${conf} ${cnt} not0 "error" reserve cpu -1
${conf} ${cnt} 0 "" query cpu

###
prepare
${conf} ${cnt} 0 "" reserve cpu 1
${conf} ${cnt} 0 "1" query cpu

###
prepare
${conf} ${cnt} 0 "" reserve cpu 31
${conf} ${cnt} 0 "31" query cpu

###
prepare
${conf} ${cnt} not0 "error" reserve cpu 32
${conf} ${cnt} 0 "" query cpu

echo "【Test for ihkconfig query cpu】"
###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
${conf} ${cnt} 0 "20-29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 20-29
${conf} ${cnt} 0 "" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 20-23
${conf} ${cnt} 0 "24-29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 22-25
${conf} ${cnt} 0 "20-21,26-29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 25-28
${conf} ${cnt} 0 "20-24,29" query cpu

###
prepare
sudo ${SBIN_DIR}/ihkconfig 0 reserve cpu 20-29
sudo ${SBIN_DIR}/ihkosctl 0 assign cpu 21,23,25,27
${conf} ${cnt} 0 "20,22,24,26,28-29" query cpu

