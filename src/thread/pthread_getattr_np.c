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
		/* The main thread's: a dynamic program's, the 1 MiB exec maps
		 * (STACK_SIZE, arch/wasm/exobox/elf.c), below where _dlstart
		 * found it starting; a static program's, its own, where its
		 * linker put it. */
#ifdef WASM_SHARED_LIBC
		extern hidden uintptr_t __wasm_main_stack;
		a->_a_stackaddr = __wasm_main_stack;
		a->_a_stacksize = 1 << 20;
#else
		extern char __stack_low[], __stack_high[];
		a->_a_stackaddr = (uintptr_t)__stack_high;
		a->_a_stacksize = __stack_high - __stack_low;
#endif
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
