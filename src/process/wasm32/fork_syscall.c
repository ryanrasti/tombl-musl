/*
 * fork: clone with neither a function nor CLONE_VM returns twice, in the
 * parent and in the child, whose memory is a copy (exobox's kernel and
 * host: arch/wasm/exobox/fork.c, linux-workers' src/kernel/user/fork.ts).
 * A wasm stack can't be copied, so the program is Asyncify-instrumented
 * and saves its stack into its memory, into this buffer, which the host
 * finds by its export: Asyncify's header (where saved frames go next, and
 * the buffer's end), then where it starts, for the host to reset next to.
 * Each thread's own (thread-local): one thread's fork, or a vfork parent's
 * stack saved as its child runs, isn't another's to overwrite. Only
 * programs that can fork link it.
 */
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include "syscall.h"

#define FORK_BUFFER_SIZE (128 * 1024)

static __thread char fork_stack[FORK_BUFFER_SIZE] __attribute__((aligned(16)));
static __thread struct {
	char *next, *end, *start;
} fork_buffer;

/* (set up at the first call: a thread-local's address isn't a constant initializer) */
__attribute__((export_name("__wasm_fork_buffer")))
void *__wasm_fork_buffer(void)
{
	if (!fork_buffer.start) {
		fork_buffer.start = fork_buffer.next = fork_stack;
		fork_buffer.end = fork_stack + FORK_BUFFER_SIZE;
	}
	return &fork_buffer;
}

/* clone(fn, fn_arg, flags, ptid, ctid, tls) with no fn: fork */
hidden long __wasm_fork(void)
{
	return __syscall(SYS_clone, 0, 0, SIGCHLD, 0, 0, 0);
}

/*
 * with CLONE_VM | CLONE_VFORK: vfork, the child on this memory until it
 * execs or exits. (Nothing between vfork's caller and the syscall keeps
 * anything on the stack in memory, which the child's own calls overwrite
 * before the parent returns through them.)
 */
hidden long __wasm_vfork(void)
{
	return __syscall(SYS_clone, 0, 0, CLONE_VM | CLONE_VFORK | SIGCHLD, 0, 0, 0);
}
