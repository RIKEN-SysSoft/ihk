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
    [--with-target={attached-mic | builtin-mic | builtin-x86 | smp-x86}],[target, default is attached-mic]),
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
    attached-mic | builtin-x86 | smp-x86)
	ARCH=`uname -m`
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
    *)
	AC_MSG_ERROR([target $WITH_TARGET is unknwon])
	;;
esac

case $ENABLE_MEMDUMP in
	yes|no|auto)
		;;
	default)
		if test "x$WITH_TARGET" = "xsmp-x86" ; then
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
AC_DEFUN([IHK_FIND_KSYM],[
  AC_MSG_CHECKING([[System.map for symbol $1]])
  ihk_addr=`eval $IHK_LINUX_SYMTAB_CMD | grep " $1\$" | cut -d\  -f1`
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
    AC_DEFINE_UNQUOTED(IHK_KSYM_[]$1,$ihk_addr,
			[Define to address of kernel symbol $1, or 0 if exported])
  fi
])

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
fi

if test "x$ENABLE_MEMDUMP" != "xno" ; then
	enableval=yes
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

AC_CONFIG_HEADERS([linux/driver/smp-x86/config.h linux/user/config.h])
AC_CONFIG_FILES([
	Makefile
	linux/core/Makefile
	linux/core/Makefile.module
	linux/core/Makefile.builtin
	ikc/Makefile
	linux/user/Makefile
	linux/driver/builtin/Makefile
	linux/driver/smp-x86/Makefile
	linux/driver/attached/mic/Makefile
	linux/user/ihkconfig.1:linux/user/ihkconfig.1in
	linux/user/ihkosctl.1:linux/user/ihkosctl.1in
	linux/user/ihklib.h:linux/user/ihklib.h.in
	cokernel/Makefile.common
])

AC_OUTPUT
