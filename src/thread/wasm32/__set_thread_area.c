/*
 * the thread pointer, and the thread's TLS: its block above the thread
 * pointer, as __copy_tls lays it out, the image's variables at their
 * addresses in it less the image's (the compiler's TLS adds __tls_base to
 * a variable's address: wasm_thread.s). The host sets them so for a signal
 * handler, by this (a blob's program's, renamed: scripts/program.sh).
 */
#include <stdint.h>
#include "pthread_impl.h"

uintptr_t __wasm_tp;

int __set_thread_area(void *p)
{
	struct tls_module *m = libc.tls_head;

	__wasm_tp = (uintptr_t)p;
	if (m)
		__wasm_set_tls_base((uintptr_t)p + m->offset - (uintptr_t)m->image);
	return 0;
}
