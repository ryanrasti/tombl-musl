/* as upstream, writing back writable shared mappings first (src/mman/wasm32/wasm_mman.c) */
#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __wasm_shared_flush);

int fexecve(int fd, char *const argv[], char *const envp[])
{
	__wasm_shared_flush();
	int r = __syscall(SYS_execveat, fd, "", argv, envp, AT_EMPTY_PATH);
	if (r != -ENOSYS) return __syscall_ret(r);
	char buf[15 + 3*sizeof(int)];
	__procfdname(buf, fd);
	execve(buf, argv, envp);
	if (errno == ENOENT) errno = EBADF;
	return -1;
}
