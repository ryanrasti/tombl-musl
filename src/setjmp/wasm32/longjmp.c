#include <setjmp.h>
#include <stdint.h>

void emscripten_longjmp(uintptr_t env, int val);

/* (for a caller the lowering didn't see: a call to it is emscripten_longjmp) */
_Noreturn void longjmp(jmp_buf env, int val)
{
	emscripten_longjmp((uintptr_t)env, val);
	__builtin_unreachable();
}

weak_alias(longjmp, _longjmp);
