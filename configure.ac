# configure.ac COPYRIGHT FUJITSU LIMITED 2015-2018
AC_PREREQ(2.63)
m4_define([IHK_VERSION_m4],[1.5.0])dnl
m4_define([MCKERNEL_VERSION_m4],[1.5.0])dnl
m4_define([IHK_RELEASE_DATE_m4],[2018-04-05])dnl
m4_define([MCKERNEL_RELEASE_DATE_m4],[2018-04-05])dnl

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

AC_ARG_ENABLE([perf],
	AS_HELP_STRING([--enable-perf],
		[enable perf_event_open implementation]),
	[ENABLE_PERF=$enableval],
	[ENABLE_PERF=yes])

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

# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.[begin]
#if test "X$bindir" = X; then
#	BINDIR="$bindir"
#fi
#
#if test "X$sbindir" = X; then
#	SBINDIR="$sbindir"
#fi
#
#if test "X$libdir" = X; then
#	LIBDIR="$libdir"
#fi
#
#if test "X$includedir" = X; then
#	INCLUDEDIR="$includedir"
#fi
# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.[end]

test "x$prefix" = xNONE && prefix="$ac_default_prefix"

DESTDIR=$prefix	# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.

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

# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.[begin]
# xxxDIR=$prefix/xxx => xxxDIR=xxx change
case $WITH_TARGET in
    attached-mic)
	if test "X$BINDIR" = X; then
		BINDIR="bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="share/man"
	fi
	;;
    builtin-mic)
	if test "X$BINDIR" = X; then
		BINDIR="attached/bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="attached/sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="attached/kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="share/man"
	fi
	;;
    builtin-x86)
	if test "X$BINDIR" = X; then
		BINDIR="bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="share/man"
	fi
	;;
    smp-x86)
	if test "X$BINDIR" = X; then
		BINDIR="bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="share/man"
	fi
	if test "X$LIBDIR" = X; then
		LIBDIR="lib"
	fi
	if test "X$INCLUDEDIR" = X; then
		INCLUDEDIR="include"
	fi
	;;
    smp-arm64)
	if test "X$BINDIR" = X; then
		BINDIR="bin"
	fi
	if test "X$SBINDIR" = X; then
		SBINDIR="sbin"
	fi
	if test "X$KMODDIR" = X; then
		KMODDIR="kmod"
	fi
	if test "X$MANDIR" = X; then
		MANDIR="share/man"
	fi
	if test "X$LIBDIR" = X; then
		LIBDIR="lib"
	fi
	if test "X$INCLUDEDIR" = X; then
		INCLUDEDIR="include"
	fi
	;;
    *)
	AC_MSG_ERROR([target $WITH_TARGET is unknwon])
	;;
esac
# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.[end]

case $ENABLE_MEMDUMP in
	yes|no|auto)
		;;
	default)
# POSTK_DEBUG_ARCH_DEP_30, Expansion of the branch.
#		if test "x$WITH_TARGET" = "xsmp-x86"; then
		if test "x$WITH_TARGET" = "xsmp-x86" -o "x$WITH_TARGET" = "xsmp-arm64" ; then
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

libudev=no
AC_CHECK_LIB([udev],[udev_new],[libudev=yes],[libudev=no])
AC_CHECK_HEADER([libudev.h],[libudev=yes],[libudev=no])
if test	"x$libudev" = "xno" ; then
   AC_MSG_ERROR([Install libudev package, e.g. yum install libudev-devel])
fi

if test "x$ENABLE_PERF" = "xyes" ; then
	AC_MSG_NOTICE([perf is enabled])
	AC_DEFINE([ENABLE_PERF],[1],[whether perf is enabled])
else
	AC_MSG_NOTICE([perf is disabled])
fi

ABS_SRCDIR=$( cd $( dirname $0 ); pwd )
IHK_ABS_SRCDIR=${ABS_SRCDIR}/../ihk
BUILDID=$( cd $IHK_ABS_SRCDIR; if @<:@ ! -d .git @:>@; then echo $IHK_VERSION; else bash -c 'git rev-list -1 HEAD | cut -c1-8'; fi )
AC_MSG_NOTICE([BUILDID=$BUILDID])
if test "x$BUILDID" != "x" ; then
   AC_DEFINE_UNQUOTED(BUILDID,"$BUILDID",[IHK build-id to confirm IHK and McKernel built at the same time are used])
fi
AC_SUBST(BUILDID)

AC_SUBST(CC)
AC_SUBST(XCC)
AC_SUBST(ARCH)
AC_SUBST(KDIR)
AC_SUBST(TARGET)
AC_SUBST(DESTDIR)	# POSTK_DEBUG_ARCH_DEP_105 make install DESTDIR enable.
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

case "$TARGET" in
smp*)
	AC_CONFIG_HEADERS([config.h])
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
	linux/include/ihk/ihklib.h:linux/include/ihk/ihklib.h.in
	cokernel/Makefile.common
])

if test -e "${ABS_SRCDIR}/test"; then
AC_CONFIG_FILES([
	mck_test_config.mk.sample:test/mck_test_config.mk.sample.in
])
fi

if test "$TARGET" = "smp-x86"; then
AC_CONFIG_FILES([
	cokernel/smp/x86_64/Makefile.config
])
fi

if test "$TARGET" = "smp-arm64"; then
AC_CONFIG_FILES([
	cokernel/smp/arm64/Makefile.predefines
	cokernel/smp/arm64/Makefile.config
])
fi

AC_OUTPUT
