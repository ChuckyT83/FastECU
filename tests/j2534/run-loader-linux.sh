#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:?Usage: run-loader-linux.sh ABSOLUTE_BUILD_DIRECTORY}
mkdir -p "$build_dir"
# Only J2534_win.cpp sees the Win32 bridge; Qt and production platform macros
# are untouched. This validates loader logic on ELF, not Windows binary ABI.
${CXX:-g++} -std=c++11 -fPIC -shared "$source_dir/mock_dll.cpp" -o "$build_dir/mock-j2534.so"
${CXX:-g++} -std=c++11 -fPIC -shared -DOMIT_IOCTL "$source_dir/mock_dll.cpp" -o "$build_dir/incomplete-j2534.so"
${CXX:-g++} -std=c++11 -DWIN32 -I"$source_dir/loader-shim" "$source_dir/loader.cpp" "$source_dir/loader-shim/windows.cpp" "$source_dir/../../serial_port/J2534_win.cpp" -ldl -o "$build_dir/loader-test"
"$build_dir/loader-test" "$build_dir/mock-j2534.so" "$build_dir/incomplete-j2534.so"
