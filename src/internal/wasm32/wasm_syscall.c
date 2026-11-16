/*
 * Syscalls go to the kernel linked into the program (a library OS on WASI:
 * its host's entry, which takes the stack and thread pointers and a
 * "program counter" too: 0, or a cancellation point's, syscall_cp.c).
 * Threads run while this one is in the kernel, and the thread pointer is
 * one global for them all, as is TLS's base: ours are restored after.
 * Memory is the kernel's too: mmap, brk and their relatives are its own.
 */
#include <stdint.h>
#include "pthread_impl.h"
#include "syscall.h"

long __lkl_syscall_regs(long n, long a, long b, long c, long d, long e, long f,
			uintptr_t sp, uintptr_t tp, uintptr_t pc);

hidden long __wasm_syscall_at(uintptr_t pc, long n, long a, long b, long c,
			      long d, long e, long f)
{
	uintptr_t tp = __wasm_tp, tls = __wasm_get_tls_base();
	long ret = __lkl_syscall_regs(n, a, b, c, d, e, f, 0, tp, pc);

	__wasm_tp = tp;
	__wasm_set_tls_base(tls);
	return ret;
}

long __wasm_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	return __wasm_syscall_at(0, n, a, b, c, d, e, f);
}
