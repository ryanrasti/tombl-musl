/* in libc: wasm_mman.c */
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include "libc.h"
#include "syscall.h"
#include "wasm_mman.h"

int __munmap(void *start, size_t len)
{
	/* (its end, rounded up to a page, past the address space: EINVAL, as a length rounding to 0) */
	unsigned long long end = (uintptr_t)start + (((unsigned long long)len + PAGE_SIZE - 1) & -(unsigned long long)PAGE_SIZE);

	if ((uintptr_t)start % PAGE_SIZE || !len || end > UINTPTR_MAX + 1ULL)
		return __syscall_ret(-EINVAL);
	return __syscall_ret(__wasm_munmap((long)start, len));
}

weak_alias(__munmap, munmap);
