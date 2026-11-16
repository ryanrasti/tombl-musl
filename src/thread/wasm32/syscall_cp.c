/*
 * Cancellable syscalls: a cancel is acted on before the syscall starts, or
 * while it waits. Signals reach wasm code only at a syscall's return (the
 * host calls the handler), and a cancellation point's syscall gives the
 * host __cp_begin as its program counter, which a handler's ucontext has
 * while the syscall is to run again (a restart, SA_RESTART's: the
 * cancellation signal's). cancel_handler then moves it to __cp_cancel, and
 * the syscall ends with -EINTR instead, which __syscall_cp_c takes for the
 * cancel. The labels are bytes of one object, in order (wasm_thread.s):
 * [__cp_begin, __cp_end) holds __cp_begin alone.
 */
#include <stdint.h>
#include "pthread_impl.h"
#include "syscall.h"

hidden long __cancel(void);
hidden long __wasm_syscall_at(uintptr_t pc, long n, long a, long b, long c,
			      long d, long e, long f);

extern hidden const char __cp_begin[1];

hidden long __syscall_cp_asm(volatile int *cancel, syscall_arg_t nr,
			     syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
			     syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	if (*cancel)
		return __cancel();
	return __wasm_syscall_at((uintptr_t)__cp_begin, nr, u, v, w, x, y, z);
}
