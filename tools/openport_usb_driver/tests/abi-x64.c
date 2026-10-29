#include <stddef.h>
#include "../j2534/j2534.h"
_Static_assert(sizeof(void *)==8,"pointer");
_Static_assert(sizeof(unsigned long)==4,"Windows LLP64 unsigned long");
_Static_assert(sizeof(int32_t)==4,"status");
_Static_assert(offsetof(PASSTHRU_MSG,Data)==24,"message data offset");
_Static_assert(sizeof(PASSTHRU_MSG)==4152,"message size");
_Static_assert(sizeof(SCONFIG)==8,"SCONFIG size");
_Static_assert(offsetof(SCONFIG_LIST,ConfigPtr)==8,"SCONFIG_LIST pointer offset");
_Static_assert(sizeof(SCONFIG_LIST)==16,"SCONFIG_LIST size");
