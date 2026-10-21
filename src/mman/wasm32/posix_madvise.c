/* a wasm memory has no protection, paging or locking: nothing to do (wasm_mman.c) */
#include <sys/mman.h>
#include "syscall.h"

int posix_madvise(void *addr, size_t len, int advice)
{
	return 0;
}
