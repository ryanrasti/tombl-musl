/*
 * Syscalls go to the kernel linked into the program (a library OS on WASI:
 * its host's entry, which takes the stack and thread pointers and a
 * "program counter" too). Threads run while this one is in the kernel, and
 * the thread pointer is one global for them all: ours is restored after.
 * Memory is the kernel's too: mmap, brk and their relatives are its own.
 */
#include <stdint.h>
#include "syscall.h"

long __lkl_syscall_regs(long n, long a, long b, long c, long d, long e, long f,
			uintptr_t sp, uintptr_t tp, uintptr_t pc);

extern uintptr_t __wasm_tp;

long __wasm_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	uintptr_t tp = __wasm_tp;
	long ret = __lkl_syscall_regs(n, a, b, c, d, e, f, 0, tp, 0);

	__wasm_tp = tp;
	return ret;
}
