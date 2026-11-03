#define LDSO_ARCH "wasm32"

/* the code is a wasm module (ldso/wasm32/dynlink.c); no MMU */
#define DL_WASM 1
#define DL_NOMMU_SUPPORT 1

/* a data word: S + A (a function's S its table slot); a GOT word of a
 * function import's (lazy binding's); B + A */
#define R_WASM32_32 1
#define R_WASM32_JMP_SLOT 2
#define R_WASM32_RELATIVE 3

#define REL_SYMBOLIC R_WASM32_32
#define REL_PLT R_WASM32_JMP_SLOT
#define REL_RELATIVE R_WASM32_RELATIVE

/* the program's entry: a table slot, given the stack */
#define CRTJMP(pc, sp) ((void (*)(size_t *))(pc))((size_t *)(sp))
