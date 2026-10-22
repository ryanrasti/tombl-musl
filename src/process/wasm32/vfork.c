/*
 * vfork: clone with CLONE_VM | CLONE_VFORK and no function, which returns
 * twice as fork's does, but the child runs on this memory, not a copy, the
 * parent waiting until it execs or exits (fork_syscall.c; exobox's kernel:
 * arch/wasm/exobox/fork.c). As anywhere, the child may only exec or
 * _exit: what it writes is the parent's.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include "syscall.h"

hidden long __wasm_vfork(void);

pid_t vfork(void)
{
	return __syscall_ret(__wasm_vfork());
}
