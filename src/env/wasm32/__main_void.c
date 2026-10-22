#include <features.h>

/* (a shared build: Scrt1.o calls main itself, by its signature) */
#ifndef WASM_SHARED_LIBC
#include "process_args.h"

int __main_argc_argv(int, char **);

weak int __main_void(void)
{
	return __main_argc_argv(__wasm_process_args->argc,
		__wasm_process_args->argv);
}
#endif
