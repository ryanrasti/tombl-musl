/* as upstream, writing back writable shared mappings first (src/mman/wasm32/wasm_mman.c) */
#include <unistd.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __wasm_shared_flush);

int execve(const char *path, char *const argv[], char *const envp[])
{
	__wasm_shared_flush();
	return syscall(SYS_execve, path, argv, envp);
}
