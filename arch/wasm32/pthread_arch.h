/*
 * the thread pointer: a plain global, which the syscall stub saves and
 * restores around syscalls, where threads switch (wasm_syscall.c)
 */
extern uintptr_t __wasm_tp;

static inline uintptr_t __get_tp()
{
	return __wasm_tp;
}

#define TLS_ABOVE_TP
#define GAP_ABOVE_TP 0

#define DTP_OFFSET 0

#define MC_PC gregs[0]
