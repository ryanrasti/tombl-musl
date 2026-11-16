	# a CLONE_VM child's start, on its stack, its start block (clone.c) at
	# the top: the host calls it, from the block's first word
	.globaltype __stack_pointer, i32
	.functype __wasm_clone_main (i32) -> ()
	.globl __wasm_clone_start
	.type __wasm_clone_start,@function
__wasm_clone_start:
	.functype __wasm_clone_start (i32) -> ()
	local.get 0
	global.set __stack_pointer
	local.get 0
	call __wasm_clone_main
	end_function

	# the base the compiler's thread-local accesses add a variable's
	# address to (with the atomics feature: else it makes them globals),
	# the running thread's: this libc's own, not the linker's (a blob's
	# programs each have a libc of their own; the blob's link makes each
	# program's TLS plain data, its image: scripts/wasm-localize.py's
	# --tls), set with the thread pointer (__set_thread_area.c)
	.globaltype __tls_base, i32
	.globl __tls_base
__tls_base:

	.globl __wasm_get_tls_base
	.hidden __wasm_get_tls_base
	.type __wasm_get_tls_base,@function
__wasm_get_tls_base:
	.functype __wasm_get_tls_base () -> (i32)
	global.get __tls_base
	end_function

	.globl __wasm_set_tls_base
	.hidden __wasm_set_tls_base
	.type __wasm_set_tls_base,@function
__wasm_set_tls_base:
	.functype __wasm_set_tls_base (i32) -> ()
	local.get 0
	global.set __tls_base
	end_function

	# the cancellation points' labels (syscall_cp.c): bytes of one object,
	# in order, as a native syscall_cp.s's code; only their addresses count
	.section .rodata.__cp_begin,"",@
	.globl __cp_begin
	.hidden __cp_begin
	.type __cp_begin,@object
__cp_begin:
	.int8 0
	.size __cp_begin, 1
	.globl __cp_end
	.hidden __cp_end
	.type __cp_end,@object
__cp_end:
	.int8 0
	.size __cp_end, 1
	.globl __cp_cancel
	.hidden __cp_cancel
	.type __cp_cancel,@object
__cp_cancel:
	.int8 0
	.size __cp_cancel, 1
