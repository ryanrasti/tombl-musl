/*
 * A detached thread exiting: generic musl switches to a static stack to
 * unmap its own (CRTJMP, which wasm can't). Ours stays on it: the kernel's
 * munmap frees it, but nothing after uses its memory. This function's and
 * the syscall stub's frames are wasm locals, not on the stack, and the
 * kernel's part runs on the thread's kernel stack; a switch at munmap's
 * return saves the frames in Asyncify's memory. The exit syscall never
 * returns.
 */
#include "pthread_impl.h"
#include "syscall.h"

void __unmapself(void *base, size_t size)
{
	__syscall(SYS_munmap, base, size);
	for (;;)
		__syscall(SYS_exit, 0);
}
