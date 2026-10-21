/* in libc: wasm_mman.c */
#include <sys/mman.h>
#include "syscall.h"
#include "wasm_mman.h"

int msync(void *start, size_t len, int flags)
{
	return __syscall_ret(__wasm_msync((long)start, len));
}
