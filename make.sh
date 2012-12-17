#!/bin/sh
installdir=
kerneldir=
target=attached-mic
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
	for tgt in $target; do
		case "$tgt" in
		    attached-mic)
			(cd linux/driver/attached/mic; make clean)
			;;
		    builtin*)
			(cd linux/driver/builtin; make clean)
			;;
		    *)
			echo "unknown target $tgt" >&2
			exit 1
			;;
		esac
	done
	rm -f target
	(cd linux/user; make clean)
	exit 0
fi

if [ "X$installdir" != X ]; then
	mkdir -p "$installdir"
fi
(cd linux/core; make modules)
if [ -f linux/core/ihk.ko ]; then
	if [ "X$installdir" != X ]; then
		cp linux/core/ihk.ko "$installdir"
	fi
else
	echo "linux/core/ihk.ko could not be built" >&2
	exit 1
fi
for tgt in $target; do
	case "$tgt" in
	    attached-mic)
		(cd linux/driver/attached/mic; make modules)
		mod=linux/driver/attached/mic/ihk_mic.ko
		;;
	    builtin*)
		(cd linux/driver/builtin; make modules)
		mod=linux/driver/builtin/ihk_builtin.ko
		;;
	    *)
		echo "unknown target $tgt" >&2
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
done
echo "$target" > target

(cd linux/user; make)
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
