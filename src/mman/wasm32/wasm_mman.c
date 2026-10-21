/*
 * Memory mappings in libc. A process's memory is its own wasm memory, which
 * the kernel can't map pages into, so mmap and its relatives are here:
 * anonymous mappings and private file mappings are memory (sbrk's, which
 * malloc shares), with munmapped ranges reused; writable shared file
 * mappings are copies too, written back to the file (only the bytes that
 * changed) by msync, munmap, fsync, exec and exit. Other processes, and the
 * file's readers, see the writes only then, and two mappings of the same
 * file don't see each other's. (linux-workers' docs/internals.md, Known
 * issues: what that leaves out.)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libc.h"
#include "lock.h"
#include "pthread_impl.h"
#include "syscall.h"
#include "wasm_mman.h"

/* a mapping's alignment: the kernel's page (sysconf(_SC_PAGESIZE)) */
#define ALIGN ((uintptr_t)PAGE_SIZE)
#define ROUND(n) (((uintptr_t)(n) + ALIGN - 1) & -ALIGN)

/* the free ranges' (inside the table's lock, when both are taken) */
static volatile int lock[1];

/*
 * Free ranges (munmapped), first fit, on a list kept in the ranges
 * themselves, by address, neighbours merged; else more of sbrk's.
 */
struct free_range {
	struct free_range *next;
	size_t len;
};
static struct free_range *free_ranges;

/* [addr, addr+len) free (any of it already free too): merged with what it touches */
static void mm_free_locked(uintptr_t addr, size_t len)
{
	uintptr_t end = addr + len;
	struct free_range **p = &free_ranges, *r;

	while ((r = *p) && (uintptr_t)r + r->len < addr)
		p = &r->next;
	while ((r = *p) && (uintptr_t)r <= end) {
		if ((uintptr_t)r < addr)
			addr = (uintptr_t)r;
		if ((uintptr_t)r + r->len > end)
			end = (uintptr_t)r + r->len;
		*p = r->next;
	}
	r = (void *)addr;
	r->len = end - addr;
	r->next = *p;
	*p = r;
}

static void mm_free(uintptr_t addr, size_t len)
{
	/* (none at 0: munmap(NULL), as of nothing mapped, is nothing to free) */
	if (!addr || !len || addr % ALIGN)
		return;
	LOCK(lock);
	mm_free_locked(addr, ROUND(len));
	UNLOCK(lock);
}

/* [addr, addr+len) off the free list, zeroed, if it's all free: 1, else 0 */
static int mm_take(uintptr_t addr, size_t len)
{
	struct free_range **p, *r, *rest;
	uintptr_t start, end;

	for (p = &free_ranges; (r = *p) && (uintptr_t)r <= addr; p = &r->next) {
		start = (uintptr_t)r;
		end = start + r->len;
		if (end < addr + len)
			continue;
		rest = 0;
		if (addr + len < end) {
			rest = (void *)(addr + len);
			rest->len = end - (addr + len);
			rest->next = r->next;
		}
		if (start < addr) {
			r->len = addr - start;
			if (rest)
				r->next = rest;
		} else {
			*p = rest ? rest : r->next;
		}
		memset((void *)addr, 0, len);	/* as fresh memory is */
		return 1;
	}
	return 0;
}

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

/* len more of sbrk's, aligned (fresh memory: zeroes): 0, none */
static uintptr_t mm_more(size_t len)
{
	for (;;) {
		uintptr_t brk = (uintptr_t)sbrk(0), pad, p, a;

		if (brk == (uintptr_t)-1)
			return 0;
		pad = -brk & (ALIGN - 1);
		if (len > PTRDIFF_MAX - pad)
			return 0;
		p = (uintptr_t)sbrk(len + pad);
		if (p == (uintptr_t)-1)
			return 0;
		a = ROUND(p);
		if (a - p <= pad)
			return a;
		/* (the break moved meanwhile, sbrk called elsewhere: what's aligned of it free, and again) */
		if (a < ((p + len + pad) & -ALIGN))
			mm_free_locked(a, ((p + len + pad) & -ALIGN) - a);
	}
}

