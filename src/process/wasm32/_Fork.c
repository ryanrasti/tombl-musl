/* _Fork, as upstream, by clone returning twice (fork_syscall.c) */
#include <unistd.h>
#include <signal.h>
#include "syscall.h"
#include "libc.h"
#include "lock.h"
#include "pthread_impl.h"
#include "aio_impl.h"
#include "fork_impl.h"

hidden long __wasm_fork(void);

static void dummy(int x) { }
weak_alias(dummy, __aio_atfork);
weak_alias(dummy, __wasm_mman_atfork);

void __post_Fork(int ret)
{
	if (!ret) {
		pthread_t self = __pthread_self();
		self->tid = __syscall(SYS_set_tid_address, &__thread_list_lock);
		self->robust_list.off = 0;
		self->robust_list.pending = 0;
		self->next = self->prev = self;
		__thread_list_lock = 0;
		libc.threads_minus_1 = 0;
		if (libc.need_locks) libc.need_locks = -1;
	}
	UNLOCK(__abort_lock);
	if (!ret) __aio_atfork(1);
	/* (a child's shared mappings' snapshots its own memory, its locks fresh: wasm_mman.c; fork's or _Fork's) */
	if (!ret) __wasm_mman_atfork(1);
}

pid_t _Fork(void)
{
	pid_t ret;
	sigset_t set;
	__block_all_sigs(&set);
	LOCK(__abort_lock);
	ret = __wasm_fork();
	__post_Fork(ret);
	__restore_sigs(&set);
	return __syscall_ret(ret);
}
