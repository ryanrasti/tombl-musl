/* wasm32's part of the loader (ldso/dynlink.c includes it where it picks
 * laddr): a dso's functions are table slots, its code a wasm module the
 * host compiles from its PT_WASM segment and instantiates once relocated
 * (its GOT words read then: imports bound, GOT globals set). */

__asm__(".globaltype __table_base, i32, immutable\n");

static int search_vec(size_t *v, size_t *r, size_t key);

/* the next dso's table slots: past exec's and every dso mapped since (a
 * failed dlopen's kept: what it instantiated keeps them, in the host's
 * every thread, so a later dso's would be bound to its functions) */
static size_t wasm_table_next;

/* (a function pointer at run time is its slot, a small number: one past
 * a data mapping's address (a process of more slots than its lowest
 * mapping's address) is taken as that data's by addr2dso, which looks at
 * PT_LOADs first, as mainline's does) */
static void *laddr(const struct dso *p, size_t v)
{
	if (v - WASM_FUNC_VADDR < p->wasm.table_size)
		return (void *)(p->wasm.table_base + (v - WASM_FUNC_VADDR));
	return p->base + v;
}
#define laddr_pg(p, v) laddr(p, v)
#define fpaddr(p, v) ((void (*)())laddr(p, v))

static void wasm_phdrs(struct dso *p, Phdr *ph, size_t phnum, size_t phent)
{
	for (; phnum--; ph = (void *)((char *)ph + phent)) {
		if (ph->p_type == PT_WASM) {
			p->wasm.code_off = ph->p_offset;
			p->wasm.code_size = ph->p_filesz;
			p->wasm.table_size = ph->p_memsz;
		} else if (ph->p_type == PT_LOAD && (ph->p_flags & PF_W) && !p->wasm.memory_vaddr) {
			p->wasm.memory_vaddr = ph->p_vaddr;
		}
	}
}

/* map_library's: the dso's code compiled, its table slots taken */
static int wasm_map_code(struct dso *p, int fd, Ehdr *eh, Phdr *ph)
{
	long r;
	wasm_phdrs(p, ph, eh->e_phnum, eh->e_phentsize);
	r = p->wasm.code_size ? __syscall(SYS_wasm_load_code, fd, p->wasm.code_off, p->wasm.code_size) : -ENOEXEC;
	if (r < 0) {
		errno = -r;
		return -1;
	}
	p->wasm.code = r;
	p->wasm.table_base = wasm_table_next;
	wasm_table_next += p->wasm.table_size;
	return 0;
}

/* unmap_library's: its code let go of (EINVAL, and kept, if instantiated) */
static void wasm_unmap_code(struct dso *p)
{
	if (p->wasm.code > 0) __syscall(SYS_wasm_unload_code, p->wasm.code);
	p->wasm.code = 0;
}

/* kernel_mapped_dso's: the interpreter (instantiated by exec at its own
 * __table_base) or the program (its code and table base in auxv) */
static void wasm_kernel_mapped(struct dso *p)
{
	wasm_phdrs(p, p->phdr, p->phnum, p->phentsize);
	if (p == &ldso) {
		size_t tb;
		__asm__("global.get __table_base\nlocal.set %0" : "=r"(tb));
		p->wasm.code = 0;
		p->wasm.table_base = tb;
	} else {
		for (size_t *a = libc.auxv; *a; a += 2) {
			if (a[0] == AT_WASM_CODE) p->wasm.code = a[1];
			if (a[0] == AT_WASM_TABLE) p->wasm.table_base = a[1];
		}
	}
	if (p->wasm.table_base + p->wasm.table_size > wasm_table_next)
		wasm_table_next = p->wasm.table_base + p->wasm.table_size;
}

/* reloc_all's: the relocated dso instantiated, its GOT (DT_PLTGOT) read
 * (the interpreter, exec's instance: its GOT globals read again); then, but for the interpreter,
 * whose _dlstart did it, its memory initialized by its start function and
 * its own relocations applied, in user space. Its thread-local block is
 * set once there is one (__wasm_dso_tls, wasm_install_tls) */
static int wasm_instantiate(struct dso *p)
{
	size_t v;
	long r = __syscall(SYS_wasm_instantiate, p->wasm.code, laddr(p, p->wasm.memory_vaddr),
		p->wasm.table_base, p->wasm.table_size, p->got);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	if (p == &ldso) return 0;
	if (search_vec(p->dynv, &v, DT_WASM_START)) fpaddr(p, v)();
	if (search_vec(p->dynv, &v, DT_WASM_RELOCS)) fpaddr(p, v)();
	return 0;
}

/* a dso's instance in the calling thread: its __tls_base the block (its __wasm_init_tls, which copies its image there too) */
static void wasm_init_tls(struct dso *p, uintptr_t block)
{
	size_t v;
	if (p->tls_id && search_vec(p->dynv, &v, DT_WASM_INIT_TLS))
		((void (*)(void *))laddr(p, v))((void *)block);
}

hidden void __acquire_ptc(void);
hidden void __release_ptc(void);

/* the calling thread's blocks of every dso its dtv has, from its dtv: as
 * it starts (pthread_create.c's start), and the main thread's once __dls3
 * has made its TLS. Under the ptc lock, which dlopen holds (__inhibit_ptc)
 * as it links a dso in and installs its TLS: a dso its dtv hasn't (one
 * loaded since) is install_new_tls's to give it. */
hidden void __wasm_dso_tls(void)
{
	pthread_t self = __pthread_self();
	__acquire_ptc();
	for (struct dso *p = head; p; p = p->next)
		if (p->tls_id && p->tls_id <= self->dtv[0])
			wasm_init_tls(p, self->dtv[p->tls_id] - DTP_OFFSET);
	__release_ptc();
}

/* install_new_tls's, for each thread, the calling one first: each module
 * loaded since (its TLS id past old_cnt) has its block in that thread at
 * dtv[id]. The calling thread's set by its __wasm_init_tls, which copies
 * the image and relocates it; another's a copy of the calling thread's,
 * its __tls_base set by the host (wasm_set_tls, by its tid), as only its
 * own code reaches its instance's. A copy is the relocated block: wasm32's
 * TLS relocations are data addresses (a thread-local pointer to a global),
 * the same in every thread; a thread-local pointer to another thread-local
 * is no static initializer (C++'s is a per-thread dynamic one) */
static void wasm_install_tls(pthread_t td, uintptr_t *dtv, size_t old_cnt)
{
	pthread_t self = __pthread_self();
	for (struct dso *p = head; p; p = p->next) {
		if (p->tls_id <= old_cnt) continue;
		uintptr_t block = dtv[p->tls_id] - DTP_OFFSET;
		if (td == self) {
			wasm_init_tls(p, block);
		} else {
			memcpy((void *)block, (void *)(self->dtv[p->tls_id] - DTP_OFFSET), p->tls.size);
			__syscall(SYS_wasm_set_tls, p->wasm.code, td->tid, block);
		}
	}
}
