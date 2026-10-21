/* as upstream: a process's mapped changes outlive it (src/mman/wasm32/wasm_mman.c) */
#include <stdlib.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __wasm_shared_flush);

_Noreturn void _Exit(int ec)
{
	__wasm_shared_flush();
	__syscall(SYS_exit_group, ec);
	for (;;) __syscall(SYS_exit, ec);
}
