#pragma once
// Test-only Win32 loader bridge. This does NOT emulate the Windows ABI.
#define __stdcall
using HINSTANCE = void *;
extern "C" HINSTANCE LoadLibraryA(const char *);
extern "C" int FreeLibrary(HINSTANCE);
extern "C" void *GetProcAddress(HINSTANCE, const char *);
extern "C" unsigned long GetLastError();
extern "C" void OutputDebugStringA(const char *);
