/* in libc: wasm_mman.c */
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include "libc.h"
#include "syscall.h"
#include "wasm_mman.h"

int __munmap(void *start, size_t len)
{
	if ((uintptr_t)start % PAGE_SIZE || !len)
		return __syscall_ret(-EINVAL);
	return __syscall_ret(__wasm_munmap((long)start, len));
}

weak_alias(__munmap, munmap);
