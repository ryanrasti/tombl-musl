#include <setjmp.h>
#include <signal.h>
#include "syscall.h"

/*
 * sigsetjmp(env, save) is __wasm_sigsetjmp(env, save, setjmp(env))
 * (<setjmp.h>: setjmp is the compiler's): the mask saved on setjmp's first
 * return, restored on a return by longjmp
 */
int __wasm_sigsetjmp(sigjmp_buf env, int save, int ret)
{
	if (!ret)
		env->__fl = save;
	if (env->__fl)
		__syscall(SYS_rt_sigprocmask, SIG_SETMASK, ret ? env->__ss : 0,
			  ret ? 0 : env->__ss, _NSIG/8);
	return ret;
}
