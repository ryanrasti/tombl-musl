/* wasm32: as ../free.c, its name a weak alias (dynamic.list) */
#define free __wasm_free
#include "../free.c"
#undef free
weak_alias(__wasm_free, free);
