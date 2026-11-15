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
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

/*
 * A writable shared file mapping: a copy, written back to the file (what of
 * it is within the file) at msync and munmap, through a duplicate of its
 * descriptor. The program is one process, so the copy is the file's one
 * mapping as long as it maps a range once (SQLite's WAL index, its -shm
 * file's regions, does); a change the file gets meanwhile by write isn't
 * seen in it, nor is one at exit written back without munmap.
 */
struct shared_map {
	struct shared_map *next;
	uintptr_t addr;
	size_t len;
	long fd;
	long long off;
};
static struct shared_map *shared_maps, *spare_maps;

static struct shared_map *shared_map_new(void)
{
	struct shared_map *m;
	long p;
	size_t i;

	if (!spare_maps) {
		if ((p = mm_alloc(MMAP_ALIGN)) < 0)
			return 0;
		for (i = 0; i < MMAP_ALIGN / sizeof *m; i++) {
			m = (struct shared_map *)p + i;
			m->next = spare_maps;
			spare_maps = m;
		}
	}
	m = spare_maps;
	spare_maps = m->next;
	return m;
}

/* the part of [addr, addr + len) that's m's, written back */
static long shared_map_sync(struct shared_map *m, uintptr_t addr, size_t len)
{
	uintptr_t a = addr > m->addr ? addr : m->addr;
	uintptr_t end = addr + len < m->addr + m->len ? addr + len : m->addr + m->len;
	long long off = m->off + (a - m->addr), n;
	struct statx stx;
	long r;

	if (a >= end)
		return 0;
	r = kernel(SYS_statx, m->fd, (long)"", AT_EMPTY_PATH, STATX_SIZE, (long)&stx, 0);
	if (r < 0)
		return r;
	if (off >= (long long)stx.stx_size)
		return 0;
	n = end - a;
	if (n > (long long)stx.stx_size - off)
		n = stx.stx_size - off;
	while (n > 0) {
		r = kernel(SYS_pwrite64, m->fd, a, n, __SYSCALL_LL_E(off), 0);
		if (r < 0)
			return r;
		a += r, off += r, n -= r;
	}
	return 0;
}

static long wasm_msync(uintptr_t addr, size_t len)
{
	struct shared_map *m;
	long r;

	for (m = shared_maps; m; m = m->next)
		if ((r = shared_map_sync(m, addr, len)) < 0)
			return r;
	return 0;
}

/* munmap: the shared mappings in the range written back, and cut out of it */
static long wasm_munmap(uintptr_t addr, size_t len)
{
	struct shared_map **p = &shared_maps, *m, *right;
	uintptr_t end;

	len = (len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
	end = addr + len;
	while ((m = *p)) {
		uintptr_t m_end = m->addr + m->len;

		if (m_end <= addr || m->addr >= end) {
			p = &m->next;
			continue;
		}
		shared_map_sync(m, addr, len);
		if (m_end > end && m->addr < addr && (right = shared_map_new())) {
			*right = *m;
			right->addr = end;
			right->len = m_end - end;
			right->off = m->off + (end - m->addr);
			right->fd = kernel(SYS_fcntl64, m->fd, F_DUPFD_CLOEXEC, 0, 0, 0, 0);
			m->next = right;
		}
		if (m->addr < addr) {
			m->len = addr - m->addr;
			p = &m->next;
		} else if (m_end > end) {
			m->off += end - m->addr;
			m->addr = end;
			m->len = m_end - end;
			p = &m->next;
		} else {
			kernel(SYS_close, m->fd, 0, 0, 0, 0, 0);
			*p = m->next;
			m->next = spare_maps;
			spare_maps = m;
		}
	}
	mm_free(addr, len);
	return 0;
}

/* a file mapping is a copy of the file; a writable shared one written back (above) */
static long wasm_mmap(long addr, long len, long prot, long flags, long fd,
		      long pgoff)
{
	struct shared_map *m = 0;
	long p, n;

	if (flags & MAP_FIXED) {
		/* over what the caller mapped (as InnoDB commits what it
		 * reserved): zeroed, or the file copied in */
		if (addr % MMAP_ALIGN || (uintptr_t)addr + len > mm_end ||
		    (uintptr_t)addr + len < (uintptr_t)addr)
			return -ENOMEM;
		if (!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (prot & PROT_WRITE))
			return -ENODEV;
		memset((void *)addr, 0, len);
		if (flags & MAP_ANONYMOUS)
			return addr;
		n = kernel(SYS_pread64, fd, addr, len, __SYSCALL_LL_E((long long)pgoff * 4096), 0);
		return n < 0 ? n : addr;
	}
	if (!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (prot & PROT_WRITE)) {
		if (!(m = shared_map_new()))
			return -ENOMEM;
		m->fd = kernel(SYS_fcntl64, fd, F_DUPFD_CLOEXEC, 0, 0, 0, 0);
		if (m->fd < 0) {
			n = m->fd;
			m->next = spare_maps;
			spare_maps = m;
			return n;
		}
	}
	p = mm_alloc(len);
	if (p >= 0 && !(flags & MAP_ANONYMOUS)) {
		n = kernel(SYS_pread64, fd, p, len, __SYSCALL_LL_E((long long)pgoff * 4096), 0);
		if (n < 0) {
			mm_free(p, len);
			p = n;
		}
	}
	if (m) {
		if (p < 0) {
			kernel(SYS_close, m->fd, 0, 0, 0, 0, 0);
			m->next = spare_maps;
			spare_maps = m;
			return p;
		}
		m->addr = p;
		m->len = (len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
		m->off = (long long)pgoff * 4096;
		m->next = shared_maps;
		shared_maps = m;
	}
	return p;
}

static long wasm_mremap(long old, long old_len, long new_len, long flags)
{
	struct shared_map *m;
	long p;

	if (new_len <= old_len)
		return old;
	/*
	 * (in place it can't grow: ENOMEM for memory that's there, EFAULT
	 * past it, as for an unmapped range: musl's pthread_getattr_np finds
	 * the main thread's stack so, from its top down to the first page)
	 */
	if (!(flags & MREMAP_MAYMOVE))
		return (uintptr_t)old < MMAP_ALIGN || (uintptr_t)old + old_len > mm_end ? -EFAULT : -ENOMEM;
	p = mm_alloc(new_len);
	if (p >= 0) {
		memcpy((void *)p, (void *)old, old_len);
		mm_free(old, old_len);
		/* (a shared mapping moved: its record with it) */
		for (m = shared_maps; m; m = m->next) {
			if (m->addr == (uintptr_t)old) {
				m->addr = p;
				m->len = (new_len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
			}
		}
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
		return wasm_munmap(a, b);
	case SYS_msync:
		return wasm_msync(a, b);
	case SYS_mprotect:
	case SYS_madvise:
		return 0;
	default:
		return kernel(n, a, b, c, d, e, f);
	}
}
