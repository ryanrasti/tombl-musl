/*
 * ucontext's coroutines, on wasm, which can't switch stacks: a context not
 * running has its wasm frames saved by Asyncify (an unwind) into the free
 * part of its own stack, and a switch to it rewinds them, on top of the
 * frames of the context that switched, which stay where they are. One
 * thread runs them all, so each sees the thread's thread-locals, as
 * natively. (Emscripten's fibers save and rewind frames the same way, from
 * JavaScript; this is all in the wasm, through Binaryen's asyncify.*
 * imports, which its Asyncify pass makes calls of its own functions.)
 *
 * The contexts whose frames are on the wasm stack are a chain: the thread's
 * own at the bottom, then each one run on top of the one that switched to
 * it, waiting in swapcontext. A switch to a context that isn't on the stack
 * (made, or saved) runs it on top of the switching one (run); a switch to
 * one below on the stack saves each context above it (suspend), down to it,
 * and its swapcontext returns. So a generator (Boost's coroutines, Nix's
 * sources and sinks) saves and rewinds only its own frames, never its
 * caller's.
 *
 * What a program needs: Asyncify, instrumenting what can be on the stack
 * at a switch (the toolchain's asyncify-list.py: what can call a function
 * that calls asyncify.start_unwind), and not this runtime itself (suspend,
 * which starts an unwind and stops a rewind, and run, which stops an
 * unwind and starts a rewind: Binaryen leaves the functions calling
 * asyncify.* uninstrumented). run is called through a pointer, so that
 * Asyncify checks for an unwind after it: a fork's, which goes on down
 * through the contexts below to the host, and whose rewind comes back up
 * through them into the context that forked.
 *
 * Statically linked programs only: a dynamically linked process's modules
 * each keep Asyncify's state of their own, which a switch in libc.so
 * couldn't reach. A context switched to must have been made (makecontext)
 * or have switched away (swapcontext): one only got (getcontext) has no
 * frames saved to go back to, and switching to it fails (EINVAL).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>
#include "atomic.h"
#include "pthread_impl.h"
#include "syscall.h"

#ifndef WASM_SHARED_LIBC

__asm__(".globaltype __stack_pointer, i32\n");

static inline char *get_sp(void)
{
	char *sp;
	__asm__ volatile("global.get __stack_pointer\n"
			 "local.set %0" : "=r"(sp));
	return sp;
}

static inline void set_sp(char *sp)
{
	__asm__ volatile("local.get %0\n"
			 "global.set __stack_pointer" :: "r"(sp));
}

#define ASYNCIFY(name) __attribute__((import_module("asyncify"), import_name(#name)))
ASYNCIFY(start_unwind) void asyncify_start_unwind(void *);
ASYNCIFY(stop_unwind) void asyncify_stop_unwind(void);
ASYNCIFY(start_rewind) void asyncify_start_rewind(void *);
ASYNCIFY(stop_rewind) void asyncify_stop_rewind(void);

/* a context, in its uc_mcontext */
struct context {
	char *sp;		/* its stack pointer: where it starts, or as it was saved */
	char *next, *end;	/* Asyncify's header: its saved frames, from its stack's bottom up */
	char *stack;		/* its stack's bottom; 0, the thread's own, which is never saved */
	int state;
	void (*func)();
	int argc;
	int *args;		/* uc_link, then func's arguments, at its stack's top */
};
_Static_assert(sizeof(struct context) <= sizeof(mcontext_t), "a context fits uc_mcontext");
#define CONTEXT(uc) ((struct context *)&(uc)->uc_mcontext)

enum {
	INVALID,	/* none to switch to (getcontext's; or switched to, and so used) */
	MADE,		/* makecontext's, not yet run */
	SAVED,		/* its frames saved into its stack */
	WAITING,	/* its frames on the wasm stack, below the running context's */
};

#define MAX_ARGS 6

/* a made context's first frame: its function, then uc_link (none: the process exits) */
static __attribute__((noinline)) void start(ucontext_t *uc)
{
	struct context *c = CONTEXT(uc);
	int *a = c->args;
	ucontext_t *link = (ucontext_t *)a[0];
	pthread_t self;

	switch (c->argc) {
	case 0: c->func(); break;
	case 1: ((void (*)(int))c->func)(a[1]); break;
	case 2: ((void (*)(int, int))c->func)(a[1], a[2]); break;
	case 3: ((void (*)(int, int, int))c->func)(a[1], a[2], a[3]); break;
	case 4: ((void (*)(int, int, int, int))c->func)(a[1], a[2], a[3], a[4]); break;
	case 5: ((void (*)(int, int, int, int, int))c->func)(a[1], a[2], a[3], a[4], a[5]); break;
	case 6: ((void (*)(int, int, int, int, int, int))c->func)(a[1], a[2], a[3], a[4], a[5], a[6]); break;
	}
	if (!link)
		exit(0);
	self = __pthread_self();
	self->uc_next = link;
	self->uc_finished = 1;
}

