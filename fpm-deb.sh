#!/bin/sh

set -e

rm -rf /tmp/playerctl-fpm
./configure --prefix=/usr
make
VERSION=`./playerctl/playerctl -V | sed s/^v//`
DESTDIR=/tmp/playerctl-fpm make install

cd /tmp/playerctl-fpm

fpm -s dir -t deb -n playerctl -v $VERSION \
    -p playerctl-VERSION_ARCH.deb \
    -d "libglib2.0-0" \
    usr/include usr/lib usr/bin usr/share/gir-1.0

cd -

mv /tmp/playerctl-fpm/playerctl-${VERSION}_amd64.deb ./

rm -r /tmp/playerctl-fpm

echo -e "\nPACKAGE CONTENTS\n"

dpkg -c ./playerctl-${VERSION}_amd64.deb
