/*
 * ldso/dynlink.c's ELF half for wasm32, selected at its hooks (DL_WASM).
 * A module isn't mapped from its file but compiled by the host, and linked
 * by instantiating it with its imports bound: three syscalls do what wasm
 * reserves to whoever holds the WebAssembly API (wasm_load_module,
 * wasm_link_module, wasm_module_sym; ldso/wasm32/module.h), and musl's
 * loader decides the rest, as it does on any arch. Included by
 * ldso/dynlink.c, for its statics.
 *
 * A freshly exec'd process's module 1 is the program and instance 1 the
 * interpreter (libc.so), which the kernel's exec loaded and linked; it
 * enters at _dlstart. A function's address is its slot in the process's
 * table, so a "symbol value" here is the slot or the data address less the
 * dso's base, for laddr to give it back.
 */
#include "module.h"
#include "process_args.h"

void __dls2b(size_t *sp, size_t *auxv);

#define WASM_INFO_MAX 65536

/* the program's _start (a table slot), for __dls3's CRTJMP */
static size_t wasm_app_entry;

/* an anonymous mapping, by the raw syscall: _dlstart's, before errno's thread pointer */
static void *wasm_mmap(size_t n)
{
	long r = __syscall(SYS_mmap2, 0, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	return (unsigned long)r > -4096UL ? 0 : (void *)r;
}

static long wasm_err(long r)
{
	if ((unsigned long)r > -4096UL) {
		errno = -r;
		return -1;
	}
	return r;
}

/*
 * A module's dso: its memory region (mmapped, unless the host placed it:
 * at), its handles, and its metadata as a dynamic section decode_dyn reads
 * (the host's, its string table rebased on the region; DT_INIT_ARRAY added:
 * its constructors' slot, once linked, a word of the section's own
 * storage, which load_library's copy of the dso leaves where it is)
 */
static void *wasm_dso(struct dso *dso, int mh, const unsigned char *info, unsigned char *at, size_t *storage, size_t storage_size)
{
	const struct wasm_module_info *h = (const void *)info;
	const size_t *hdyn = (const void *)(info + sizeof *h);
	size_t n = h->dyn, i, strtab = 0, strsz = 0, *dyn, *ctors;
	unsigned char *base = at;
	char *strings;

	for (i=0; i<n; i+=2) {
		if (hdyn[i] == DT_STRTAB) strtab = hdyn[i+1];
		if (hdyn[i] == DT_STRSZ) strsz = hdyn[i+1];
	}
	if (!base && h->mem_size) {
		size_t align = h->mem_align > PAGE_SIZE ? h->mem_align : PAGE_SIZE;
		unsigned char *map = wasm_mmap(h->mem_size + align);
		if (!map) return 0;
		base = (void *)(((uintptr_t)map + align-1) & -align);
		dso->map = map;
		dso->map_len = h->mem_size + align;
	}
	/* (its dynamic section, its constructors' slot and its string table, its own: the info is the caller's) */
	dyn = storage;
	if ((n + 6) * sizeof *dyn + strsz > storage_size)
		dyn = storage ? 0 : malloc((n + 6) * sizeof *dyn + strsz);
	if (!dyn) return 0;
	ctors = dyn + n + 5;
	*ctors = 0;
	strings = (char *)(ctors + 1);
	memcpy(strings, info + strtab, strsz);
	for (i=0; i+2<=n && hdyn[i] != DT_NULL; i+=2) {
		dyn[i] = hdyn[i];
		dyn[i+1] = hdyn[i] == DT_STRTAB ? (size_t)strings - (size_t)base : hdyn[i+1];
	}
	dyn[i++] = DT_INIT_ARRAY;
	dyn[i++] = (size_t)ctors - (size_t)base;
	dyn[i++] = DT_INIT_ARRAYSZ;
	dyn[i++] = 0;
	dyn[i] = DT_NULL;
	dso->base = base;
	dso->dynv = dyn;
	dso->wasm.module = mh;
	dso->wasm.table_size = h->table_size;
	return dyn;
}

/* (its DT_INIT_ARRAY's one slot and DT_INIT_ARRAYSZ: its constructors, once linked; the program's are crt1's) */
static void wasm_set_ctors(struct dso *p, size_t slot)
{
	size_t *dyn = p->dynv;
	for (; *dyn; dyn+=2) {
		if (*dyn == DT_INIT_ARRAY) *(size_t *)laddr(p, dyn[1]) = slot;
		if (*dyn == DT_INIT_ARRAYSZ) dyn[1] = slot && p != head ? sizeof(size_t) : 0;
	}
}

static void *map_library(int fd, struct dso *dso)
{
	static unsigned char info[WASM_INFO_MAX];
	long mh = wasm_err(__syscall(SYS_wasm_load_module, fd, info, sizeof info));
	return mh < 0 ? 0 : wasm_dso(dso, mh, info, 0, 0, 0);
}

static void unmap_library(struct dso *dso)
{
	if (dso->map && dso->map_len) munmap(dso->map, dso->map_len);
	if (dso != &ldso) free(dso->dynv);
}

static void reclaim_gaps(struct dso *dso)
{
}

/* the program, as exec loaded it (module 1) */
static void kernel_mapped_dso(struct dso *p)
{
	static unsigned char info[WASM_INFO_MAX];
	if (wasm_err(__syscall(SYS_wasm_load_module, -1, info, sizeof info)) < 0
	    || !wasm_dso(p, 1, info, 0, 0, 0)) {
		error("Error loading %s: %m", p->name);
		ldso_fail = 1;
	}
}

static struct symdef find_sym2(struct dso *dso, const char *s, int need_def, int use_deps)
{
	static __thread Sym sym;
	struct dso **deps = use_deps ? dso->deps : 0;
	for (; dso; dso=use_deps ? *deps++ : dso->syms_next) {
		long v;
		if (!dso->wasm.instance) continue;
		v = __syscall(SYS_wasm_module_sym, dso->wasm.instance, s, 0, 0, 0);
		if (!v || (unsigned long)v > -4096UL) continue;
		sym.st_value = (size_t)v - (size_t)dso->base;
		sym.st_info = STT_FUNC | (STB_GLOBAL << 4);
		sym.st_shndx = 1;
		return (struct symdef){ .sym = &sym, .dso = dso };
	}
	return (struct symdef){ 0 };
}

/* whether a linked dso itself exports a symbol */
static int wasm_defines(struct dso *p, const char *s)
{
	long v = __syscall(SYS_wasm_module_sym, p->wasm.instance, s, WASM_SYM_OWN, 0, 0);
	return v && (unsigned long)v <= -4096UL;
}

static void prepare_lazy(struct dso *p)
{
	p->wasm.lazy = 1;
}

static void redo_lazy_relocs()
{
}

/*
 * Linking a set (mainline's reloc_all): each dso not relocated yet
 * instantiated, its imports left pending; then all bound at once over the
 * global scope in mainline's order (head, then syms_next: the program,
 * preloads, libraries breadth first), so precedence is ELF's exactly
 * (WASM_LINK_COMMIT; lazy: what's still undefined stays pending); then each
 * one's start function and data relocations run here, as process code,
 * which write the addresses bound. The set is every dso from head not
 * relocated, not only p's: __dls3 relocates the libraries (app.next)
 * before the program, whose definitions (a malloc of its own) are theirs
 * and libc.so's too, which a wasm module has only once it's instantiated;
 * so the program is in the libraries' set, and reloc_all(&app) finds it
 * done. (A dlopen's set is the same either way: the tail from p.)
 */
static void reloc_all(struct dso *p)
{
	struct dso *q, *r, **list;
	struct wasm_link_out out;
	size_t n = 0, i, ns;
	int lazy = 0;
	int scope[256];
	long ret;

	for (q=head; q; q=q->next) if (!q->relocated) n++;
	if (!n) return;
	list = malloc(n * sizeof *list);
	if (!list) {
		error("Error relocating %s: %m", p->name);
		if (runtime) longjmp(*rtld_fail, 1);
		return;
	}
	for (n=0, q=head; q; q=q->next) if (!q->relocated) list[n++] = q;
	for (i=0; i<n; i++) {
		q = list[i];
		lazy |= q->wasm.lazy;
		ret = __syscall(SYS_wasm_link_module, q->wasm.module, q->base, 0, 0, 0, &out);
		if ((unsigned long)ret > -4096UL) {
			error("Error relocating %s: %s", q->name, strerror(-ret));
			goto fail;
		}
		q->wasm.instance = ret;
		q->wasm.table_base = out.table_base;
		q->wasm.start = out.start;
		q->wasm.relocs = out.relocs;
		wasm_set_ctors(q, out.ctors);
		q->relocated = 1;
	}
	for (ns=0, r=head; r && ns<sizeof scope/sizeof *scope; r=r->syms_next)
		if (r->wasm.instance) scope[ns++] = r->wasm.instance;
	ret = __syscall(SYS_wasm_link_module, 0, 0, scope, ns,
		WASM_LINK_COMMIT | (lazy ? WASM_LINK_LAZY : 0), &out);
	if ((unsigned long)ret > -4096UL) {
		error("Error relocating %s: %s: symbol not found", p->name, out.undefined);
		goto fail;
	}
	/* (mainline swaps a second libc for libc.so before mapping it; linked, refused) */
	for (i=0; i<n; i++) {
		q = list[i];
		if (q != &ldso && wasm_defines(q, "__libc_start_main") && wasm_defines(q, "stdin")) {
			error("Error loading %s: a second libc", q->name);
			goto fail;
		}
	}
	for (i=n; i--; ) {
		q = list[i];
		if (q->wasm.start) ((void (*)(void))(size_t)q->wasm.start)();
		if (q->wasm.relocs) ((void (*)(void))(size_t)q->wasm.relocs)();
		q->wasm.start = q->wasm.relocs = 0;
		if (q == head && !wasm_app_entry) {
			long e = __syscall(SYS_wasm_module_sym, q->wasm.instance, "_start", 0, 0, 0);
			if ((unsigned long)e <= -4096UL) wasm_app_entry = e;
		}
	}
	free(list);
	return;
fail:
	free(list);
	if (runtime) longjmp(*rtld_fail, 1);
}

static void *addr2dso(size_t a)
{
	struct dso *p;
	for (p=head; p; p=p->next) {
		if (a - (size_t)p->base < p->map_len) return p;
		if (a - p->wasm.table_base < p->wasm.table_size) return p;
	}
	return 0;
}

/* a symbol's name, kept as long as its library (as mainline's string table): each once */
static const char *wasm_name(struct dso *p, const char *name)
{
	struct wasm_name { struct wasm_name *next; char name[]; } *n;
	for (n=p->wasm.names; n; n=n->next)
		if (!strcmp(n->name, name)) return n->name;
	n = malloc(sizeof *n + strlen(name) + 1);
	if (!n) return 0;
	strcpy(n->name, name);
	n->next = p->wasm.names;
	p->wasm.names = n;
	return n->name;
}

int dladdr(const void *addr_arg, Dl_info *info)
{
	struct { uint32_t addr; char name[256]; } out;
	struct dso *p;
	long ih;

	pthread_rwlock_rdlock(&lock);
	p = addr2dso((size_t)addr_arg);
	pthread_rwlock_unlock(&lock);
	if (!p) return 0;
	ih = __syscall(SYS_wasm_module_sym, p->wasm.instance, addr_arg, WASM_SYM_ADDR, &out, sizeof out);
	info->dli_fname = p->name;
	info->dli_fbase = p->map;
	if (!ih || (unsigned long)ih > -4096UL) {
		info->dli_sname = 0;
		info->dli_saddr = 0;
		return 1;
	}
	info->dli_sname = wasm_name(p, out.name);
	info->dli_saddr = (void *)(size_t)out.addr;
	return 1;
}

/* (CRTJMP: the program's PIC _start, given the stack, as mainline's entry is: its arguments, environment and auxv) */
static _Noreturn void __wasm_dls_enter(size_t *sp)
{
	((void (*)(size_t *))wasm_app_entry)(sp);
	a_crash();
}

/*
 * The interpreter's entry (exec's: instance 1, libc.so). Its own start
 * function and relocations first, through the slots the host gives it (no
 * data of its own is usable before), then libc.so as ldso, the arguments as
 * a stack mainline reads (argc, argv, envp, auxv: the kernel gives them
 * once, here, and the program gets them from this stack), and mainline's
 * stage 2b.
 */
void _dlstart(void)
{
	static unsigned char info[WASM_INFO_MAX];
	static size_t ldso_dyn[512];
	struct wasm_link_out self;
	struct wasm_process_args *args;
	size_t *sp, i, n, na, *auxv;
	const size_t args_size = 65536 * 4;

	if (__syscall(SYS_wasm_module_sym, 1, 0, WASM_SYM_INIT, &self, sizeof self)) a_crash();
	if (self.start) ((void (*)(void))(size_t)self.start)();
	if (self.relocs) ((void (*)(void))(size_t)self.relocs)();
	ldso_page_size = 4096;

	if (wasm_err(__syscall(SYS_wasm_load_module, -2, info, sizeof info)) < 0
	    || !wasm_dso(&ldso, 2, info, (void *)(size_t)self.memory_base, ldso_dyn, sizeof ldso_dyn))
		a_crash();
	ldso.name = ldso.shortname = "libc.so";
	ldso.wasm.instance = 1;
	ldso.wasm.table_base = self.table_base;
	ldso.relocated = 1;
	wasm_set_ctors(&ldso, self.ctors);
	/* (not the app's program headers: __dls3 takes "the kernel loaded the app") */
	ldso.phdr = (void *)&ldso;
	head = &ldso;

	/* (the arguments, then the stack mainline reads, in one mapping: no malloc before the thread pointer) */
	args = wasm_mmap(2 * args_size);
	if (!args || __syscall(SYS_wasm_get_args, args, args_size) < 0) a_crash();
	for (n=0; args->envp[n]; n++);
	auxv = (size_t *)(args->envp + n + 1);
	for (na=0; auxv[na]; na+=2);
	sp = (size_t *)((char *)args + args_size);
	if ((args->argc + n + na + 5) * sizeof *sp > args_size) a_crash();
	sp[0] = args->argc;
	for (i=0; i<(size_t)args->argc; i++) sp[1+i] = (size_t)args->argv[i];
	sp[1+i] = 0;
	for (i=0; i<n; i++) sp[2+args->argc+i] = (size_t)args->envp[i];
	sp[2+args->argc+n] = 0;
	auxv = sp + 3 + args->argc + n;
	for (i=0; i<na+2; i++) auxv[i] = ((size_t *)(args->envp + n + 1))[i];
	__dls2b(sp, auxv);
}
