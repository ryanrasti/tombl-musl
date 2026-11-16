/*
 * no assembly entry point: the host calls _start_c (crt/crt1.c) directly.
 * Its main and _init (not the dynamic linker's start, which has neither):
 */
#ifndef SHARED
/*
 * clang names main after its signature on wasm: main(void) is __main_void,
 * main(argc, argv) is __main_argc_argv, main(argc, argv, envp) stays main.
 */
weak int __main_void(void);
weak int __main_argc_argv(int, char **);
weak int main(int, char **, char **);

static int wasm_main(int argc, char **argv, char **envp)
{
	if (__main_void)
		return __main_void();
	if (__main_argc_argv)
		return __main_argc_argv(argc, argv);
	return main(argc, argv, envp);
}
#define main wasm_main

/*
 * the constructors (wasm-ld's __wasm_call_ctors) as libc's init runs them,
 * after the thread pointer is set; called here, the linker doesn't run them
 * before each export instead (as the host's cabi_realloc, before _start)
 */
void __wasm_call_ctors(void);

weak void _init(void)
{
	__wasm_call_ctors();
}
#endif
