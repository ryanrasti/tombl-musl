/*
 * brk and sbrk on a wasm memory, which others may grow too: the host's
 * dynamic loader (libraries, the stack, thread-local storage) grows a
 * dynamically linked process's memory, and only memory.grow's result says
 * where new memory starts. So sbrk grows from the break while what it
 * grew last is still the memory's end, and otherwise starts afresh at the
 * memory's end (the break moves there: sbrk's regions are its callers',
 * malloc's and mmap's, not one contiguous heap). brk can only grow, within
 * what sbrk has. (tombl's port assumed the memory's growth was its own.)
 */
#define _BSD_SOURCE
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include "pthread_impl.h"
#include "syscall.h"

extern void __heap_end; /* the end of the program's data: the first break */

static uintptr_t current, limit;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* grow to cover [current, end): 0, or -1 (ENOMEM) */
static int grow_to(uintptr_t end)
{
	uintptr_t size = __builtin_wasm_memory_size(0) * PAGESIZE;
	size_t pages, old;

	if (!current) {
		current = (uintptr_t)&__heap_end;
		limit = size;
	}
	if (limit != size) {
		/* grown by someone else since: what's past limit is theirs */
		uintptr_t inc = end - current;
		current = size;
		end = size + inc;
		limit = size;
	}
	if (end <= limit)
		return 0;
	pages = (end - limit + PAGESIZE - 1) / PAGESIZE;
	old = __builtin_wasm_memory_grow(0, pages);
	if (old == SIZE_MAX)
		return -1;
	if (old * PAGESIZE != limit) {
		/* (grown by another thread's module meanwhile: start at what we got) */
		end = old * PAGESIZE + (end - current);
		current = old * PAGESIZE;
	}
	limit = (old + pages) * PAGESIZE;
	return 0;
}

int brk(void *end)
{
	int ret = -1;

	if (pthread_mutex_lock(&mutex) != 0)
		a_crash();
	if (!current)
		grow_to((uintptr_t)&__heap_end);
	if ((uintptr_t)end >= current && (uintptr_t)end <= limit &&
	    limit == __builtin_wasm_memory_size(0) * PAGESIZE) {
		current = (uintptr_t)end;
		ret = 0;
	}
	pthread_mutex_unlock(&mutex);
	if (ret)
		errno = ENOMEM;
	return ret;
}

void *sbrk(intptr_t inc)
{
	void *old = (void *)-1;

	if (inc < 0)
		return (void *)__syscall_ret(-EINVAL);
	if (pthread_mutex_lock(&mutex) != 0)
		a_crash();
	if (grow_to((current ? current : (uintptr_t)&__heap_end) + inc) == 0) {
		old = (void *)current;
		current += inc;
	}
	pthread_mutex_unlock(&mutex);
	if (old == (void *)-1)
		errno = ENOMEM;
	return old;
}