/*
 * Runs t on top of o (waiting in swapcontext): starts it, or rewinds its
 * saved frames, until it switches to a context below it on the stack, or
 * returns; then the context it switched to (or its uc_link). Neither made
 * nor saved, t's frames are being rewound by the host (a fork's, through
 * o's frames): on into them. 0: they're unwinding for the host (a fork),
 * on down through o's.
 */
static __attribute__((noinline)) ucontext_t *run(ucontext_t *o, ucontext_t *t)
{
	pthread_t self = __pthread_self();
	struct context *c = CONTEXT(t);

	if (c->state == MADE || c->state == SAVED) {
		self->uc_stack = c->stack;
		set_sp(c->sp);
		if (c->state == SAVED) {
			self->uc_rewinding = 1;
			asyncify_start_rewind(&c->next);
		}
		c->state = INVALID;
	}
	start(t);
	if (self->uc_switching) {
		self->uc_switching = 0;
		asyncify_stop_unwind();
	} else if (self->uc_finished) {
		self->uc_finished = 0;
	} else {
		return 0;
	}
	self->uc_stack = CONTEXT(o)->stack;
	set_sp(CONTEXT(o)->sp);
	return self->uc_next;
}

/* (through a pointer, so that Asyncify checks for an unwind after it) */
static ucontext_t *(*volatile const run_indirect)(ucontext_t *, ucontext_t *) = run;

/*
 * Saves the running context, as o, into its stack, leaving it for t
 * (Asyncify's unwind, down to the run that ran it); returns as it's
 * rewound.
 */
static __attribute__((noinline)) void suspend(ucontext_t *o, ucontext_t *t)
{
	pthread_t self = __pthread_self();
	struct context *c = CONTEXT(o);

	if (self->uc_rewinding) {
		self->uc_rewinding = 0;
		asyncify_stop_rewind();
		return;
	}
	/* (the thread's own context is at the bottom: no context to switch to is below it) */
	if (!c->stack)
		a_crash();
	c->sp = get_sp();
	c->next = c->stack;
	c->end = c->sp;
	c->state = SAVED;
	self->uc_next = t;
	self->uc_switching = 1;
	asyncify_start_unwind(&c->next);
}

int getcontext(ucontext_t *uc)
{
	CONTEXT(uc)->state = INVALID;
	__syscall(SYS_rt_sigprocmask, SIG_BLOCK, 0, &uc->uc_sigmask, _NSIG / 8);
	return 0;
}

void makecontext(ucontext_t *uc, void (*func)(), int argc, ...)
{
	struct context *c = CONTEXT(uc);
	uintptr_t bottom = (uintptr_t)uc->uc_stack.ss_sp;
	uintptr_t top = (bottom + uc->uc_stack.ss_size) & -16;
	int *args = (int *)((top - (argc + 1) * sizeof(int)) & -16);
	va_list ap;

	if (argc < 0 || argc > MAX_ARGS) {
		c->state = INVALID;
		return;
	}
	args[0] = (int)uc->uc_link;
	va_start(ap, argc);
	for (int i = 0; i < argc; i++)
		args[i + 1] = va_arg(ap, int);
	va_end(ap);
	c->sp = (char *)args;
	c->stack = (char *)((bottom + 15) & -16);
	c->func = func;
	c->argc = argc;
	c->args = args;
	c->state = MADE;
}

int swapcontext(ucontext_t *restrict o, const ucontext_t *restrict uc)
{
	ucontext_t *t = (ucontext_t *)uc;
	struct context *c = CONTEXT(o);
	int state = CONTEXT(t)->state;

	if (state != MADE && state != SAVED && state != WAITING) {
		errno = EINVAL;
		return -1;
	}
	__syscall(SYS_rt_sigprocmask, SIG_SETMASK, &uc->uc_sigmask, &o->uc_sigmask, _NSIG / 8);
	c->sp = get_sp();
	c->stack = __pthread_self()->uc_stack;
	c->state = WAITING;
	while (t != o) {
		state = CONTEXT(t)->state;
		if (state == WAITING) {
			suspend(o, t);
			break;
		}
		/* (uc_link, or a context switched to from one run here, used or never made) */
		if (state != MADE && state != SAVED)
			a_crash();
		t = run_indirect(o, t);
	}
	c->state = INVALID;
	return 0;
}

int setcontext(const ucontext_t *uc)
{
	/* the running context's, never switched to again */
	ucontext_t left;

	return swapcontext(&left, uc);
}

#endif
