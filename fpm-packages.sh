#!/bin/sh

set -e

PROJECT_ROOT=$PWD
FPM_DIR=$PWD/playerctl-fpm
DEB_DIR=$FPM_DIR/deb
RPM_DIR=$FPM_DIR/rpm

# sanity check
if [ ! -f playerctl/playerctl.h ]; then
    echo 'You must run this from the playerctl project directory'
    exit 1
fi

command -v fpm &> /dev/null || {
    echo "you need fpm to package playerctl"
    exit 127
}

VERSION=`./playerctl/playerctl -v | sed s/^v//`

rm -rf $FPM_DIR

function fpm_deb() {
    cd $PROJECT_ROOT
    make clean
    ./configure --prefix=/usr --libdir=/usr/lib
    make

    DESTDIR=$DEB_DIR make install

    cd $DEB_DIR

    fpm -s dir -t deb -n playerctl -v $VERSION \
        -p playerctl-VERSION_ARCH.deb \
        -d "libglib2.0-0" \
        usr/include usr/lib usr/bin usr/share/gir-1.0

    command -v dpkg &> /dev/null && {
        echo -e "\nDEBIAN PACKAGE CONTENTS"
        echo -e "-----------------------"
        dpkg -c $DEB_DIR/playerctl-${VERSION}_amd64.deb
    }
}

function fpm_rpm() {
    cd $PROJECT_ROOT
    make clean
    ./configure --prefix=/usr --libdir=/usr/lib64
    make

    DESTDIR=$RPM_DIR make install

    cd $RPM_DIR

    fpm -s dir -t rpm -n playerctl -v $VERSION \
        -p playerctl-VERSION_ARCH.rpm \
        -d "glib2" \
        usr/include usr/lib64 usr/bin usr/share/gir-1.0

    command -v rpm &> /dev/null && {
        echo -e "\nRPM PACKAGE CONTENTS"
        echo -e "--------------------"
        rpm -qlp $RPM_DIR/playerctl-${VERSION}_x86_64.rpm
    }
}

fpm_deb
fpm_rpm
