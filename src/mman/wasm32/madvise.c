/* in libc: wasm_mman.c (MADV_DONTNEED zeroes private anonymous memory; the rest, a wasm memory has no paging for) */
#include <sys/mman.h>
#include "syscall.h"
#include "wasm_mman.h"

int __madvise(void *addr, size_t len, int advice)
{
	return __syscall_ret(__wasm_madvise((long)addr, len, advice));
}

weak_alias(__madvise, madvise);
