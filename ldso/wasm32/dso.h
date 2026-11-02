/* A wasm32 dso's code: its PT_WASM segment, a wasm module, compiled by the
 * host (wasm_load_code) and instantiated at the dso's bases
 * (wasm_instantiate). Its functions' addresses are table slots: vaddr
 * WASM_FUNC_VADDR + k is table_base + k, for k below table_size (the
 * segment's p_memsz). */
#define PT_WASM 0x60005741
#define WASM_FUNC_VADDR 0x80000000
/* the program's code handle and table base, from exec (auxv) */
#define AT_WASM_CODE 0x7761
#define AT_WASM_TABLE 0x7762
/* its start function (its memory initialized) and __wasm_apply_data_relocs, run once as it's loaded */
#define DT_WASM_START 0x60005741
#define DT_WASM_RELOCS 0x60005742

struct wasm_dso {
	int code;	/* the host's handle; 0: the interpreter, exec's instance */
	size_t table_base, table_size;
	size_t memory_vaddr;	/* the first writable PT_LOAD's: the module's __memory_base */
	size_t code_off, code_size;
};
