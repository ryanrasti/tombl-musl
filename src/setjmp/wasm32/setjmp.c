/*
 * setjmp and longjmp by the compiler's lowering (clang -mllvm
 * -enable-emscripten-sjlj; its C++ exceptions, -enable-emscripten-cxx-exceptions,
 * share __THREW__ and the wrappers). A function that calls setjmp makes
 * each of its calls through an invoke_<signature> wrapper (the host's, made
 * at link), which stops a longjmp's unwinding there and sets __THREW__; the
 * function then tests whether the buffer is one of its own setjmps
 * (__wasm_setjmp_test) and resumes there, or longjmps on. The unwinding is
 * the host's (lkl_wasi_throw: Asyncify, as a thread's suspend is).
 */
#include <setjmp.h>
#include "libc.h"
#include <stdint.h>

/*
 * what a wrapper reports: 0, the buffer longjmped to, or 1 (an exception);
 * each thread's (the lowering's, in code with the atomics feature: arch.mak)
 */
_Thread_local uintptr_t __THREW__;
_Thread_local int __threwValue;
static int temp_ret;

void lkl_wasi_throw(void);

/*
 * setjmp's and _setjmp's own functions (an address taken, or a call the
 * lowering didn't see, which can't return twice): a trap
 */
int setjmp(jmp_buf env)
{
	__builtin_trap();
}

int (_setjmp)(jmp_buf env)
{
	__builtin_trap();
}

/* a buffer: the setjmp's invocation of its function, and its label there */
struct jmp {
	void *id;
	uint32_t label;
};

void __wasm_setjmp(void *env, uint32_t label, void *id)
{
	struct jmp *j = env;

	j->id = id;
	j->label = label;
}

uint32_t __wasm_setjmp_test(void *env, void *id)
{
	struct jmp *j = env;

	return j->id == id ? j->label : 0;
}

int getTempRet0(void)
{
	return temp_ret;
}

void setTempRet0(int v)
{
	temp_ret = v;
}

/* a longjmp: it returns into the unwinding Asyncify started */
hidden void __wasm_longjmp_to(uintptr_t env, int val)
{
	__THREW__ = env;
	__threwValue = val ? val : 1;
	lkl_wasi_throw();
}

/* (the lowering's longjmp) */
void emscripten_longjmp(uintptr_t env, int val)
{
	__wasm_longjmp_to(env, val);
}

/*
 * a C++ exception that unwinds past a function that calls setjmp: it
 * resumes the unwinding (the lowering's, compiling both). C++'s runtime's,
 * if linked; without it there are no exceptions, and these aren't called.
 */
weak void *__cxa_find_matching_catch_2(void)
{
	return 0;
}

weak void __resumeException(void *exception)
{
	lkl_wasi_throw();
}
