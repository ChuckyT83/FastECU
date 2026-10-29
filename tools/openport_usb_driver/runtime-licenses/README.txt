The candidate statically links compiler support and MinGW-w64 threading runtime
so the driver's only non-system runtime DLL remains the pinned libusb-1.0.dll.
The GCC Runtime Library Exception applies to compiler runtime linking; MinGW-w64
runtime has its own permissive notices. The accompanying Debian notice aggregates
supply the license texts, not proof of a compiler/runtime version. Consult the
build manifest's exact compiler and artifact SHA-256 for the actual build.
Corresponding source: https://gcc.gnu.org/releases.html and
https://github.com/mingw-w64/mingw-w64 . Native CI uses Qt's MinGW 8.1.0 package;
local cross-build uses Debian MinGW GCC 14.2.0-19+27+b1. No vendor driver code is
statically incorporated. libusb remains dynamically replaceable, with its own
source archive and license supplied separately.
