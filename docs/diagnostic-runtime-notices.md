# Diagnostic runtime notices

FastECU diagnostic source: GPL-3.0; see the packaged LICENSE and corresponding
commit at https://github.com/ChuckyT83/FastECU.

The standalone tool dynamically links Qt Core 5.15.2. Qt is available under
LGPL-3.0/GPL/commercial terms; these development packages use the open-source
SDK. The matching unmodified Qt source and license texts are available at
https://download.qt.io/archive/qt/5.15/5.15.2/single/ and
https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v5.15.2.
Replacement compatible Qt Core DLLs can be placed beside the executable; the
package does not restrict relinking or debugging changes to these libraries.

MinGW-w64 / GCC 8.1 runtime DLLs are deployed by Qt's windeployqt. GCC runtime
libraries carry GPL terms with the GCC Runtime Library Exception where
applicable: https://www.gnu.org/licenses/gcc-exception-3.1.html.
Matching toolchain sources/notices: https://download.qt.io/development_releases/prebuilt/mingw/
and https://gcc.gnu.org/releases.html. SDK-supplied notices are copied into this
directory when present; consult them for the individual components.

No Tactrix, OBDX, clone, EcuFlash or other proprietary driver is redistributed.
The full GUI package has additional dependency notices of its own.
