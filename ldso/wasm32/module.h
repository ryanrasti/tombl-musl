/*
 * The wasm loader's records, shared with the host (linux-workers'
 * src/kernel/user/loader.ts): what wasm_load_module returns of a module,
 * what wasm_link_module returns of an instance, and each dso's handles.
 */
#ifndef _LDSO_WASM32_MODULE_H
#define _LDSO_WASM32_MODULE_H

#include <stddef.h>
#include <stdint.h>

/*
 * wasm_load_module's info: this header, then a dynamic section of `dyn`
 * size_t pairs (DT_NEEDED, DT_RUNPATH as offsets into the string table;
 * DT_STRTAB the string table's offset from the start of the info; DT_STRSZ;
 * DT_NULL-ended), then the string table
 */
struct wasm_module_info {
	uint32_t mem_size, mem_align, table_size, tls_size, tls_align, dyn;
};

/* wasm_link_module's flags */
#define WASM_LINK_LAZY 1	/* an undefined function bound when something defines it */
#define WASM_LINK_COMMIT 2	/* mh 0: every pending import bound over scope, in its order (first wins), else the first undefined's name (ENOENT) */

/* wasm_link_module's out: the instance's table base, and its functions musl calls (table slots, 0: none) */
struct wasm_link_out {
	uint32_t memory_base, table_base, start, relocs, ctors;
	char undefined[256];	/* an undefined symbol's name (ENOENT) */
};

/* wasm_module_sym's flags */
#define WASM_SYM_ADDR 1	/* name is an address: the nearest export's name and address into out; its instance */
#define WASM_SYM_INIT 2	/* out: a struct wasm_link_out of the instance (its bases and slots) */
#define WASM_SYM_OWN 4	/* the instance's own exports alone, not its dependencies' */

/* a dso's wasm side (struct dso's `wasm`) */
struct wasm_dso {
	int module, instance;
	char lazy;
	uint32_t table_base, table_size;
	uint32_t start, relocs;	/* its start function's and relocations' slots, till they've run */
	void *names;	/* dladdr's names, kept as long as the library */
};

#endif
