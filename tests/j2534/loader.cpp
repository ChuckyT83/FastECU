#include "../../serial_port/J2534_win.h"
#include <cstdio>
#include <cstring>
#include <string>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } ++checks; } while (0)
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    int checks = 0;
    J2534 driver;
    driver.setDllName("this-driver-does-not-exist.dll");
    CHECK(!driver.init()); CHECK(!driver.valid());
    unsigned long device = 0, channel = 0;
    CHECK(driver.PassThruOpen(nullptr, &device) == ERR_DEVICE_NOT_CONNECTED);
    driver.setDllName(argv[2]);
    CHECK(!driver.init()); CHECK(!driver.valid());
    CHECK(strstr(driver.getLastError(), "PassThruIoctl") != nullptr);
    driver.setDllName(argv[1]);
    CHECK(driver.init()); CHECK(driver.valid());
    HINSTANCE control = LoadLibraryA(argv[1]);
    CHECK(control != nullptr);
    using SetFailure = void (PT_CALL *)(long);
    SetFailure fail = reinterpret_cast<SetFailure>(GetProcAddress(control, "MockFail"));
    CHECK(fail != nullptr);
    fail(ERR_FAILED);
    CHECK(driver.PassThruOpen(nullptr, &device) == ERR_FAILED);
    CHECK(driver.PassThruConnect(41, ISO15765, 0, 500000, &channel) == ERR_FAILED);
    CHECK(driver.PassThruIoctl(99, CLEAR_MSG_FILTERS, nullptr, nullptr) == ERR_FAILED);
    fail(0);
    FreeLibrary(control);
    CHECK(driver.PassThruOpen(nullptr, &device) == STATUS_NOERROR);
    CHECK(device == 41);
    CHECK(driver.PassThruConnect(device, ISO15765, 0, 500000, &channel) == STATUS_NOERROR);
    CHECK(channel == 99);
    CHECK(driver.PassThruDisconnect(device) == ERR_INVALID_CHANNEL_ID);
    CHECK(driver.PassThruDisconnect(channel) == STATUS_NOERROR);
    CHECK(driver.PassThruClose(channel) == ERR_INVALID_DEVICE_ID);
    CHECK(driver.PassThruClose(device) == STATUS_NOERROR);
    char api[80] = {}, dll[80] = {}, fw[80] = {};
    CHECK(driver.PassThruReadVersion(api, dll, fw, device) == STATUS_NOERROR);
    CHECK(strcmp(api, "04.04") == 0 && strcmp(dll, "1.2.3") == 0 && strcmp(fw, "9.8.7") == 0);
    driver.disable(); CHECK(!driver.valid());
#if !defined(_WIN32)
    // No other references are retained: the production disable must dlclose.
    void *retained = dlopen(argv[1], RTLD_NOLOAD | RTLD_NOW);
    CHECK(retained == nullptr);
#endif
    driver.disable(); CHECK(!driver.valid());
    CHECK(driver.init()); CHECK(driver.valid()); // reload and resolve pointers
    driver.disable();
    const std::string tooLong(256, 'x');
    driver.setDllName(tooLong.c_str()); CHECK(!driver.init());
    driver.setDllName(nullptr); CHECK(!driver.init());
    driver.setDllName(""); CHECK(!driver.init());
    { J2534 scoped; scoped.setDllName(argv[1]); CHECK(scoped.init()); }
#if !defined(_WIN32)
    retained = dlopen(argv[1], RTLD_NOLOAD | RTLD_NOW);
    CHECK(retained == nullptr); // destructor unloads without needing OP20PT32_USE_LIB
#endif
    printf("Loader regression checks passed: %d\n", checks);
}
