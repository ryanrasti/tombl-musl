#include "process_args.h"
#include "syscall.h"

// The wasm process ABI caps the complete argument blob at four 64 KiB pages.
static char args_buf[65536 * 4];
hidden struct wasm_process_args *__wasm_process_args = (void*)args_buf;

#ifdef START_is_dlstart
hidden void _dlstart_c(size_t *sp, size_t *dynv);
hidden void _dlstart(void) {
	_dlstart_c(0, 0);
}
#endif

#ifdef START_is_start
void __wasm_call_ctors(void);
int __main_void(void);
hidden void _start_c(long *p);
#ifdef __PIC__
/*
 * A position independent program (Scrt1.o), linked with libc.so: libc's
 * internals are hidden there, so start as other ports do, through
 * __libc_start_main, which runs the libraries' constructors (the host's
 * loader queues them) and then this, the program's, then main.
 */
int __libc_start_main(int (*)(int, char **, char **), int, char **,
	void (*)(void), void (*)(void), void (*)(void));

/* clang names main after its signature (main(void) is __main_void, ...) */
weak int __main_void(void);
weak int __main_argc_argv(int, char **);
weak int main(int, char **, char **);

static int wasm_main(int argc, char **argv, char **envp)
{
	__wasm_call_ctors();
	/* (main(void) also defines a two-argument main: not ours to call) */
	if (__main_argc_argv)
		return __main_argc_argv(argc, argv);
	if (__main_void)
		return __main_void();
	return main(argc, argv, envp);
}

hidden void _start(void) {
	if (__syscall(SYS_wasm_get_args, __wasm_process_args,
	    sizeof(args_buf)) < 0) __builtin_trap();

	__libc_start_main(wasm_main, __wasm_process_args->argc,
		__wasm_process_args->argv, 0, 0, 0);
	__builtin_trap();
}
#else
hidden void _start(void) {
	if (__syscall(SYS_wasm_get_args, __wasm_process_args,
	    sizeof(args_buf)) < 0) __builtin_trap();

	__init_libc(__wasm_process_args->envp, __wasm_process_args->argv[0]);
	__libc_start_init();
	__wasm_call_ctors();

	exit(__main_void());
}
#endif
#endif
