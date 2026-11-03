#include <dlfcn.h>
#include "dynlink.h"

#undef dlsym

/* (a caller that took dlsym's address, not through dlfcn.h's macro: no
 * module of its told, as with no return address) */
void *dlsym(void *restrict p, const char *restrict s)
{
	return __dlsym(p, s, 0);
}

/* dlsym, its caller's module told by an address of that module's
 * (dlfcn.h's macro passes its __dso_handle), as other archs' dlsym.s pass
 * the return address */
void *__dlsym_from(void *restrict p, const char *restrict s, void *caller)
{
	return __dlsym(p, s, caller);
}
