/* dlclose: libraries stay loaded (shared builds; see dlopen.c) */
#include <dlfcn.h>
#include "dynlink.h"

int dlclose(void *p)
{
#ifdef WASM_SHARED_LIBC
	return 0;
#else
	return __dl_invalid_handle(p);
#endif
}
