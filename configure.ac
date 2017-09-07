# configure.ac COPYRIGHT FUJITSU LIMITED 2015-2016
AC_PREREQ(2.63)
m4_define([IHK_VERSION_m4],[0.9.0])dnl
m4_define([MCKERNEL_VERSION_m4],[0.9.0])dnl
m4_define([DCFA_VERSION_m4],[0.9.0])dnl
m4_define([IHK_RELEASE_DATE_m4],[2013-11-18])dnl
m4_define([MCKERNEL_RELEASE_DATE_m4],[2013-11-18])dnl
m4_define([DCFA_RELEASE_DATE_m4],[2013-11-18])dnl


AC_INIT([ihk], IHK_VERSION_m4)

IHK_VERSION=IHK_VERSION_m4
MCKERNEL_VERSION=MCKERNEL_VERSION_m4
DCFA_VERSION=DCFA_VERSION_m4
IHK_RELEASE_DATE=IHK_RELEASE_DATE_m4
MCKERNEL_RELEASE_DATE=MCKERNEL_RELEASE_DATE_m4
DCFA_RELEASE_DATE=DCFA_RELEASE_DATE_m4

AC_PREFIX_DEFAULT([/opt/ppos])

AC_ARG_WITH([kernelsrc],
  AS_HELP_STRING(
    [--with-kernelsrc=path],[Path to 'kernel src', default is /lib/modules/uname_r/build]),
  [WITH_KERNELSRC=$withval],[WITH_KERNELSRC=yes])

AC_ARG_WITH([target],
  AS_HELP_STRING(
    [--with-target={attached-mic | builtin-mic | builtin-x86 | smp-x86 | smp-arm64}],[target, default is attached-mic]),
  [WITH_TARGET=$withval],[WITH_TARGET=yes])

AC_ARG_ENABLE([memdump],
	AS_HELP_STRING([--enable-memdump],
		[enable dumping memory and analyzing a dump]),
	[ENABLE_MEMDUMP=$enableval],
	[ENABLE_MEMDUMP=default])

AC_ARG_ENABLE([rusage],
	AS_HELP_STRING([--enable-rusage],
		[enable rusage implementation]),
	[ENABLE_RUSAGE=$enableval],
	[ENABLE_RUSAGE=yes])

AC_ARG_WITH([system_map],
  AS_HELP_STRING(
    [--with-system_map=path],[Path to 'System.map file', default is /boot/System.map-uname_r]),
  [WITH_SYSTEM_MAP=$withval],[WITH_SYSTEM_MAP=yes])

# POSTK_DEBUG_ARCH_DEP_84 power management support [BEGIN]
HPCPWR_DIR_DEFAULT=/opt/FJSVxos/hpcpwr/lib/modules/make_kernelrelease/build
AC_ARG_WITH([hpcpwr],
  AS_HELP_STRING(
    [--with-hpcpwr=path],[Path to 'hpcpwr' "$HPCPWR_DIR_DEFAULT"]),
  [WITH_HPCPWR=$withval],[WITH_HPCPWR=no])
# POSTK_DEBUG_ARCH_DEP_84 power management support [END]

