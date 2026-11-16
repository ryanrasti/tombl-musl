/*
 * the thread pointer: a plain global, which the syscall stub saves and
 * restores around syscalls, where threads switch (wasm_syscall.c), as it
 * does TLS's base
 */
extern uintptr_t __wasm_tp;

static inline uintptr_t __get_tp()
{
	return __wasm_tp;
}

/* TLS's base, this libc's global (src/thread/wasm32/wasm_thread.s) */
static inline uintptr_t __wasm_get_tls_base(void)
{
	return (uintptr_t)__builtin_wasm_tls_base();
}

static inline void __wasm_set_tls_base(uintptr_t base)
{
	__asm__ __volatile__ (".globaltype __tls_base, i32\n\t"
			      "local.get %0\n\tglobal.set __tls_base" : : "r"(base));
}

#define TLS_ABOVE_TP
#define GAP_ABOVE_TP 0

#define DTP_OFFSET 0

#define MC_PC gregs[0]
