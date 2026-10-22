/* every page of a wasm memory is resident (wasm_mman.c) */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "libc.h"
#include "syscall.h"

/*
 * One byte per page, as sysconf(_SC_PAGESIZE) has it: callers size vec by
 * it. Past the memory's end, none (ENOMEM), as of memory not mapped.
 */
int mincore(void *addr, size_t len, unsigned char *vec)
{
	unsigned long long end = __builtin_wasm_memory_size(0) * 65536ULL;

	if ((uintptr_t)addr % PAGE_SIZE)
		return __syscall_ret(-EINVAL);
	if ((uintptr_t)addr + (unsigned long long)len > end)
		return __syscall_ret(-ENOMEM);
	memset(vec, 1, len / PAGE_SIZE + !!(len % PAGE_SIZE));
	return 0;
}