/* len more at addr, the break: 1, else 0 */
static int mm_grow_at_break(uintptr_t addr, size_t len)
{
	uintptr_t p;

	if ((uintptr_t)sbrk(0) != addr)
		return 0;
	p = (uintptr_t)sbrk(len);
	if (p == addr)
		return 1;
	if (p != (uintptr_t)-1 && ROUND(p) < ((p + len) & -ALIGN))
		mm_free_locked(ROUND(p), ((p + len) & -ALIGN) - ROUND(p));
	return 0;
}

static long mm_alloc(size_t len)
{
	uintptr_t p;

	if (!len || len > PTRDIFF_MAX)
		return -ENOMEM;
	len = ROUND(len);
	LOCK(lock);
	p = mm_reuse(len);
	if (!p)
		p = mm_more(len);
	UNLOCK(lock);
	return p ? p : -ENOMEM;
}

/*
 * Writable shared file mappings, their table under its own lock (taken
 * before the free ranges'), held across their write-backs: a munmap
 * meanwhile in another thread would free a snapshot being compared, and
 * close the descriptor written through.
 */
struct shared_map {
	uintptr_t addr;
	size_t len;		/* 0: a free slot */
	off_t off;
	int fd;			/* ours (the file opened again, or a dup), for writing back */
	unsigned char *snap;	/* the file's bytes as last read or written */
	uintptr_t snap_base;	/* ...in this allocation (0: none yet) */
	size_t snap_len;
};
static struct shared_map shared_maps[64];
static volatile int maplock[1];
static int maplock_owner;

/* 1 taken; 0 this thread has it (exiting from a signal handler that interrupted a write-back) */
static int map_lock(void)
{
	int self = __pthread_self()->tid;

	if (maplock_owner == self)
		return 0;
	LOCK(maplock);
	maplock_owner = self;
	return 1;
}

static void map_unlock(int taken)
{
	if (!taken)
		return;
	maplock_owner = 0;
	UNLOCK(maplock);
}

static struct shared_map *shared_slot(void)
{
	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++)
		if (!shared_maps[i].len)
			return &shared_maps[i];
	return 0;
}

static void shared_drop(struct shared_map *m)
{
	if (m->snap_base)
		mm_free(m->snap_base, m->snap_len);
	m->snap_base = 0;
	m->len = 0;
	__syscall(SYS_close, m->fd);
}

/* the file at fd as a description of our own, read and write: a write-back through it isn't an append (O_APPEND), nor moves its offset */
static long shared_open(int fd)
{
	char buf[15 + 3 * sizeof(int)];
	long n;

	__procfdname(buf, fd);
	n = __syscall(SYS_openat, AT_FDCWD, buf, O_RDWR | O_CLOEXEC | O_LARGEFILE);
	return n >= 0 ? n : __syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
}

/* len bytes of fd at off into p, as far as the file goes (the rest left as it is): a negative errno, else 0 */
static long pread_all(int fd, uintptr_t p, size_t len, off_t off)
{
	long n;

	while (len) {
		n = __syscall(SYS_pread64, fd, p, len, __SYSCALL_LL_E((long long)off));
		if (n == -EINTR)
			continue;
		if (n <= 0)
			return n;
		p += n, len -= n, off += n;
	}
	return 0;
}

/* write the changed bytes of [addr, addr+len) of mapping m back to its file */
static long shared_write_back(struct shared_map *m, uintptr_t addr, size_t len)
{
	uintptr_t end = addr + len < addr ? UINTPTR_MAX : addr + len;
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
		for (unsigned char *w = p; w < q; ) {
			n = __syscall(SYS_pwrite64, m->fd, (long)w, q - w,
				      __SYSCALL_LL_E((long long)(m->off + ((uintptr_t)w - m->addr))));
			if (n == -EINTR)
				continue;
			if (n < 0)
				return n;
			w += n;
		}
		memcpy(snap, p, q - p);
		snap += q - p;
	}
	return 0;
}

/* msync: write back the shared mappings in [addr, addr+len) */
static long shared_sync(uintptr_t addr, size_t len)
{
	uintptr_t end = addr + len < addr ? UINTPTR_MAX : addr + len;
	long ret = 0, r;

	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++) {
		struct shared_map *m = &shared_maps[i];

		if (!m->len || m->addr + m->len <= addr || end <= m->addr)
			continue;
		r = shared_write_back(m, addr, len);
		if (r && !ret)
			ret = r;
	}
	return ret;
}

