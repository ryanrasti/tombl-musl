/* in libc: wasm_mman.c (no MREMAP_FIXED: nothing is at a fixed address) */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include "libc.h"
#include "syscall.h"
#include "wasm_mman.h"

void *__mremap(void *old_addr, size_t old_len, size_t new_len, int flags, ...)
{
	if (new_len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if ((flags & MREMAP_FIXED) || !new_len || (uintptr_t)old_addr % PAGE_SIZE) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	return (void *)__syscall_ret(__wasm_mremap((long)old_addr, old_len, new_len, flags));
}

weak_alias(__mremap, mremap);
