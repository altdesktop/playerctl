#!/bin/sh

set -e

PROJECT_ROOT=${PWD}
FPM_DIR=${PWD}/playerctl-fpm
DEB_DIR=${FPM_DIR}/deb
RPM_DIR=${FPM_DIR}/rpm
MESON_DIR=${FPM_DIR}/build

# sanity check
if [[ ! -f playerctl/playerctl.h ]]; then
    echo 'You must run this from the playerctl project directory'
    exit 1
fi

packages=(fpm rpm dpkg)
for pkg in ${packages[@]}; do
    if ! hash ${pkg}; then
        echo "you need ${pkg} to package playerctl"
        exit 127
    fi
done

rm -rf ${FPM_DIR}
mkdir -p ${FPM_DIR}

fpm_deb() {
    cd ${PROJECT_ROOT}
    meson ${DEB_DIR}/build --prefix=/usr --libdir=/usr/lib
    DESTDIR=${DEB_DIR}/install ninja -C ${DEB_DIR}/build install
	VERSION=`LD_LIBRARY_PATH=${DEB_DIR}/install/usr/lib ${DEB_DIR}/install/usr/bin/playerctl -v | sed s/^v// | sed s/-.*//`

    cd ${DEB_DIR}/install

    fpm -s dir -t deb -n playerctl -v ${VERSION} \
        -p playerctl-VERSION_ARCH.deb \
        -d "libglib2.0-0" \
        usr/include usr/lib usr/bin usr/share

    echo -e "\nDEBIAN PACKAGE CONTENTS"
    echo -e "-----------------------"
    dpkg -c ${DEB_DIR}/install/playerctl-${VERSION}_amd64.deb

    mv ${DEB_DIR}/install/playerctl-${VERSION}_amd64.deb ${FPM_DIR}

	cd - &> /dev/null
}

fpm_rpm() {
    cd ${PROJECT_ROOT}
    meson ${RPM_DIR}/build --prefix=/usr --libdir=/usr/lib64
    DESTDIR=${RPM_DIR}/install ninja -C ${RPM_DIR}/build install
	VERSION=`LD_LIBRARY_PATH=${RPM_DIR}/install/usr/lib64 ${RPM_DIR}/install/usr/bin/playerctl -v | sed s/^v// | sed s/-.*//`

    cd ${RPM_DIR}/install

    fpm -s dir -t rpm -n playerctl -v ${VERSION} \
        -p playerctl-VERSION_ARCH.rpm \
        -d "glib2" \
        usr/include usr/lib64 usr/bin usr/share

    echo -e "\nRPM PACKAGE CONTENTS"
    echo -e "--------------------"
    rpm -qlp ${RPM_DIR}/install/playerctl-${VERSION}_x86_64.rpm

    mv ${RPM_DIR}/install/playerctl-${VERSION}_x86_64.rpm ${FPM_DIR}

    cd - &> /dev/null
}

do_dist() {
    local DIST_DIR=${FPM_DIR}/dist
    meson ${DIST_DIR}
    ninja -C ${DIST_DIR} dist
    gpg --sign --armor --detach-sign ${DIST_DIR}/meson-dist/playerctl-*.tar.xz
    mv ${DIST_DIR}/meson-dist/* ${FPM_DIR}
}

fpm_deb
fpm_rpm
do_dist
