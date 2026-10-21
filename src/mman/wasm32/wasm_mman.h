/* mmap and its relatives in libc (wasm_mman.c): negative errnos, as syscalls */
#include <sys/types.h>
#include <features.h>

hidden long __wasm_mmap(long addr, long len, long prot, long flags, long fd, off_t off);
hidden long __wasm_munmap(long addr, long len);
hidden long __wasm_mremap(long old, long old_len, long new_len, long flags);
hidden long __wasm_msync(long addr, long len);
/* every writable shared mapping written back: before exec and exit, and on fsync and sync */
hidden void __wasm_shared_flush(void);
