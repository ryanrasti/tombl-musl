/*
 * dlopen for processes in shared builds (WASM_SHARED_LIBC): as ld.so does, we
 * find the library (a bare name: in the loaded modules' run paths, then
 * /lib and /usr/lib), load the libraries it needs first, then read it and the
 * host's loader links it into the process (dyn.load); we run constructors
 * here. A library already loaded under the name isn't loaded again. Static
 * builds: unsupported, as generic musl.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "dynlink.h"
#include "lock.h"
#include "pthread_impl.h"

#ifdef WASM_SHARED_LIBC
__attribute__((import_module("dyn"), import_name("load")))
int __dyn_load(const void *bytes, size_t len, const char *name);
__attribute__((import_module("dyn"), import_name("find")))
int __dyn_find(const char *name);
__attribute__((import_module("dyn"), import_name("needed")))
int __dyn_needed(const void *bytes, size_t len, int i, char *out, size_t size);
__attribute__((import_module("dyn"), import_name("runpath")))
int __dyn_runpath(const void *bytes, size_t len, int i, char *out, size_t size);
__attribute__((import_module("dyn"), import_name("next_ctor")))
void (*__dyn_next_ctor(void))(void);

#define MAX_DEPTH 16

/*
 * One dlopen at a time, as ld.so's lock: threads switch at the syscalls it
 * makes, and two loading one library would both find it not loaded yet.
 * Reentrant, for constructors that dlopen.
 */
static volatile int lock[1];
static pthread_t owner;
static int held;

static void dl_lock(void)
{
	pthread_t self = __pthread_self();

	if (owner != self) {
		LOCK(lock);
		owner = self;
	}
	held++;
}

static void dl_unlock(void)
{
	if (--held)
		return;
	owner = 0;
	UNLOCK(lock);
}

static int load_name(const char *name, const void *req, size_t reqlen, int depth);

static const char *base(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

/* read the whole file; 0 on failure (errors set) */
static char *slurp(const char *path, size_t *len)
{
	struct stat st;
	char *bytes = 0;
	ssize_t n = 0;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return 0;
	if (fstat(fd, &st) < 0 || !(bytes = malloc(st.st_size + 1)))
		goto fail;
	for (off_t off = 0; off < st.st_size; off += n)
		if ((n = read(fd, bytes + off, st.st_size - off)) <= 0)
			goto fail;
	close(fd);
	*len = st.st_size;
	return bytes;
fail:
	free(bytes);
	close(fd);
	return 0;
}

/* load the library at path (registered as name), its needs first */
static int load_path(const char *path, const char *name, int depth)
{
	char dep[NAME_MAX + 1];
	size_t len;
	char *bytes = slurp(path, &len);
	int handle = 0;

	if (!bytes) {
		__dl_seterr("Error loading shared library %s", path);
		return 0;
	}
	for (int i = 0; __dyn_needed(bytes, len, i, dep, sizeof dep) >= 0; i++) {
		if (__dyn_find(dep))
			continue;
		if (!load_name(dep, bytes, len, depth + 1))
			goto out; /* error set */
	}
	handle = __dyn_load(bytes, len, name);
	if (!handle) {
		__dl_seterr("Error linking shared library %s", path);
		goto out;
	}
	for (void (*ctor)(void); (ctor = __dyn_next_ctor()); )
		ctor();
out:
	free(bytes);
	return handle;
}

/*
 * a bare name: in the run paths of the module that needs it (req), or for
 * dlopen itself (req 0) of all loaded modules; then /lib and /usr/lib
 */
static int load_name(const char *name, const void *req, size_t reqlen, int depth)
{
	/* (on the heap: a thread's stack is small, and this recurses) */
	char *dir, *path;
	int handle = __dyn_find(name);

	if (handle)
		return handle;
	if (depth > MAX_DEPTH) {
		__dl_seterr("Error loading shared library %s: too many levels", name);
		return 0;
	}
	if (!(dir = malloc(2 * PATH_MAX))) {
		__dl_seterr("Error loading shared library %s: out of memory", name);
		return 0;
	}
	path = dir + PATH_MAX;
	for (int i = 0; __dyn_runpath(req, reqlen, i, dir, PATH_MAX) >= 0; i++)
		if (snprintf(path, PATH_MAX, "%s/%s", dir, name) < PATH_MAX &&
		    access(path, F_OK) == 0)
			goto found;
	for (int i = 0; i < 2; i++) {
		snprintf(path, PATH_MAX, "%s/%s", i ? "/usr/lib" : "/lib", name);
		if (access(path, F_OK) == 0)
			goto found;
	}
	free(dir);
	__dl_seterr("Error loading shared library %s: not found", name);
	return 0;
found:
	handle = load_path(path, name, depth);
	free(dir);
	return handle;
}

void *dlopen(const char *file, int mode)
{
	int handle;

	(void)mode;	/* always RTLD_NOW, RTLD_GLOBAL */
	if (!file) {
		__dl_seterr("dlopen: no handle for the program itself");
		return 0;
	}
	dl_lock();
	if (strchr(file, '/')) {
		handle = __dyn_find(base(file));
		if (!handle)
			handle = load_path(file, base(file), 0);
	} else {
		handle = load_name(file, 0, 0, 0);
	}
	dl_unlock();
	return handle ? (void *)(long)handle : 0;
}
#else
static void *stub_dlopen(const char *file, int mode)
{
	__dl_seterr("Dynamic loading not supported");
	return 0;
}

weak_alias(stub_dlopen, dlopen);
#endif