case "X$WITH_KERNELSRC" in
    Xyes | Xno | X)
	WITH_KERNELSRC="/lib/modules/`uname -r`/build"
	;;
    X/*)
	;;
    *)
	WITH_KERNELSRC="`pwd`/$WITH_KERNELSRC"
	;;
esac

if test "X$WITH_TARGET" = Xyes -o "X$WITH_TARGET" = Xno; then
	WITH_TARGET=attached-mic
fi

if test "X$bindir" = X; then
	BINDIR="$bindir"
fi

if test "X$sbindir" = X; then
	SBINDIR="$sbindir"
fi

if test "X$libdir" = X; then
	LIBDIR="$libdir"
fi

if test "X$includedir" = X; then
	INCLUDEDIR="$includedir"
fi

test "x$prefix" = xNONE && prefix="$ac_default_prefix"

case $WITH_TARGET in
# POSTK_DEBUG_ARCH_DEP_30, Expansion of the branch.
    attached-mic | builtin-x86 | smp-*)
	case $WITH_TARGET in
	    attached-mic | builtin-x86 | smp-x86)
		ARCH=`uname -m`
		;;
	    smp-arm64)
		ARCH=arm64
		;;
	esac
	AC_PROG_CC
	XCC=$CC
	CFLAGS="$CFLAGS -ffreestanding -fno-tree-loop-distribute-patterns"
	;;
    builtin-mic)
	ARCH=k1om
	AC_CHECK_PROG(XCC,
		[x86_64-$ARCH-linux-gcc],
		[x86_64-$ARCH-linux-gcc],
		[no])
	CC=$XCC
	;;
    *)
	AC_MSG_ERROR([target $WITH_TARGET is unknwon])
	;;
esac

case $WITH_TARGET in
    attached-mic)
	if test "X$BINDIR" = X; then
		BINDIR="$prefix/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="$prefix/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="$prefix/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="$prefix/man"
	fi
	;;
    builtin-mic)
	if test "X$BINDIR" = X; then
		BINDIR="$prefix/attached/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="$prefix/attached/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="$prefix/attached/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="$prefix/attached/man"
	fi
	;;
    builtin-x86)
	if test "X$BINDIR" = X; then
		BINDIR="$prefix/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="$prefix/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="$prefix/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="$prefix/attached/man"
	fi
	;;
    smp-x86)
	if test "X$BINDIR" = X; then
		BINDIR="$prefix/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="$prefix/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="$prefix/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="$prefix/smp-x86/man"
	fi
	if test "X$LIBDIR" = X; then
		LIBDIR="$prefix/lib"
	fi
	if test "X$INCLUDEDIR" = X; then
		INCLUDEDIR="$prefix/include"
	fi
	;;
    smp-arm64)
	if test "X$BINDIR" = X; then
		BINDIR="$prefix/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="$prefix/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="$prefix/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="$prefix/smp-arm64/man"
	fi
	if test "X$LIBDIR" = X; then
		LIBDIR="$prefix/lib"
	fi
	if test "X$INCLUDEDIR" = X; then
		INCLUDEDIR="$prefix/include"
	fi

	;;
    *)
	AC_MSG_ERROR([target $WITH_TARGET is unknwon])
	;;
esac

case $ENABLE_MEMDUMP in
	yes|no|auto)
		;;
	default)
# POSTK_DEBUG_ARCH_DEP_30, Expansion of the branch.
		if test "x$WITH_TARGET" = "xsmp-*"; then
			ENABLE_MEMDUMP=auto
		else
			ENABLE_MEMDUMP=no
		fi
		;;
	*)
		AC_MSG_ERROR([unknown memdump argument: $ENABLE_MEMDUMP])
		;;
esac

case $ENABLE_RUSAGE in
	yes|no)
		;;
	default)
		ENABLE_RUSAGE=yes
		;;
	*)
		AC_MSG_ERROR([unknown rusage argument: $ENABLE_RUSAGE])
		;;
esac

# POSTK_DEBUG_ARCH_DEP_84 power management support [BEGIN]
case "X$WITH_HPCPWR" in
    Xno | X)
	;;
    Xyes)
	HPCPWR_DIR="yes"
	;;
    X/*)
	HPCPWR_DIR="$WITH_HPCPWR"
	;;
    *)
	HPCPWR_DIR="`pwd`/$WITH_HPCPWR"
	;;
esac
# POSTK_DEBUG_ARCH_DEP_84 power management support [END]

KDIR="$WITH_KERNELSRC"
TARGET="$WITH_TARGET"

IHK_LINUX_SYMTAB=""
case "X$WITH_SYSTEM_MAP" in
    Xyes | Xno | X)
	IHK_LINUX_SYMTAB=""
	;;
    *)
	IHK_LINUX_SYMTAB="$WITH_SYSTEM_MAP"
	;;
esac

AC_MSG_CHECKING([[for System.map]])
if test -f "$IHK_LINUX_SYMTAB"; then
	IHK_LINUX_SYMTAB="$IHK_LINUX_SYMTAB"
elif test -f "/boot/System.map-`uname -r`"; then
	IHK_LINUX_SYMTAB="/boot/System.map-`uname -r`"	
elif test -f "$KDIR/System.map"; then
	IHK_LINUX_SYMTAB="$KDIR/System.map"	
fi

if test "$IHK_LINUX_SYMTAB" == ""; then
	AC_MSG_ERROR([could not find])
fi

if test -z "`eval cat $IHK_LINUX_SYMTAB`"; then
	AC_MSG_ERROR([could not read System.map file, no read permission?])
fi
AC_MSG_RESULT([$IHK_LINUX_SYMTAB])

IHK_LINUX_SYMTAB_CMD="cat $IHK_LINUX_SYMTAB"

# IHK_FIND_KSYM(SYMBOL)
# ------------------------------------------------------
# Search System.map for address of the given symbol and
# do one of three things in config.h:
# If not found, leave IHK_KSYM_foo undefined
# If found to be exported, "#define IHK_KSYM_foo 0"
# If found not to be exported, "#define IHK_KSYM_foo 0x<value>"

# POSTK_DEBUG_ARCH_DEP_10, Enhance IHK_FIND_KSYM macro feature.
AC_DEFUN([IHK_FIND_KSYM],[
  AC_MSG_CHECKING([[System.map for symbol $1]])
  symbol_src=$2
  ihk_addr=`eval $IHK_LINUX_SYMTAB_CMD | grep " $1\$" | cut -d\  -f1`
  ihk_addr_array=($ihk_addr)
  if test ${#ihk_addr_array[*]} -gt 1 ; then
    for addr in $ihk_addr ; do
      filename=`addr2line -e $KDIR/vmlinux ${addr} | cut -d ':' -f1 | rev | cut -d '/' -f1 | rev`
      if test $filename = $symbol_src  ; then
        ihk_addr=$addr
        break
      fi
    done
  fi
  if test -z $ihk_addr; then
    AC_MSG_RESULT([not found])
  else
    ihk_result=$ihk_addr
    ihk_addr="0x$ihk_addr"
    m4_ifval([$2],[],[
      if `eval $IHK_LINUX_SYMTAB_CMD | grep " __ksymtab_$1\$" >/dev/null`; then
        ihk_result="exported"
        ihk_addr="0"
      fi
    ])
    AC_MSG_RESULT([$ihk_result])
    AC_DEFINE_UNQUOTED(IHK_KSYM_[]$1$3,$ihk_addr,
			[Define to address of kernel symbol $1$3, or 0 if exported])
  fi
])
# <older version>
# AC_DEFUN([IHK_FIND_KSYM],[
#   AC_MSG_CHECKING([[System.map for symbol $1]])
#   ihk_addr=`eval $IHK_LINUX_SYMTAB_CMD | grep " $1\$" | cut -d\  -f1`
#   if test -z $ihk_addr; then
#     AC_MSG_RESULT([not found])
#   else
#     ihk_result=$ihk_addr
#     ihk_addr="0x$ihk_addr"
#     m4_ifval([$2],[],[
#       if `eval $IHK_LINUX_SYMTAB_CMD | grep " __ksymtab_$1\$" >/dev/null`; then
# 	ihk_result="exported"
# 	ihk_addr="0"
#       fi
#     ])
#     AC_MSG_RESULT([$ihk_result])
#     AC_DEFINE_UNQUOTED(IHK_KSYM_[]$1,$ihk_addr,
# 			[Define to address of kernel symbol $1, or 0 if exported])
#   fi
# ])

if test "$TARGET" = "smp-x86"; then
	IHK_FIND_KSYM([x86_trampoline_base])
	IHK_FIND_KSYM([real_mode_header])
	IHK_FIND_KSYM([per_cpu__vector_irq])
	IHK_FIND_KSYM([vector_irq])
	IHK_FIND_KSYM([lapic_get_maxlvt])
	IHK_FIND_KSYM([init_deasserted])
	IHK_FIND_KSYM([irq_to_desc])
	IHK_FIND_KSYM([alloc_desc])
	IHK_FIND_KSYM([irq_desc_tree])
	IHK_FIND_KSYM([irq_to_desc_alloc_node])
	IHK_FIND_KSYM([dummy_irq_chip])
	IHK_FIND_KSYM([get_uv_system_type])
	IHK_FIND_KSYM([wakeup_secondary_cpu_via_init])
	IHK_FIND_KSYM([__default_send_IPI_dest_field])
elif test "$TARGET" = "smp-arm64"; then
	IHK_FIND_KSYM([gic_data], [irq-gic.c], [_gicv2])
	IHK_FIND_KSYM([gic_data], [irq-gic-v3.c], [_gicv3])
	IHK_FIND_KSYM([gic_raise_softirq], [irq-gic.c], [_gicv2])
	IHK_FIND_KSYM([gic_raise_softirq], [irq-gic-v3.c], [_gicv3])
	IHK_FIND_KSYM([__irq_domain_alloc_irqs])
	IHK_FIND_KSYM([irq_domain_free_irqs])
	IHK_FIND_KSYM([irq_to_desc])
	IHK_FIND_KSYM([alloc_desc])
	IHK_FIND_KSYM([irq_desc_tree])
	IHK_FIND_KSYM([irq_to_desc_alloc_node])
	IHK_FIND_KSYM([psci_ops])
	IHK_FIND_KSYM([__cpu_logical_map])
	IHK_FIND_KSYM([invoke_psci_fn])
	IHK_FIND_KSYM([__invoke_psci_fn_hvc])
	IHK_FIND_KSYM([__invoke_psci_fn_smc])
	IHK_FIND_KSYM([acpi_gic_ver])
	IHK_FIND_KSYM([cpu_pmu])
	IHK_FIND_KSYM([__oprofile_cpu_pmu])
	IHK_FIND_KSYM([__irq_set_affinity])
	IHK_FIND_KSYM([arch_timer_use_virtual])
	IHK_FIND_KSYM([arch_timer_uses_ppi])
	IHK_FIND_KSYM([arch_timer_rate])
fi

if test "x$ENABLE_MEMDUMP" != "xno" ; then
	enableval=yes
# POSTK_DEBUG_ARCH_DEP_32, AC_CHECK_LIB for libiberty
	AC_CHECK_LIB([iberty],[hex_init],[],[enableval=no])
	AC_CHECK_LIB([bfd],[bfd_init],[],[enableval=no])
	AC_CHECK_HEADER([bfd.h],[],[enableval=no])

	if test "x$ENABLE_MEMDUMP" = "xyes" -a "x$enableval" = "xno" ; then
		AC_MSG_ERROR([memdump feature needs bfd.h and libbfd a.k.a binutils-devel])
	fi
	ENABLE_MEMDUMP=$enableval
fi

if test "x$ENABLE_MEMDUMP" = "xyes" ; then
	AC_MSG_NOTICE([memdump feature is enabled])
	AC_DEFINE([ENABLE_MEMDUMP],[1],[whether memdump feature is enabled])
	uncomment_if_ENABLE_MEMDUMP=''
else
	AC_MSG_NOTICE([memdump feature is disabled])
	uncomment_if_ENABLE_MEMDUMP='#'
fi

if test "x$ENABLE_RUSAGE" = "xyes" ; then
	AC_MSG_NOTICE([rusage is enabled])
	AC_DEFINE([ENABLE_RUSAGE],[1],[whether rusage is enabled])
else
	AC_MSG_NOTICE([rusage is disabled])
fi

# POSTK_DEBUG_ARCH_DEP_84 power management support [BEGIN]
if test "$TARGET" = "smp-arm64" ; then
	ENABLE_HPCPWR=no

	if test "$HPCPWR_DIR" = "yes" ; then
		HPCPWR_KERNEL_RELEASE="$KDIR/include/config/kernel.release"
		AC_MSG_CHECKING([[for kernel.release]])

		if test ! -f "$HPCPWR_KERNEL_RELEASE" ; then
			AC_MSG_ERROR([could not find])
		fi

		if test -z "`eval cat $HPCPWR_KERNEL_RELEASE`"; then
			AC_MSG_ERROR([could not read kernel.release file, no read permission?])
		fi
		AC_MSG_RESULT([$HPCPWR_KERNEL_RELEASE])
		HPCPWR_DIR="/opt/FJSVxos/hpcpwr/lib/modules/`cat $HPCPWR_KERNEL_RELEASE`/build"
	fi

	if test -n "$HPCPWR_DIR" ; then
		AC_MSG_CHECKING([[for Module.symvers]])
		HPCPWR_MODULE_SYMVERS="$HPCPWR_DIR/Module.symvers"

		if test ! -f "$HPCPWR_MODULE_SYMVERS" ; then
			AC_MSG_ERROR([could not find])
		fi

		if test -z "`eval cat $HPCPWR_MODULE_SYMVERS`"; then
			AC_MSG_ERROR([could not read Module.symvers file, no read permission?])
		fi

		AC_MSG_RESULT([$HPCPWR_MODULE_SYMVERS])

		CPPFLAGS="$CPPFLAGS -I$HPCPWR_DIR"
		AC_CHECK_HEADER([pwr_arm64hpcdev_mck.h],[enableval=yes],[enableval="no"])
		if test "x$enableval" = "xno" ; then
			AC_MSG_ERROR([power management feature needs pwr_arm64hpcdev_mck.h])
		fi
		ENABLE_HPCPWR=$enableval
	fi

	if test "x$ENABLE_HPCPWR" = "xyes" ; then
		AC_MSG_NOTICE([power management is enabled])
		AC_DEFINE([ENABLE_HPCPWR],[1],[whether power management is enabled])
	else
		AC_MSG_NOTICE([power management is disabled])
	fi
fi
# POSTK_DEBUG_ARCH_DEP_84 power management support [END]

AC_SUBST(CC)
AC_SUBST(XCC)
AC_SUBST(ARCH)
AC_SUBST(KDIR)
AC_SUBST(TARGET)
AC_SUBST(BINDIR)
AC_SUBST(SBINDIR)
AC_SUBST(INCLUDEDIR)
AC_SUBST(LIBDIR)
AC_SUBST(KMODDIR)
AC_SUBST(MANDIR)
AC_SUBST(CFLAGS)

AC_SUBST(IHK_VERSION)
AC_SUBST(MCKERNEL_VERSION)
AC_SUBST(DCFA_VERSION)
AC_SUBST(IHK_RELEASE_DATE)
AC_SUBST(MCKERNEL_RELEASE_DATE)
AC_SUBST(DCFA_RESEASE_DATE)
AC_SUBST(uncomment_if_ENABLE_MEMDUMP)

# POSTK_DEBUG_ARCH_DEP_84 power management support [BEGIN]
AC_SUBST(HPCPWR_DIR)
# POSTK_DEBUG_ARCH_DEP_84 power management support [END]

case "$TARGET" in
smp*)
	AC_CONFIG_HEADERS([linux/driver/smp/config.h linux/user/config.h])
	arch=`echo "$TARGET" | sed -e 's/smp-//'`
	AC_CONFIG_HEADERS([linux/driver/smp/arch/$arch/config-$arch.h])
	;;
esac

AC_CONFIG_FILES([
	Makefile
	linux/core/Makefile
	linux/core/Makefile.module
	linux/core/Makefile.builtin
	ikc/Makefile
	linux/user/Makefile
	linux/driver/builtin/Makefile
	linux/driver/smp/Makefile
	linux/driver/attached/mic/Makefile
	linux/user/ihkconfig.1:linux/user/ihkconfig.1in
	linux/user/ihkosctl.1:linux/user/ihkosctl.1in
	linux/user/ihklib.h:linux/user/ihklib.h.in
	cokernel/smp/arm64/Makefile.predefines
	cokernel/Makefile.common
])

AC_OUTPUT
