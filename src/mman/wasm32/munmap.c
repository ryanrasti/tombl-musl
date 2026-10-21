/* in libc: wasm_mman.c */
#include <sys/mman.h>
#include "syscall.h"
#include "wasm_mman.h"

int __munmap(void *start, size_t len)
{
	return __syscall_ret(__wasm_munmap((long)start, len));
}

weak_alias(__munmap, munmap);
