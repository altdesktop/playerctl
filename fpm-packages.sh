#!/usr/bin/env bash

set -e

PROJECT_ROOT=${PWD}
FPM_DIR=${PWD}/build/fpm
DEB_DIR=${FPM_DIR}/deb
RPM_DIR=${FPM_DIR}/rpm

usage() {
    cat <<EOF
Usage: ${0##*/} [-h|--help]

Build the playerctl release artifacts. Run from the project root.

Builds, in order:
  - a .deb package (fpm, prefix /usr, libdir /usr/lib)
  - an .rpm package (fpm, prefix /usr, libdir /usr/lib64)
  - a GPG-signed source tarball (meson dist)

Output goes to ./build/fpm/ (removed and recreated on every run).

Requires: fpm, rpm, dpkg, meson, ninja, git, gpg.

Options:
  -h, --help    show this help and exit
EOF
}

parse_args() {
    case "${1:-}" in
        -h|--help)
            usage
            exit 0
            ;;
        '') ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
}

check_environment() {
    if [[ ! -f playerctl/playerctl.h ]]; then
        echo 'You must run this from the playerctl project directory'
        exit 1
    fi

    local packages=(fpm rpm dpkg)
    local pkg
    for pkg in "${packages[@]}"; do
        if ! hash "${pkg}"; then
            echo "you need ${pkg} to package playerctl"
            exit 127
        fi
    done
}

fpm_deb() {
    cd "${PROJECT_ROOT}"
    meson setup "${DEB_DIR}/build" --prefix=/usr --libdir=/usr/lib
    DESTDIR="${DEB_DIR}/install" ninja -C "${DEB_DIR}/build" install
    VERSION=$(LD_LIBRARY_PATH="${DEB_DIR}/install/usr/lib" "${DEB_DIR}/install/usr/bin/playerctl" -v | sed s/^v// | sed s/-.*//)

    cd "${DEB_DIR}/install"

    fpm -s dir -t deb -n playerctl -v "${VERSION}" \
        -p playerctl-VERSION_ARCH.deb \
        -d "libglib2.0-0" \
        usr/include usr/lib usr/bin usr/share

    echo -e "\nDEBIAN PACKAGE CONTENTS"
    echo -e "-----------------------"
    dpkg -c "${DEB_DIR}/install/playerctl-${VERSION}_${DEB_ARCH}.deb"

    mv "${DEB_DIR}/install/playerctl-${VERSION}_${DEB_ARCH}.deb" "${FPM_DIR}"

    cd - &> /dev/null
}

fpm_rpm() {
    cd "${PROJECT_ROOT}"
    meson setup "${RPM_DIR}/build" --prefix=/usr --libdir=/usr/lib64
    DESTDIR="${RPM_DIR}/install" ninja -C "${RPM_DIR}/build" install
    VERSION=$(LD_LIBRARY_PATH="${RPM_DIR}/install/usr/lib64" "${RPM_DIR}/install/usr/bin/playerctl" -v | sed s/^v// | sed s/-.*//)

    cd "${RPM_DIR}/install"

    fpm -s dir -t rpm -n playerctl -v "${VERSION}" \
        -p playerctl-VERSION_ARCH.rpm \
        -d "glib2" \
        usr/include usr/lib64 usr/bin usr/share

    echo -e "\nRPM PACKAGE CONTENTS"
    echo -e "--------------------"
    rpm -qlp "${RPM_DIR}/install/playerctl-${VERSION}_${RPM_ARCH}.rpm"

    mv "${RPM_DIR}/install/playerctl-${VERSION}_${RPM_ARCH}.rpm" "${FPM_DIR}"

    cd - &> /dev/null
}

do_dist() {
    local DIST_DIR="${FPM_DIR}/dist"
    meson setup "${DIST_DIR}"
    meson dist -C "${DIST_DIR}" --formats gztar --allow-dirty --no-tests
    local archive_path
    archive_path=$(ls "${DIST_DIR}"/meson-dist/playerctl-*.tar.gz)
    gpg --sign --armor --detach-sign "${archive_path}"
}

main() {
    parse_args "$@"
    check_environment

    # computed after the tool check so a missing dpkg gets a clear message
    DEB_ARCH=$(dpkg --print-architecture)
    RPM_ARCH=$(uname -m)

    rm -rf "${FPM_DIR}"
    mkdir -p "${FPM_DIR}"

    fpm_deb
    fpm_rpm
    do_dist
}

# only run when executed directly, not when sourced
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
