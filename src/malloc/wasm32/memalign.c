/* wasm32: as ../memalign.c, its name a weak alias (dynamic.list) */
#define memalign __wasm_memalign
#include "../memalign.c"
#undef memalign
weak_alias(__wasm_memalign, memalign);
