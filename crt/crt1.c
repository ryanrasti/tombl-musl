#include <features.h>
#include "libc.h"

#define START "_start"

#include "crt_arch.h"

int main();
weak void _init();
weak void _fini();
int __libc_start_main(int (*)(), int, char **,
	void (*)(), void(*)(), void(*)());

#ifdef __wasm__
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
#endif

hidden void _start_c(long *p)
{
	int argc = p[0];
	char **argv = (void *)(p+1);
	__libc_start_main(main, argc, argv, _init, _fini, 0);
}
