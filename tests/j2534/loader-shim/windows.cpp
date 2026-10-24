#include "windows.h"
#include <dlfcn.h>
#include <cstdio>
extern "C" HINSTANCE LoadLibraryA(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
extern "C" int FreeLibrary(HINSTANCE handle) { return handle ? dlclose(handle) == 0 : 0; }
extern "C" void *GetProcAddress(HINSTANCE handle, const char *name) { return dlsym(handle, name); }
extern "C" unsigned long GetLastError() { return 126; }
extern "C" void OutputDebugStringA(const char *text) { fputs(text, stderr); }
