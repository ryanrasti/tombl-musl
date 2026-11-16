/* static linking only: there is no dynamic linker on wasm32 yet */
#define LDSO_ARCH "wasm32"

#define TPOFF_K 0

#define REL_SYMBOLIC    1
#define REL_PLT         2
#define REL_RELATIVE    3
#define REL_COPY        4
#define REL_DTPMOD      5
#define REL_DTPOFF      6
#define REL_TPOFF       7

#define CRTJMP(pc,sp) __builtin_trap()
