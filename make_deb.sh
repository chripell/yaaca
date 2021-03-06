#!/bin/sh

NAME="$1"
if [ "$NAME" = "" ]
then
    echo Syntax: make_deb.sh [name] [desc] [sep] [file,where_to_install ..]
    exit 1
fi
shift

VER=`date '+%Y%m%d'`
ARCH=`dpkg --print-architecture`
T=/tmp/$NAME-$ARCH-$VER
rm -rf $T
mkdir -p $T

DESC="$1"
shift

DEP="$1"
shift

while [ ! "$1" = "" ]
do
    F=`echo $1 | awk -F, '{print $1}'`
    D=`echo $1 | awk -F, '{print $2}'`
    shift
    mkdir -p $T/$D
    cp -a $F $T/$D
done

SIZE=`du -ks $T | awk '{print $1}'`

mkdir -p $T/DEBIAN
cat <<EOF > $T/DEBIAN/control
Package: $NAME
Priority: optional
Section: misc
Installed-Size: $SIZE
Maintainer: chripell@gmail.com
Architecture: $ARCH
Version: $VER
Depends: $DEP
Description: $DESC
EOF

chmod -R 755 $T
dpkg-deb -z8 -Zgzip --build $T
