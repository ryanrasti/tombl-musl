/* every page of a wasm memory is resident (wasm_mman.c) */
#define _DEFAULT_SOURCE
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include "syscall.h"

/*
 * One byte per page, as sysconf(_SC_PAGESIZE) (PAGESIZE, a wasm page) has
 * it: callers size vec by it. (Not by mmap's 4 KiB alignment: that wrote
 * 16 times past their vector.)
 */
int mincore(void *addr, size_t len, unsigned char *vec)
{
	memset(vec, 1, len / PAGESIZE + !!(len % PAGESIZE));
	return 0;
}
