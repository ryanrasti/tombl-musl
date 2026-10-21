/* a wasm memory has no protection, paging or locking: nothing to do (wasm_mman.c) */
#include <sys/mman.h>
#include "syscall.h"

int mlock(const void *addr, size_t len)
{
	return 0;
}
