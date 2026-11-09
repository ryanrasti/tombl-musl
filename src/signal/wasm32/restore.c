#define _GNU_SOURCE
#include <features.h>
#include <signal.h>
#include <stdint.h>
#include "pthread_impl.h"

__attribute__((import_module("linux"), import_name("copy_siginfo")))
int __wasm_copy_siginfo(siginfo_t *);

/* Signal delivery is a synchronous host callback, so there is no native
 * signal frame to restore. Instead, the kernel calls a restorer as the
 * userspace trampoline (for every handler: sigaction gives the kernel
 * SA_SIGINFO), which calls the application's handler. Each counts the
 * handlers the thread is in, for libc's coroutines, which can't switch
 * past the kernel's frames under a handler (src/ucontext/wasm32/ucontext.c). */
hidden void __restore(uintptr_t fn, int sig)
{
	pthread_t self = __pthread_self();
	siginfo_t info;

	/* (the kernel's: a trampoline that never asks for the info fails) */
	if (__wasm_copy_siginfo(&info)) return;

	self->sig_depth++;
	((void (*)(int))fn)(sig);
	self->sig_depth--;
}

/* It materializes the public objects before invoking the application's
 * three-argument handler. */
hidden void __restore_rt(uintptr_t fn, int sig)
{
	void (*handler)(int, siginfo_t *, void *) = (void *)fn;
	pthread_t self = __pthread_self();
	siginfo_t info;

	/* Register state is not representable for a callback made at the wasm
	 * syscall boundary. Keep the context valid and intentionally minimal;
	 * siginfo carries the source information applications depend on. */
	ucontext_t context = {0};

	if (__wasm_copy_siginfo(&info)) return;

	self->sig_depth++;
	handler(sig, &info, &context);
	self->sig_depth--;
}
