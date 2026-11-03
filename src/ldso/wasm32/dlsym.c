#include "dynlink.h"

/* dlsym, its caller's module told by an address of that module's
 * (dlfcn.h's dlsym, inline in its caller, passes its __dso_handle), as
 * other archs' dlsym.s pass the return address */
void *__dlsym_from(void *restrict p, const char *restrict s, void *caller)
{
	return __dlsym(p, s, caller);
}

/* (the extern dlsym, for code that calls it as a function of libc.so's,
 * not through dlfcn.h: no module of its caller's told) */
void *dlsym(void *restrict p, const char *restrict s)
{
	return __dlsym(p, s, 0);
}
