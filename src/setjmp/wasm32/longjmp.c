#include "libc.h"
#include <setjmp.h>
#include <stdint.h>

/*
 * (not by emscripten_longjmp's name, and no alias: the lowering, compiling
 * this file, declares that name itself and takes the alias for a call)
 */
hidden void __wasm_longjmp_to(uintptr_t env, int val);

/* (for a caller the lowering didn't see: a call to it is emscripten_longjmp) */
_Noreturn void longjmp(jmp_buf env, int val)
{
	__wasm_longjmp_to((uintptr_t)env, val);
	__builtin_unreachable();
}

_Noreturn void _longjmp(jmp_buf env, int val)
{
	__wasm_longjmp_to((uintptr_t)env, val);
	__builtin_unreachable();
}
