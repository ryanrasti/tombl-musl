/*
 * A detached thread exiting: generic musl switches to a static stack to
 * unmap its own (CRTJMP, which wasm can't). Ours can stay on it: munmap
 * only lists the range for reuse (wasm_syscall.c), and nothing else runs
 * until the exit syscall, after which this thread's code never runs again.
 */
#include "pthread_impl.h"
#include "syscall.h"

void __unmapself(void *base, size_t size)
{
	__syscall(SYS_munmap, base, size);
	for (;;)
		__syscall(SYS_exit, 0);
}
