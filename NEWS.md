===========================================
What's new in V1.7.0rc4 (Apr 15, 2020)
===========================================

------------------
IHK major updadtes
------------------
1. Switch build system to cmake
2. arm64 port: Direct access to Mckernel memory from Linux

-------------------
IHK major bug fixes
-------------------
1.  ihk_os_get_status() SHUTDOWN detect fix.
2.  smp_ihk_os_set_ikc_map: do not access user memory directly
3.  perf_event: update struct x86_pmu for newer kernels
4.  smp-driver: add handling for 5th level of page table
5.  ihk_ikc_recv: Record channel to packet for release
6.  smp request_irq: fix refcount handling's fi
7.  configure: Fix BUILDID (again)
8.  ihklib: Add function to query total memory
9.  Fix the possibility that detect_hungup()'s ihklib_os_open() will fail.
10. set_ikc_map: check if requested mapping is OK for arch
11. smp_ihk_os_dump: fix user direct access
12. ihk_ikc_linux_schedule_work: schedule ikc work on same cpu
13. ihk smp: dcache flush trampoline/startup and elf for arm64
14. build system: switch to cmake
15. Pass boot_tsc to cokernel.
16. ihk smp cpu/mem desc: fix user accesses
17. ihk_mc_get_vector(): fix handling of TLB invalidation vectors
18. IKC: use Linux work IRQ for IKC interrupt
19. Use ASM cmpxchg() instead of compiler atomic intrinsics
20. __ihk_smp_reserve_mem: Make min chunk size and timeout duration configurable

===========================================
What's new in V1.6.0 (Nov 11, 2018)
===========================================

---------------------------------------------
IHK new features and improvements and changes
---------------------------------------------
1. McKernel and Linux share one unified kernel virtual address space.
   That is, McKernel sections resides in Linux sections spared for
   modules.  In this way, Linux can access the McKernel kernel memory
   area.

----------------------
IHK bug fixes (digest)
----------------------
1. #1142 __ihk_smp_reserve_mem: Check if requested NUMA node is online
2. #1024 Fix to VMAP virtual address leak.
3. ihk_ikc_interrupt_handler: fix null dereferences
4. warm_reset: fix uninitialized access
5. ihk-smp: lookup unexported symbols at runtime
6. arm64: fix gic_chip_data struct for rhel kernel
7. arm64/kallsyms: use kallsym for gic_data_v3 as well
8. IKC: fix condition to avoid race between write/read when queue is full
9. return -EFAULT if ihk_get_request_os_cpu() ihk_os is NULL.

===========================================
What's new in V1.5.1 (July 9, 2018)
===========================================

---------------------------------------------
IHK new features and improvements and changes
---------------------------------------------
1. IHK-SMP: support allocating all free memory from a specific NUMA
   node (e.g., -m all@1)

----------------------
IHK bug fixes (digest)
----------------------
#<num> indicates the issue number in the McKernel redmine
(https://postpeta.pccluster.org/redmine/).

1. #1147: __ihk_smp_reserve_mem(): limit "all" to 98% to avoid Linux crashing
2. #1148: IHK-SMP: adjust page order_limit in reservation based on requested
                   size
3. #898 #928 #1071: ihk_os_boot: Wait until IKC and sysfs get ready
4. #1106: ihk_os_create_pseudofs, ihk_os_create_pseudofs: Return
          -ECHILD when children are wait()-ed by others
5. #1082: ihklib: Make query functions thread-safe
6. #1067: ihklib: Create /proc and /sys in specified namespace
7. #1131: ihklib: Fix functions to query CPUs
8. #1069: ihklib: Fix functions to get number of CPUs
9. #1073: Fix double-free of ihk_kmsg_buf_container in __ihk_os_shutdown()

===================================
What's new in V1.5.0 (Apr 5, 2018)
===================================

---------------------------------
IHK new features and improvements
---------------------------------
1. Speedup memory allocation time
2. Support movable memory
3. Check if IHK, McKernel and mcexec have the same build-id

----------------------
IHK bug fixes (digest)
----------------------
1. #1042: ihkmond: Fix stack usage and temporary file location
2. #942: IKC: Check validity of IRQ handlers before manipulating it
3. #898: Shutdown OS only after no in-flight IKC exist
4. #1068: __ihk_smp_reserve_mem(): free any leftover from allocation chunks
5. #1118: IKC: Timeout when queue is full
6. #1088: Don't take too much memory from Linux to prevent OOM


===================================
What's new in V1.4.0 (Oct 30, 2017)
===================================

N/A


===================================
What's new in V1.3.0 (Sep 30, 2017)
===================================

--------------------
Feature: Kernel dump
--------------------
1. A dump level of "only kernel memory" is added.

The following two levels are available now:
   0: Dump all
  24: Dump only kernel memory

The dump level can be set by -d option in ihkosctl or the argument
for ihk_os_makedumpfile(), as shown in the following examples:

   Command:		ihkosctl 0 dump -d 24
   Function call:	ihk_os_makedumpfile(0, NULL, 24, 0);

2. Dump file is created when Linux panics.

The dump level can be set by dump_level kernel argument, as shown in the
following example:

   ihkosctl 0 kargs "hidos dump_level=24"

The IHK dump function is registered to panic_notifier_list when creating
/dev/mcdX and called when Linux panics.
