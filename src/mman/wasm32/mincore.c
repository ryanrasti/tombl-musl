/* every page of a wasm memory is resident (wasm_mman.c) */
#define _DEFAULT_SOURCE
#include <string.h>
#include <sys/mman.h>
#include "syscall.h"

int mincore(void *addr, size_t len, unsigned char *vec)
{
	memset(vec, 1, (len + 4095) / 4096);
	return 0;
}
