/*
 * Syscalls go to the kernel linked into the program (a library OS on WASI:
 * its host's entry, which takes the stack and thread pointers and a
 * "program counter" too). Threads run while this one is in the kernel, and
 * the thread pointer is one global for them all: ours is restored after.
 *
 * Memory is mapped here, from memory.grow: the kernel (no MMU) would give
 * mappings from its own memory, a fixed size, each contiguous in its page
 * allocator (at most 4 MiB: not a 4 MiB thread stack and its guard).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "syscall.h"

long __lkl_syscall_regs(long n, long a, long b, long c, long d, long e, long f,
			uintptr_t sp, uintptr_t tp, uintptr_t pc);

extern uintptr_t __wasm_tp;

static long kernel(long n, long a, long b, long c, long d, long e, long f)
{
	uintptr_t tp = __wasm_tp;
	long ret = __lkl_syscall_regs(n, a, b, c, d, e, f, 0, tp, 0);

	__wasm_tp = tp;
	return ret;
}

#define WASM_PAGE 65536
#define MMAP_ALIGN 4096

/*
 * mmap: munmapped ranges first (first fit), on a list kept in the ranges
 * themselves, else a bump allocator over memory.grow. Ranges aren't merged.
 * Threads switch only at syscalls, so none runs in here meanwhile.
 */
struct free_range {
	struct free_range *next;
	size_t len;
};
static struct free_range *free_ranges;
static uintptr_t mm_cur, mm_end;

static uintptr_t mm_reuse(size_t len)
{
	struct free_range **p, *r;
	uintptr_t a;

	for (p = &free_ranges; (r = *p); p = &r->next) {
		if (r->len < len)
			continue;
		if (r->len == len) {
			*p = r->next;
			a = (uintptr_t)r;
		} else {
			/* the top of it; the rest stays listed where it is */
			r->len -= len;
			a = (uintptr_t)r + r->len;
		}
		memset((void *)a, 0, len);	/* as fresh memory is */
		return a;
	}
	return 0;
}

static void mm_free(uintptr_t addr, size_t len)
{
	struct free_range *r = (void *)addr;

	len = (len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
	if (!len || addr % MMAP_ALIGN)
		return;
	r->len = len;
	r->next = free_ranges;
	free_ranges = r;
}

static long mm_alloc(size_t len)
{
	uintptr_t p;

	len = (len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
	if ((p = mm_reuse(len)))
		return p;
	if (mm_end - mm_cur < len) {
		size_t pages = (len + WASM_PAGE - 1) / WASM_PAGE;
		size_t old = __builtin_wasm_memory_grow(0, pages);

		if (old == (size_t)-1)
			return -ENOMEM;
		if (old * WASM_PAGE != mm_end)
			mm_cur = old * WASM_PAGE;
		mm_end = (old + pages) * WASM_PAGE;
	}
	p = mm_cur;
	mm_cur += len;
	return p;
}

/* a file mapping is a copy of the file (none is written back: no MAP_SHARED writes) */
static long wasm_mmap(long addr, long len, long prot, long flags, long fd,
		      long pgoff)
{
	long p, n;

	if (flags & MAP_FIXED)
		return -ENOMEM;
	if (!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (prot & PROT_WRITE))
		return -ENODEV;
	p = mm_alloc(len);
	if (p < 0 || (flags & MAP_ANONYMOUS))
		return p;	/* zeroed */
	n = kernel(SYS_pread64, fd, p, len, __SYSCALL_LL_E((long long)pgoff * 4096), 0);
	if (n < 0) {
		mm_free(p, len);
		return n;
	}
	return p;
}

static long wasm_mremap(long old, long old_len, long new_len, long flags)
{
	long p;

	if (new_len <= old_len)
		return old;
	if (!(flags & MREMAP_MAYMOVE))
		return -ENOMEM;
	p = mm_alloc(new_len);
	if (p >= 0) {
		memcpy((void *)p, (void *)old, old_len);
		mm_free(old, old_len);
	}
	return p;
}

long __wasm_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	switch (n) {
	case SYS_brk:
		return 0;	/* no brk: malloc maps */
	case SYS_mmap2:
		return wasm_mmap(a, b, c, d, e, f);
	case SYS_mremap:
		return wasm_mremap(a, b, c, d);
	case SYS_munmap:
		mm_free(a, b);
		return 0;
	case SYS_mprotect:
	case SYS_madvise:
	case SYS_msync:
		return 0;
	default:
		return kernel(n, a, b, c, d, e, f);
	}
}
