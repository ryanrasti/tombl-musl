#define LDSO_ARCH "wasm32"

/* the loader is ldso/wasm32/dynlink.c's (ldso/dynlink.c's hooks) */
#define DL_WASM 1

/* (the program's entry, as __dls3 jumps to it: its _start, a table slot, given the stack) */
#define CRTJMP(pc, sp) __wasm_dls_enter((void *)(sp))