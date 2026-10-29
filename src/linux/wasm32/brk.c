/*
 * brk and sbrk, by the kernel's break (exobox's: it keeps a process's
 * memory). The memory is grown by others too (the host's dynamic loader:
 * libraries, the stack, thread-local storage), so where the break can't
 * grow from where it is, the kernel starts it again at the memory's end:
 * a break moved by inc is then not where it was plus inc, and sbrk's
 * memory is the new break less inc (sbrk's regions are its callers',
 * malloc's, not one contiguous heap). On failure the kernel's brk returns
 * the break as it was.
 */
#define _BSD_SOURCE
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include "pthread_impl.h"
#include "syscall.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int brk(void *end)
{
	return __syscall(SYS_brk, end) == (long)end ? 0 : __syscall_ret(-ENOMEM);
}

void *sbrk(intptr_t inc)
{
	uintptr_t old, now;

	if (inc < 0)
		return (void *)__syscall_ret(-EINVAL);
	if (pthread_mutex_lock(&mutex) != 0)
		a_crash();
	old = __syscall(SYS_brk, 0);
	if (inc) {
		/* (past the address space: no memory, not a wrapped break) */
		now = (uintptr_t)inc <= UINTPTR_MAX - old ? __syscall(SYS_brk, old + inc) : old;
		old = now == old ? (uintptr_t)-1 : now - inc;
	}
	pthread_mutex_unlock(&mutex);
	if (old == (uintptr_t)-1)
		errno = ENOMEM;
	return (void *)old;
}
