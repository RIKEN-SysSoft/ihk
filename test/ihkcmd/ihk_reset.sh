#!/bin/sh

. ./config.sh

sudo ${SBIN_DIR}/mcstop+release.sh
sudo insmod ${KMOD_DIR}/ihk.ko
sudo insmod ${KMOD_DIR}/ihk-smp-x86_64.ko ihk_start_irq=106 ihk_ikc_irq_core=0
#sudo insmod ${KMOD_DIR}/ihk-smp-x86.ko ihk_start_irq=106 ihk_ikc_irq_core=0

sudo ${SBIN_DIR}/ihkconfig 0 create
