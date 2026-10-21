/* a wasm memory has no protection, paging or locking: nothing to do (wasm_mman.c) */
#include <sys/mman.h>
#include "syscall.h"

int __madvise(void *addr, size_t len, int advice)
{
	return 0;
}

weak_alias(__madvise, madvise);
