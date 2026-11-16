# setjmp.c's __THREW__ and __threwValue thread-local, as the compiler's
# lowering of longjmp and exceptions takes them with the atomics feature
# (programs': for their thread-local storage), which it needs for that
obj/src/setjmp/wasm32/setjmp.o obj/src/setjmp/wasm32/setjmp.lo: CFLAGS_ALL += -matomics
