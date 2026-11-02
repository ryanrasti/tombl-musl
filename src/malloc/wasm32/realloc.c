/* wasm32: as ../realloc.c, its name a weak alias (dynamic.list) */
#define realloc __wasm_realloc
#include "../realloc.c"
#undef realloc
weak_alias(__wasm_realloc, realloc);
