/* in libc: wasm_mman.c */
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include "syscall.h"
#include "wasm_mman.h"

void *__mmap(void *start, size_t len, int prot, int flags, int fd, off_t off)
{
	if (off & 4095) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return (void *)__syscall_ret(__wasm_mmap((long)start, len, prot, flags, fd, off));
}

weak_alias(__mmap, mmap);
