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
