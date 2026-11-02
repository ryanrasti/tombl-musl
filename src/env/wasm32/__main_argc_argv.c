#include <features.h>

/* (a shared build: Scrt1.o calls main itself, by its signature) */
#ifndef WASM_SHARED_LIBC
#include "process_args.h"

int __main_argc_argv_envp(int, char **, char **);

weak int __main_argc_argv(int argc, char **argv)
{
	return __main_argc_argv_envp(argc, argv, __wasm_process_args->envp);
}
#endif
