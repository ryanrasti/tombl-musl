/*
 * vfork: a fork (fork_syscall.c), as upstream's is a clone without the
 * atfork handlers. The child's memory is a copy, which is all vfork's
 * child may rely on.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include "syscall.h"

hidden long __wasm_fork(void);

pid_t vfork(void)
{
	return __syscall_ret(__wasm_fork());
}
