/* as upstream, writing back writable shared mappings first (src/mman/wasm32/wasm_mman.c) */
#include <unistd.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __wasm_shared_flush);

void sync(void)
{
	__wasm_shared_flush();
	__syscall(SYS_sync);
}