/*
 * munmap: the shared mappings in [addr, end) written back, and that much
 * of them gone; a hole in the middle of one leaves two, the rest another
 * mapping with its own descriptor and snapshot. -ENOMEM, if there's no
 * room for it (then, as natively, nothing more of the range is unmapped).
 */
static long shared_unmap(uintptr_t addr, uintptr_t end)
{
	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++) {
		struct shared_map *m = &shared_maps[i], *t;
		size_t cut;
		long fd, snap;

		if (!m->len || m->addr + m->len <= addr || end <= m->addr)
			continue;
		shared_write_back(m, addr, end - addr);
		if (addr <= m->addr && m->addr + m->len <= end) {
			shared_drop(m);
		} else if (addr <= m->addr) {
			/* its start unmapped: what's left is the rest */
			cut = end - m->addr;
			m->addr += cut, m->off += cut, m->snap += cut, m->len -= cut;
		} else if (m->addr + m->len <= end) {
			m->len = addr - m->addr;	/* its end unmapped */
		} else {
			cut = end - m->addr;
			if (!(t = shared_slot()))
				return -ENOMEM;
			snap = mm_alloc(m->len - cut);
			if (snap < 0)
				return -ENOMEM;
			fd = __syscall(SYS_fcntl, m->fd, F_DUPFD_CLOEXEC, 0);
			if (fd < 0) {
				mm_free(snap, m->len - cut);
				return -ENOMEM;
			}
			memcpy((void *)snap, m->snap + cut, m->len - cut);
			*t = (struct shared_map){
				.addr = end, .len = m->len - cut, .off = m->off + cut, .fd = fd,
				.snap = (void *)snap, .snap_base = snap, .snap_len = m->len - cut,
			};
			m->len = addr - m->addr;
		}
	}
	return 0;
}

hidden void __wasm_shared_flush(void)
{
	int taken = map_lock();

	shared_sync(0, UINTPTR_MAX);
	map_unlock(taken);
}

/*
 * fork (around _Fork): <0, the locks taken; 0, the parent, let go of; >0,
 * the child: its locks fresh, and its snapshots its own memory as it is,
 * so its write-backs are its own changes, not the parent's it inherited.
 */
hidden void __wasm_mman_atfork(int who)
{
	if (who < 0) {
		LOCK(maplock);
		LOCK(lock);
	} else if (!who) {
		UNLOCK(lock);
		UNLOCK(maplock);
	} else {
		lock[0] = 0;
		maplock[0] = 0;
		maplock_owner = 0;
		for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++)
			if (shared_maps[i].len)
				memcpy(shared_maps[i].snap, (void *)shared_maps[i].addr, shared_maps[i].len);
	}
}

