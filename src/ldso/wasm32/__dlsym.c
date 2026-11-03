/* dlsym: see dlopen.c */
#include <dlfcn.h>
#include "dynlink.h"

#ifdef WASM_SHARED_LIBC
__attribute__((import_module("dyn"), import_name("sym")))
void *__dyn_sym(int handle, const char *name);

void *__dlsym(void *restrict p, const char *restrict s, void *restrict ra)
{
	void *sym = __dyn_sym((int)(long)p, s);

	if (!sym)
		__dl_seterr("Symbol not found: %s", s);
	return sym;
}
#else
static void *stub_dlsym(void *restrict p, const char *restrict s, void *restrict ra)
{
	__dl_seterr("Symbol not found: %s", s);
	return 0;
}

weak_alias(stub_dlsym, __dlsym);
#endif
