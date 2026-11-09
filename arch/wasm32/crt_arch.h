#include "process_args.h"
#include "syscall.h"

#ifndef __PIC__
hidden struct wasm_process_args *__wasm_process_args;
#endif

#ifdef START_is_dlstart
#include <elf.h>
hidden void _dlstart_c(size_t *sp, size_t *dynv);
/* stage 2's entry: its address as code takes it (__table_base + k), not a
 * pointer in data, which only stage 2 relocates */
#define GETFUNCSYM(fp, sym, got) do { hidden void sym(); *(fp) = sym; } while(0)
/*
 * The interpreter's entry (exec's e_entry). Its memory holds nothing yet
 * (its data is its start function's to copy), so first, with only locals:
 * the arguments the kernel gives once (wasm_get_args), here on the stack, as
 * mainline's entry gets them (argc, argv, envp, auxv); its ELF header at
 * AT_BASE, and from its dynamic section its start function and
 * __wasm_apply_data_relocs (DT_WASM_START, DT_WASM_RELOCS: table slots past
 * its own __table_base), which it runs; then mainline's stage 1.
 */
__asm__(".globaltype __table_base, i32, immutable\n");
__asm__(".globaltype __stack_pointer, i32\n");
/* the main thread's stack's top, exec's 1 MiB mapping's (all of it
 * under this, the program's frames as well): pthread_getattr_np's */
hidden uintptr_t __wasm_main_stack;
static __attribute__((noinline)) void dlstart_args(uintptr_t top)
{
	char buf[65536 * 4] __attribute__((aligned(16)));
	struct wasm_process_args *args = (void *)buf;
	size_t i, n, na, *auxv, base = 0, tb, *dynv;
	if (__syscall(SYS_wasm_get_args, args, sizeof buf) < 0) __builtin_trap();
	for (n=0; args->envp[n]; n++);
	auxv = (size_t *)(args->envp + n + 1);
	for (na=0; auxv[na]; na+=2) if (auxv[na] == AT_BASE) base = auxv[na+1];
	Elf32_Ehdr *eh = (void *)base;
	Elf32_Phdr *ph = (void *)(base + eh->e_phoff);
	for (i=eh->e_phnum; i && ph->p_type != PT_DYNAMIC; i--) ph = (void *)((char *)ph + eh->e_phentsize);
	dynv = (size_t *)(base + ph->p_vaddr);
	__asm__("global.get __table_base\nlocal.set %0" : "=r"(tb));
	for (i=0; dynv[i]; i+=2) if (dynv[i] == 0x60005741) ((void (*)(void))(tb + dynv[i+1] - 0x80000000))();
	for (i=0; dynv[i]; i+=2) if (dynv[i] == 0x60005742) ((void (*)(void))(tb + dynv[i+1] - 0x80000000))();
	/* (its data now in place) */
	__wasm_main_stack = top;
	size_t sp[args->argc + n + na + 5];
	sp[0] = args->argc;
	for (i=0; i<(size_t)args->argc; i++) sp[1+i] = (size_t)args->argv[i];
	sp[1+i] = 0;
	for (i=0; i<n; i++) sp[2+args->argc+i] = (size_t)args->envp[i];
	sp[2+args->argc+n] = 0;
	for (i=0; i<na+2; i++) sp[3+args->argc+n+i] = auxv[i];
	_dlstart_c(sp, dynv);
}
/* (no frame of its own: its stack pointer is the stack's top, as exec set it) */
void _dlstart(void)
{
	uintptr_t top;
	__asm__("global.get __stack_pointer\nlocal.set %0" : "=r"(top));
	dlstart_args(top);
}
#endif

#ifdef START_is_start
void __wasm_call_ctors(void);
int __main_void(void);
hidden void _start_c(long *p);
#ifdef __PIC__
/*
 * A position independent program (Scrt1.o), linked with libc.so: libc's
 * internals are hidden there, so start as other ports do, through
 * __libc_start_main, which runs the libraries' constructors and then this,
 * the program's, then main. The interpreter enters it (CRTJMP) with the
 * stack mainline's entry gets: argc, argv, envp, auxv (the kernel gives
 * the arguments once, to the interpreter).
 */
int __libc_start_main(int (*)(int, char **, char **), int, char **,
	void (*)(void), void (*)(void), void (*)(void));

/* clang names main after its signature (main(void) is __main_void, ...) */
weak int __main_void(void);
weak int __main_argc_argv(int, char **);
weak int main(int, char **, char **);

static int wasm_main(int argc, char **argv, char **envp)
{
	__wasm_call_ctors();
	/* (main(void) also defines a two-argument main: not ours to call) */
	if (__main_argc_argv)
		return __main_argc_argv(argc, argv);
	if (__main_void)
		return __main_void();
	return main(argc, argv, envp);
}

hidden void _start(long *p) {
	__libc_start_main(wasm_main, p[0], (char **)(p + 1), 0, 0, 0);
	__builtin_trap();
}
#else
#include <sys/mman.h>
/* the program's start function, a slot of its own table (exec's, from its ELF file's DT_WASM_START) */
#define AT_WASM_INIT 0x7763
/*
 * A static program's entry (exec's e_entry). Its memory holds nothing yet
 * (its data is its start function's to copy), so the arguments go where
 * that doesn't touch, a mapping of their own (the wasm process ABI caps
 * them at four 64 KiB pages), then the start function runs, in user space.
 */
hidden void _start(void) {
	const size_t args_size = 65536 * 4;
	long r = __syscall(SYS_mmap2, 0, args_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	struct wasm_process_args *args = (void *)r;
	size_t i, n, *auxv;
	if ((unsigned long)r > -4096UL || __syscall(SYS_wasm_get_args, args, args_size) < 0) __builtin_trap();
	for (n=0; args->envp[n]; n++);
	auxv = (size_t *)(args->envp + n + 1);
	for (i=0; auxv[i]; i+=2) if (auxv[i] == AT_WASM_INIT) ((void (*)(void))auxv[i+1])();
	__wasm_process_args = args;

	__init_libc(__wasm_process_args->envp, __wasm_process_args->argv[0]);
	__libc_start_init();
	__wasm_call_ctors();

	exit(__main_void());
}
#endif
#endif
