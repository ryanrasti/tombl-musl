/* wasm32: as ../aligned_alloc.c, its name a weak alias (dynamic.list) */
#define aligned_alloc __wasm_aligned_alloc
#include "../aligned_alloc.c"
#undef aligned_alloc
weak_alias(__wasm_aligned_alloc, aligned_alloc);