hidden long __wasm_mmap(long addr, long len, long prot, long flags, long fd, off_t off)
{
	int shared = !(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (prot & PROT_WRITE);
	struct shared_map *m = 0;
	int taken = 0;
	long p, n;

	if (flags & MAP_FIXED)
		return -ENOMEM;	/* (nothing is at a fixed address) */
	if (!(flags & MAP_ANONYMOUS)) {
		struct stat st;
		long fl = __syscall(SYS_fcntl, fd, F_GETFL);

		if (fl < 0)
			return fl;
		if (fstat(fd, &st))
			return -errno;
		if (!S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
			return -ENODEV;
		if ((fl & O_ACCMODE) == O_WRONLY || (shared && (fl & O_ACCMODE) != O_RDWR))
			return -EACCES;
	}
	if (shared) {
		taken = map_lock();
		if (!(m = shared_slot())) {
			map_unlock(taken);
			return -ENOMEM;
		}
		n = shared_open(fd);
		if (n < 0) {
			map_unlock(taken);
			return n;
		}
		m->fd = n;
		m->off = off;
	}
	p = mm_alloc(len);
	if (p < 0 || (flags & MAP_ANONYMOUS)) {
		if (m)
			__syscall(SYS_close, m->fd);
		map_unlock(taken);
		return p;	/* zeroed */
	}
	/* a file mapping: a copy of the file's contents */
	n = pread_all(fd, p, len, off);
	if (!n && m) {
		long snap = mm_alloc(len);

		if (snap < 0) {
			n = snap;
		} else {
			memcpy((void *)snap, (void *)p, len);
			m->snap = (void *)snap;
			m->snap_base = snap;
			m->snap_len = len;
			m->addr = p;
			m->len = len;
		}
	}
	if (n < 0) {
		if (m)
			__syscall(SYS_close, m->fd);
		mm_free(p, len);
	}
	map_unlock(taken);
	return n < 0 ? n : p;
}

hidden long __wasm_munmap(long addr, long len)
{
	int taken = map_lock();
	/* (written back before the range is reused) */
	long ret = shared_unmap(addr, addr + ROUND(len));

	if (!ret)
		mm_free(addr, len);
	map_unlock(taken);
	return ret;
}

hidden long __wasm_msync(long addr, long len)
{
	int taken = map_lock();
	long ret = shared_sync(addr, len);

	map_unlock(taken);
	return ret;
}

/*
 * A mapping resized: shrunk, its tail freed; grown in place, into a free
 * range after it or the break; else moved (MREMAP_MAYMOVE). A shared file
 * mapping in it must be all of it, as mmap gave it (else EINVAL).
 */
hidden long __wasm_mremap(long old, long old_len, long new_len, long flags)
{
	size_t olen = ROUND(old_len), nlen = ROUND(new_len);
	struct shared_map *m = 0;
	int taken = map_lock(), in_place;
	long p = old, ret;

	for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++) {
		struct shared_map *e = &shared_maps[i];

		if (!e->len || e->addr + e->len <= old || old + olen <= e->addr)
			continue;
		if (e->addr != old || ROUND(e->len) != olen) {
			ret = -EINVAL;
			goto out;
		}
		m = e;
	}
	if (nlen < olen) {
		if (m && new_len < m->len) {
			shared_write_back(m, old + new_len, m->len - new_len);
			m->len = new_len;
		}
		mm_free(old + nlen, olen - nlen);
	} else if (nlen > olen) {
		LOCK(lock);
		in_place = mm_take(old + olen, nlen - olen) ||
			mm_grow_at_break(old + olen, nlen - olen);
		UNLOCK(lock);
		if (!in_place) {
			if (!(flags & MREMAP_MAYMOVE)) {
				ret = -ENOMEM;
				goto out;
			}
			p = mm_alloc(nlen);
			if (p < 0) {
				ret = p;
				goto out;
			}
			memcpy((void *)p, (void *)old, olen);
			mm_free(old, olen);
		}
	}
	if (m) {
		m->addr = p;
		if (new_len > m->len) {
			/* the file's bytes past the old end, as natively mapped, and in its snapshot */
			long snap = mm_alloc(new_len);

			if (snap >= 0) {
				pread_all(m->fd, p + m->len, new_len - m->len, m->off + m->len);
				memcpy((void *)snap, m->snap, m->len);
				memcpy((void *)(snap + m->len), (void *)(p + m->len), new_len - m->len);
				mm_free(m->snap_base, m->snap_len);
				m->snap = (void *)snap;
				m->snap_base = snap;
				m->snap_len = new_len;
				m->len = new_len;
			}	/* (else the growth isn't written back) */
		}
	}
	ret = p;
out:
	map_unlock(taken);
	return ret;
}

/*
 * madvise(MADV_DONTNEED): the range's private pages zeroed, as Linux's
 * anonymous ones read once dropped (allocators count on it); a writable
 * shared file mapping's are left, as the page cache keeps them.
 */
hidden long __wasm_madvise(long addr, long len, long advice)
{
	uintptr_t pos = addr, end = addr + ROUND(len);
	int taken;

	if (addr % ALIGN)
		return -EINVAL;
	if (advice != MADV_DONTNEED)
		return 0;
	taken = map_lock();
	while (pos < end) {
		uintptr_t next = end, skip = 0;

		/* up to the next shared mapping in it, then past it */
		for (int i = 0; i < sizeof shared_maps / sizeof *shared_maps; i++) {
			struct shared_map *m = &shared_maps[i];

			if (!m->len || m->addr + m->len <= pos || m->addr >= next)
				continue;
			next = m->addr > pos ? m->addr : pos;
			skip = m->addr + m->len;
		}
		memset((void *)pos, 0, next - pos);
		pos = skip > next ? skip : next;
	}
	map_unlock(taken);
	return 0;
}
