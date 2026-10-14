#!/bin/sh

set -eu

if [ -z "${TARGET_BUILD_DIR:-}" ] || [ -z "${EXECUTABLE_PATH:-}" ] ||
   [ -z "${SRCROOT:-}" ]; then
    echo "error: deploy_libsquid.sh must run from an Xcode build phase" >&2
    exit 1
fi

install_root="${HOME}/Library/Developer/Squid"
library_source="${TARGET_BUILD_DIR}/${EXECUTABLE_PATH}"
library_destination="${install_root}/lib/liblibsquid.dylib"
header_source="${SRCROOT}/libsquid/squid.h"
header_destination="${install_root}/include/squid.h"

/usr/bin/install -d "${install_root}/lib" "${install_root}/include"

if ! /usr/bin/cmp -s "${library_source}" "${library_destination}"; then
    /usr/bin/ditto "${library_source}" "${library_destination}"
fi

if ! /usr/bin/cmp -s "${header_source}" "${header_destination}"; then
    /usr/bin/ditto "${header_source}" "${header_destination}"
fi

echo "Deployed liblibsquid.dylib and squid.h to ${install_root}"
