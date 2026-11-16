/*
 * Cancellable syscalls: a cancel is acted on before the syscall starts.
 * (Signals reach wasm code only around syscalls, and no handler can move a
 * thread out of one: the cancellation points' bounds are empty.)
 */
#include "pthread_impl.h"
#include "syscall.h"

hidden long __cancel(void);

hidden const char __cp_begin[1], __cp_end[1], __cp_cancel[1];

hidden long __syscall_cp_asm(volatile int *cancel, syscall_arg_t nr,
			     syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
			     syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	if (*cancel)
		return __cancel();
	return __syscall(nr, u, v, w, x, y, z);
}
