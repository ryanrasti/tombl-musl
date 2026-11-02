/* wasm32: as ../calloc.c, its name a weak alias (dynamic.list) */
#define calloc __wasm_calloc
#include "../calloc.c"
#undef calloc
weak_alias(__wasm_calloc, calloc);
