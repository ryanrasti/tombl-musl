/*
 * Memory mappings in libc. A process's memory is its own wasm memory, which
 * the kernel can't map pages into, so mmap and its relatives are here:
 * anonymous mappings and private file mappings are memory (sbrk's, which
 * malloc shares), with munmapped ranges reused; writable shared file
 * mappings are copies too, written back to the file (only the bytes that
 * changed) by msync, munmap, fsync, exec and exit. Other processes, and the
 * file's readers, see the writes only then, and two mappings of the same
 * file don't see each other's.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "atomic.h"
#include "lock.h"
#include "syscall.h"
#include "wasm_mman.h"

#define MMAP_ALIGN 4096

static volatile int lock[1];

/*
 * Free ranges (munmapped: thread stacks, big allocations), first fit, on a
 * list kept in the ranges themselves; else more of sbrk's.
 */
struct free_range {
	struct free_range *next;
	size_t len;
};
static struct free_range *free_ranges;

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
	LOCK(lock);
	r->len = len;
	r->next = free_ranges;
	free_ranges = r;
	UNLOCK(lock);
}

static long mm_alloc(size_t len)
{
	uintptr_t p, brk;

	len = (len + MMAP_ALIGN - 1) & -MMAP_ALIGN;
	if (!len || len > PTRDIFF_MAX)
		return -ENOMEM;
	LOCK(lock);
	p = mm_reuse(len);
	if (!p) {
		/* sbrk's (fresh memory: zeroes), aligned */
		brk = (uintptr_t)sbrk(0);
		if (brk != (uintptr_t)-1 && brk % MMAP_ALIGN &&
		    sbrk(MMAP_ALIGN - brk % MMAP_ALIGN) == (void *)-1)
			brk = -1;
		if (brk != (uintptr_t)-1)
			p = (uintptr_t)sbrk(len);
		if (brk == (uintptr_t)-1 || p == (uintptr_t)-1) {
			UNLOCK(lock);
			return -ENOMEM;
		}
	}
	UNLOCK(lock);
	return p;
}

/* Writable shared file mappings. */
struct shared_map {
	uintptr_t addr;
	size_t len;		/* 0: a free slot; 1: being set up */
	off_t off;
	int fd;			/* ours (a dup), for writing back */
	unsigned char *snap;	/* the file's bytes as last read or written */
	uintptr_t snap_base;	/* ...in this allocation (0: none yet) */
	size_t snap_len;
};
static struct shared_map shared_maps[64];

static void shared_drop(struct shared_map *m)
{
	int fd = m->fd;

	if (m->snap_base)
		mm_free(m->snap_base, m->snap_len);
	m->snap_base = 0;
	/* free before the syscall, where other threads run */
	m->len = 0;
	__syscall(SYS_close, fd);
}

/* write the changed bytes of [addr, addr+len) of mapping m back to its file */
static long shared_write_back(struct shared_map *m, uintptr_t addr, size_t len)
{
	uintptr_t end = addr + len;
	unsigned char *p, *q, *snap;
	struct stat st;
	off_t off;
	long n;

	if (addr < m->addr)
		addr = m->addr;
	if (end > m->addr + m->len)
		end = m->addr + m->len;
	off = m->off + (addr - m->addr);
	/* as natively, nothing past the file's end reaches it */
	if (fstat(m->fd, &st))
		return -errno;
	if (off >= st.st_size || addr >= end)
		return 0;
	if (end - addr > st.st_size - off)
		end = addr + (st.st_size - off);
	snap = m->snap + (addr - m->addr);
	for (p = (void *)addr; p < (unsigned char *)end; p = q) {
		/* a run of changed bytes: from the first differing to the next equal */
		while (p < (unsigned char *)end && *p == *snap)
			p++, snap++;
		for (q = p; q < (unsigned char *)end && *q != snap[q - p]; q++)
			;
		if (q == p)
			break;
		n = __syscall(SYS_pwrite64, m->fd, (long)p, q - p,
			      __SYSCALL_LL_E((long long)(m->off + ((uintptr_t)p - m->addr))));
		if (n < 0)
			return n;
		memcpy(snap, p, q - p);
		snap += q - p;
	}
	return 0;
}

