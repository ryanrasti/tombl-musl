/*
 * fork: clone with neither a function nor CLONE_VM returns twice, in the
 * parent and in the child, whose memory is a copy (exobox's kernel and
 * host: arch/wasm/exobox/fork.c, linux-workers' src/kernel/user/fork.ts).
 * A wasm stack can't be copied, so the program is Asyncify-instrumented
 * and saves its stack into its memory, into this buffer, which the host
 * finds by its export: Asyncify's header (where saved frames go next, and
 * the buffer's end), then where it starts, for the host to reset next to.
 * Each thread's own: one thread's fork, or a vfork parent's stack saved
 * as its child runs, isn't another's to overwrite. Only programs that can
 * fork link it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include "pthread_impl.h"
#include "syscall.h"

#define FORK_BUFFER_SIZE (128 * 1024)

/* Asyncify's header (next, end), then where the saved frames start; the frames after it, 16-aligned */
struct fork_buffer {
	char *next, *end, *start;
};
#define FORK_HEADER 16

#ifdef WASM_SHARED_LIBC
/*
 * (in libc.so, unused: the loader gives each module of a dynamically
 * linked process a buffer of its own)
 */
static char fork_stack[FORK_BUFFER_SIZE] __attribute__((aligned(16)));
static struct fork_buffer fork_buffer = { fork_stack, fork_stack + FORK_BUFFER_SIZE, fork_stack };

__attribute__((export_name("__wasm_fork_buffer")))
void *__wasm_fork_buffer(void)
{
	return &fork_buffer;
}

static int fork_prepare(void)
{
	return 0;
}
#else
hidden void *__mmap(void *, size_t, int, int, int, off_t);
hidden int __munmap(void *, size_t);

/*
 * The calling thread's, 0 till it first forks. Mapped by fork's own call
 * (fork_prepare), before the syscall: the host asks for it inside it,
 * when nothing may be allocated (malloc's lock is held). Not a
 * thread-local: Go's cgo linker refuses the relocation a __thread takes.
 */
__attribute__((export_name("__wasm_fork_buffer")))
void *__wasm_fork_buffer(void)
{
	return __pthread_self()->fork_buffer;
}

/* the thread's buffer, made if it has none (an mmap: the kernel's, no lock of libc's): 0, or -ENOMEM */
static int fork_prepare(void)
{
	pthread_t self = __pthread_self();
	struct fork_buffer *b;

	if (self->fork_buffer)
		return 0;
	b = __mmap(0, FORK_HEADER + FORK_BUFFER_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (b == MAP_FAILED)
		return -ENOMEM;
	b->start = b->next = (char *)b + FORK_HEADER;
	b->end = b->start + FORK_BUFFER_SIZE;
	self->fork_buffer = b;
	return 0;
}

/* a thread exiting: its buffer unmapped (pthread_create.c's __pthread_exit) */
hidden void __wasm_fork_release(pthread_t self)
{
	if (self->fork_buffer)
		__munmap(self->fork_buffer, FORK_HEADER + FORK_BUFFER_SIZE);
	self->fork_buffer = 0;
}
#endif

/* clone(fn, fn_arg, flags, ptid, ctid, tls) with no fn: fork */
hidden long __wasm_fork(void)
{
	int ret = fork_prepare();

	return ret ? ret : __syscall(SYS_clone, 0, 0, SIGCHLD, 0, 0, 0);
}

/*
 * with CLONE_VM | CLONE_VFORK: vfork, the child on this memory until it
 * execs or exits. (Nothing between vfork's caller and the syscall keeps
 * anything on the stack in memory, which the child's own calls overwrite
 * before the parent returns through them.)
 */
hidden long __wasm_vfork(void)
{
	int ret = fork_prepare();

	return ret ? ret : __syscall(SYS_clone, 0, 0, CLONE_VM | CLONE_VFORK | SIGCHLD, 0, 0, 0);
}
