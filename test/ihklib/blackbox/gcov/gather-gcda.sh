#!/bin/bash -e

DEST=$2
GCDA=/sys/kernel/debug/gcov
#BASE=/home/toshi/build/wb1-mckernel/
BASE=$1
DIR=.

if [ -z "$DEST" ] ; then
  echo "Usage: $0 <build-dir> <output.tar.gz>" >&2
  exit 1
fi

TEMPDIR=$(mktemp -d)
CWD=$(pwd)
echo Collecting data..
cd $GCDA/$BASE
find $DIR -type d -exec mkdir -p $TEMPDIR/\{\} \;
find $DIR -name '*.gcda' -exec sh -c 'cat < $0 > '$TEMPDIR'/$0' {} \;
find $DIR -name '*.gcno' -exec sh -c 'cp -d $0 '$TEMPDIR'/$0' {} \;
cd $BASE
find $DIR -type d -exec mkdir -p $TEMPDIR/\{\} \;
find $DIR -name '*.gcda' -exec sh -c 'cat < $0 > '$TEMPDIR'/$0' {} \;
find $DIR -name '*.gcno' -exec sh -c 'cp -d $0 '$TEMPDIR'/$0' {} \;
cd $CWD
tar czf $DEST -C $TEMPDIR $DIR
rm -rf $TEMPDIR

echo "$DEST successfully created, copy to build system and unpack with:"
echo "  tar xfz $DEST"
