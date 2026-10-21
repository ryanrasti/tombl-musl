/* a wasm memory has no protection, paging or locking: nothing to do (wasm_mman.c) */
#include <sys/mman.h>
#include "syscall.h"

int __mprotect(void *addr, size_t len, int prot)
{
	return 0;
}

weak_alias(__mprotect, mprotect);
