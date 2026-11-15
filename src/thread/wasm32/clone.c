/*
 * clone on wasm32: a wasm thread can't return from a syscall on a new
 * stack, so a CLONE_VM child (a thread) gets what it should run at the top
 * of its new stack, passed as the new stack pointer. Its first word is where
 * the child starts, as a native child returns into its libc's clone: this
 * libc's __wasm_clone_start (wasm_thread.s), which the host calls with that
 * stack, and which sets the stack and thread pointers and runs it (a module
 * can link several programs, each with a libc of its own). Without CLONE_VM the kernel refuses (no copies of a
 * program's memory).
 */
#define _GNU_SOURCE
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include "pthread_impl.h"
#include "syscall.h"

void __wasm_clone_start(uintptr_t stack);

struct clone_start {
	void (*start)(uintptr_t stack);	/* first: the host calls it */
	int (*func)(void *);
	void *arg;
	void *tls;
	int flags;
};

int __clone(int (*func)(void *), void *stack, int flags, void *arg, ...)
{
	struct clone_start *s;
	pid_t *ptid = 0, *ctid = 0;
	void *tls = 0;
	va_list ap;

	/* callers pass these only with the flags that use them */
	va_start(ap, arg);
	if (flags & (CLONE_PARENT_SETTID | CLONE_SETTLS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) {
		ptid = va_arg(ap, pid_t *);
		tls = va_arg(ap, void *);
		ctid = va_arg(ap, pid_t *);
	}
	va_end(ap);

	s = (void *)(((uintptr_t)stack - sizeof *s) & -16);
	s->start = __wasm_clone_start;
	s->func = func;
	s->arg = arg;
	s->tls = tls;
	s->flags = flags;
	/* (the generic order: tls last) the child's frames go below s */
	return __syscall(SYS_clone, flags, s, ptid, ctid, tls);
}

/* from __wasm_clone_start, on the child's stack */
hidden _Noreturn void __wasm_clone_main(struct clone_start *s)
{
	extern uintptr_t __wasm_tp;

	if (s->flags & CLONE_SETTLS)
		__wasm_tp = (uintptr_t)s->tls;
	int ret = s->func(s->arg);
	for (;;)
		__syscall(SYS_exit, ret);
}
