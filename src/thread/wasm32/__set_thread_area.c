#include <stdint.h>

uintptr_t __wasm_tp;

int __set_thread_area(void *p)
{
	__wasm_tp = (uintptr_t)p;
	return 0;
}
