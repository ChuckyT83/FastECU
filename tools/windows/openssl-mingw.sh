#!/usr/bin/env bash
# MSYS2 is a build dependency only. Output uses Qt's native x64 MinGW compiler.
set -euo pipefail
if [[ $# != 4 ]]; then
    echo 'Expected: Qt MinGW bin, OpenSSL source, build directory, install prefix' >&2
    exit 2
fi
task_mingw=$(/usr/bin/cygpath -u "$1")
task_source=$(/usr/bin/cygpath -u "$2")
task_build=$(/usr/bin/cygpath -u "$3")
task_prefix=$(/usr/bin/cygpath -u "$4")
export PATH="$task_mingw:/usr/bin:$PATH"
test -x "$task_mingw/gcc.exe"
test -f "$task_source/Configure"
cd "$task_build"
command -v gcc perl make
gcc --version
perl -V:osname -V:archname
perl "$task_source/Configure" mingw64 no-shared no-tests no-asm no-quic \
    --libdir=lib "--prefix=$task_prefix"
make -j2
make install_sw
