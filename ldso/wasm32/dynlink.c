/* wasm32's part of the loader (ldso/dynlink.c includes it where it picks
 * laddr): a dso's functions are table slots, its code a wasm module the
 * host compiles from its PT_WASM segment and instantiates once relocated
 * (its GOT words read then: imports bound, GOT globals set). */

__asm__(".globaltype __table_base, i32, immutable\n");

static int search_vec(size_t *v, size_t *r, size_t key);

/* the next dso's table slots: past exec's and every dso mapped since */
static size_t wasm_table_next;

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

/* reloc_all's: the relocated dso instantiated (the interpreter, exec's
 * instance: its GOT globals read again); then, but for the interpreter,
 * whose _dlstart did it, its memory initialized by its start function and
 * its own relocations applied, in user space. The start function points a
 * module with TLS at its own block (wasm-ld's main thread's): the thread
 * pointer set again puts it back at tp - its offset (TLS variant II), as
 * the host keeps each instance's __tls_base */
static int wasm_instantiate(struct dso *p)
{
	size_t v;
	long r = __syscall(SYS_wasm_instantiate, p->wasm.code, laddr(p, p->wasm.memory_vaddr),
		p->wasm.table_base, p->wasm.table_size, -p->tls.offset);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	if (p == &ldso) return 0;
	if (search_vec(p->dynv, &v, DT_WASM_START)) fpaddr(p, v)();
	if (search_vec(p->dynv, &v, DT_WASM_RELOCS)) fpaddr(p, v)();
	if (p->tls.size) __syscall(SYS_set_thread_area, __get_tp());
	return 0;
}

