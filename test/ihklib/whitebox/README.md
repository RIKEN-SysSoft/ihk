Please follow steps below to compile a patched version of McKernel and install test apps:

  - Compiling McKernel (patched):
    $ cd $MCKERNEL_SRC_DIR/ihk/test/ihklib/whitebox
    $ mkdir build
    $ cd build

    Assume that we will install patched McKernel to $HOME/whitebox_mck, and test apps to $HOME/whitebox_testapp

    $ cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/whitebox_testapp -DMCK_INSTALL_PREFIX=$HOME/whitebox_mck -DWITH_MCK_SRC=$MCKERNEL_SRC_DIR

    After this command, a patched McKernel will be installed to $HOME/whitebox_mck

  - Installing test apps:
    $ make -j install

Checking installation apps:
  $ cd $HOME/whitebox_testapp/bin
  $ sudo dmesg -C
  $ sudo ./test__ihk_device_create_os
  $ dmesg | grep " IHK "

