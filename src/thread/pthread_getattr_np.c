#define _GNU_SOURCE
#include "pthread_impl.h"
#include "libc.h"
#include <sys/mman.h>

int pthread_getattr_np(pthread_t t, pthread_attr_t *a)
{
	*a = (pthread_attr_t){0};
	a->_a_detach = t->detach_state>=DT_DETACHED;
	a->_a_guardsize = t->guard_size;
	if (t->stack) {
		a->_a_stackaddr = (uintptr_t)t->stack;
		a->_a_stacksize = t->stack_size;
	} else {
#ifdef __wasm__
		/* The main thread's: 1 MiB (the toolchain's -z stack-size, or
		 * the host's loader's for a dynamic program) below its top.
		 * __init_libc saw the stack a little below the top (_start's
		 * frame and __libc_start_main's above it, main's at the top
		 * again once it returned): the top is put 4 KiB above that,
		 * and the size short by 12 KiB, so both ends are inside. */
		extern hidden uintptr_t __wasm_main_stack;
		a->_a_stackaddr = (__wasm_main_stack + 4096) & -16;
		a->_a_stacksize = (1 << 20) - 3 * 4096;
#else
		// char *p = (void *)libc.auxv;
		// size_t l = PAGE_SIZE;
		// p += -(uintptr_t)p & PAGE_SIZE-1;
		// a->_a_stackaddr = (uintptr_t)p;
		// while (mremap(p-l-PAGE_SIZE, PAGE_SIZE, 2*PAGE_SIZE, 0)==MAP_FAILED && errno==ENOMEM)
		// 	l += PAGE_SIZE;
		// a->_a_stacksize = l;
		a_crash();
#endif
	}
	return 0;
}
