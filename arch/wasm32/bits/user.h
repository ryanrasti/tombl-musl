/*
 * a wasm thread's registers: the few slots a ucontext has (bits/signal.h's
 * gregs); no floating-point state
 */
#define ELF_NGREG 16
typedef unsigned long elf_greg_t, elf_gregset_t[ELF_NGREG];
typedef struct { unsigned long __unused; } elf_fpregset_t;
