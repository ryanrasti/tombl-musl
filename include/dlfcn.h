#ifndef	_DLFCN_H
#define	_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_NOLOAD 4
#define RTLD_NODELETE 4096
#define RTLD_GLOBAL 256
#define RTLD_LOCAL  0

#define RTLD_NEXT    ((void *)-1)
#define RTLD_DEFAULT ((void *)0)

#define RTLD_DI_LINKMAP 2

int    dlclose(void *);
char  *dlerror(void);
void  *dlopen(const char *, int);
#ifndef __wasm__
void  *dlsym(void *__restrict, const char *__restrict);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
int dladdr(const void *, Dl_info *);
int dlinfo(void *, int, void *);
#endif

#if _REDIR_TIME64
__REDIR(dlsym, __dlsym_time64);
#endif

#if defined(__wasm__)
/* (wasm has no return address for dlsym to tell its caller's module by,
 * as RTLD_NEXT needs: the caller passes an address of its own module's,
 * wasm-ld's __dso_handle, which every module has) */
void *__dlsym_from(void *__restrict, const char *__restrict, void *);
extern void *__dso_handle __attribute__((__visibility__("hidden")));
static __inline __attribute__((__always_inline__)) void *dlsym(void *__restrict __p, const char *__restrict __s)
{
	return __dlsym_from(__p, __s, &__dso_handle);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