/* msync and munmap: write back the shared mappings in [addr, addr+len) */
static long shared_sync(uintptr_t addr, size_t len, int unmap)
{
	uintptr_t end = addr + len;
	long ret = 0, r;

	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++) {
		struct shared_map *m = &shared_maps[i];

		if (m->len <= 1 || m->addr + m->len <= addr || end <= m->addr)
			continue;
		r = shared_write_back(m, addr, len);
		if (r && !ret)
			ret = r;
		if (!unmap)
			continue;
		if (addr <= m->addr && m->addr + m->len <= end) {
			shared_drop(m);
		} else if (addr <= m->addr) {
			/* its start unmapped: what's left is the rest */
			size_t cut = end - m->addr;
			m->addr += cut, m->off += cut, m->snap += cut, m->len -= cut;
		} else if (m->addr + m->len <= end) {
			m->len = addr - m->addr;	/* its end unmapped */
		}
		/* (a hole in the middle leaves it all mapped) */
	}
	return ret;
}

hidden void __wasm_shared_flush(void)
{
	shared_sync(0, UINTPTR_MAX, 0);
}

hidden long __wasm_mmap(long addr, long len, long prot, long flags, long fd, off_t off)
{
	struct shared_map *m = 0;
	long p, n;
	int i;

	if (flags & MAP_FIXED)
		return -ENOMEM;
	if (!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (prot & PROT_WRITE)) {
		for (i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++)
			if (a_cas((volatile int *)&shared_maps[i].len, 0, 1) == 0)
				break;
		if (i == sizeof shared_maps / sizeof *shared_maps)
			return -ENOMEM;
		m = &shared_maps[i];
		m->snap_base = 0;
		n = __syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
		if (n < 0) {
			m->len = 0;
			return n;
		}
		m->fd = n;
		m->off = off;
	}
	p = mm_alloc(len);
	if (p < 0 || (flags & MAP_ANONYMOUS)) {
		if (m)
			shared_drop(m);
		return p;	/* zeroed */
	}
	/* a file mapping: a copy of the file's contents */
	n = __syscall(SYS_pread64, fd, p, len, __SYSCALL_LL_E((long long)off));
	if (m && n < 0)
		shared_drop(m);
	else if (m) {
		long snap = mm_alloc(len);

		if (snap < 0) {
			shared_drop(m);
			return snap;
		}
		memcpy((void *)snap, (void *)p, len);
		m->snap = (void *)snap;
		m->snap_base = snap;
		m->snap_len = len;
		m->addr = p;
		m->len = len;
	}
	if (n < 0)
		mm_free(p, len);
	return n < 0 ? n : p;
}

hidden long __wasm_munmap(long addr, long len)
{
	/* (written back before the range is reused) */
	shared_sync(addr, len, 1);
	mm_free(addr, len);
	return 0;
}

hidden long __wasm_msync(long addr, long len)
{
	return shared_sync(addr, len, 0);
}

hidden long __wasm_mremap(long old, long old_len, long new_len, long flags)
{
	long p;

	if (new_len <= old_len)
		return old;
	if (!(flags & MREMAP_MAYMOVE))
		return -ENOMEM;
	p = mm_alloc(new_len);
	if (p < 0)
		return p;
	memcpy((void *)p, (void *)old, old_len);
	/* a shared file mapping moves with its copy */
	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++)
		if (shared_maps[i].len > 1 && shared_maps[i].addr == old) {
			struct shared_map *m = &shared_maps[i];
			long snap = mm_alloc(new_len);

			m->addr = p;
			if (snap < 0)
				continue;	/* (the growth isn't written back) */
			/* the file's bytes past the old end, as natively mapped */
			__syscall(SYS_pread64, m->fd, p + m->len, new_len - m->len,
				  __SYSCALL_LL_E((long long)(m->off + m->len)));
			memcpy((void *)snap, m->snap, m->len);
			memcpy((void *)(snap + m->len), (void *)(p + m->len), new_len - m->len);
			mm_free(m->snap_base, m->snap_len);
			m->snap = (void *)snap;
			m->snap_base = snap;
			m->snap_len = new_len;
			m->len = new_len;
		}
	mm_free(old, old_len);
	return p;
}
