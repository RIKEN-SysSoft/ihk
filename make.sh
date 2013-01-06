#!/bin/sh
MOPT=
OPT=
installdir=
kerneldir=
if [ -f kerneldir ]; then
	kerneldir="`cat kerneldir`"
fi
if [ -f target ]; then
	target="`cat target`"
else
	target=attached-mic
fi
cleanflag=
while [ "X$1" != X ]; do
	case "$1" in
	    clean)
		cleanflag=1
		;;
	    installdir=*)
		installdir="`echo $1 | sed 's/^installdir=//'`"
		;;
	    kerneldir=*)
		kerneldir="`echo $1 | sed 's/^kerneldir=//'`"
		;;
	    target=*)
		target="`echo $1 | sed 's/^target=//'`"
		;;
	    *)
		echo "unknown option $1" >&2
		exit 1
		;;
	esac
	shift
done

if [ "X$cleanflag" != X ]; then
	(cd linux/core; make clean)
	case "$target" in
	    attached-mic)
		(cd linux/driver/attached/mic; make clean)
		;;
	    builtin*)
		(cd linux/driver/builtin; make clean)
		;;
	    *)
		echo "unknown target $target" >&2
		exit 1
		;;
	esac
	rm -f target
	rm -f kerneldir
	(cd linux/user; make clean)
	exit 0
fi

if [ "X$kerneldir" != X ]; then
	MOPT="KDIR=$kerneldir"
fi
if [ "X$target" = "Xbuiltin-mic" ]; then
	MOPT="$MOPT ARCH=k1om"
	OPT="CC=x86_64-k1om-linux-gcc"
fi

if [ "X$installdir" != X ]; then
	mkdir -p "$installdir"
fi
(cd linux/core; make $MOPT modules)
if [ -f linux/core/ihk.ko ]; then
	if [ "X$installdir" != X ]; then
		cp linux/core/ihk.ko "$installdir"
	fi
else
	echo "linux/core/ihk.ko could not be built" >&2
	exit 1
fi
case "$target" in
    attached-mic)
	(cd linux/driver/attached/mic; make $MOPT modules)
	mod=linux/driver/attached/mic/ihk_mic.ko
	;;
    builtin*)
	(cd linux/driver/builtin; make $MOPT modules)
	mod=linux/driver/builtin/ihk_builtin.ko
	;;
    *)
	echo "unknown target $target" >&2
	exit 1
	;;
esac
if [ -f $mod ]; then
	if [ "X$installdir" != X ]; then
		cp $mod "$installdir"
	fi
else
	echo "$mod could not be built" >&2
	exit 1
fi
echo "$target" > target
if [ "X$kerneldir" != X ]; then
	echo "$kerneldir" > kerneldir
fi

(cd linux/user; make $OPT)
if [ -f linux/user/ihktest ]; then
	if [ "X$installdir" != X ]; then
		cp linux/user/ihktest "$installdir"
	fi
else
	echo "linux/user/ihktest could not be built" >&2
	exit 1
fi
if [ -f linux/user/ihkostest ]; then
	if [ "X$installdir" != X ]; then
		cp linux/user/ihkostest "$installdir"
	fi
else
	echo "linux/core/ihkostest could not be built" >&2
	exit 1
fi
