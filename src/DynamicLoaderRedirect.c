/*
 * Provides hooks for LD_PRELOAD on GoboLinux.
 * Written by Lucas C. Villa Real <lucasvr@gobolinux.org>
 * Released under the GNU GPL version 2
 *
 * Debugging:
 * LD_DEBUG=all
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>

#define GOBO_LIBRARIES    "/System/Links/Libraries"
#define GOBO_PROGRAMS     "/Programs"
#define GOBO_PROGRAMS_LEN  9

#define LOAD_SYMBOL(fn,name) do { \
	if (! (fn)) { \
		(fn) = dlsym(RTLD_NEXT, name); \
		if (! (fn)) \
			return -1; \
	} \
} while(0)

#define RUN_STAT_HOOK(fn,arg1,arg2) do { \
	int ret = fn(arg1, arg2); \
	if (ret != 0) { \
		char *sys_path = _getSystemPath(path); \
		if (sys_path) { \
			ret = fn(sys_path, arg2); \
			free(sys_path); \
		} \
	} \
	return ret; \
} while(0)

#define RUN_XSTAT_HOOK(fn,ver,arg1,arg2) do { \
	int ret = fn(ver, arg1, arg2); \
	if (ret != 0) { \
		char *sys_path = _getSystemPath(path); \
		if (sys_path) { \
			ret = fn(ver, sys_path, arg2); \
			free(sys_path); \
		} \
	} \
	return ret; \
} while(0)

#define RUN_OPEN_HOOK(fn,arg1,arg2,arg3) do { \
	int ret = fn(arg1, arg2, arg3); \
	if (ret < 0) { \
		char *sys_path = _getSystemPath(path); \
		if (sys_path) { \
			ret = fn(sys_path, arg2, arg3); \
			free(sys_path); \
		} \
	} \
	return ret; \
} while(0)


static char *_getSystemPath(const char *path)
{
	static int (*realstat)(const char *, struct stat *) = NULL;
	char *program, *version, *subdir, *rest;
	char *sys_path = NULL;
	int ret;
	
	if (! realstat) {
		realstat = dlsym(RTLD_NEXT, "stat");
		if (! realstat)
			return NULL;
	}

	if (strncmp(path, GOBO_PROGRAMS, GOBO_PROGRAMS_LEN) || path[0] != '/')
		return NULL;

	program = strstr(path+1, "/");
	if (! program)
		return NULL;

	version = strstr(program+1, "/");
	if (! version)
		return NULL;

	subdir = strstr(version+1, "/");
	if (! subdir)
		return NULL;
	
	ret = asprintf(&sys_path, "%.*s", subdir-path, path);
	if (ret > 0) {
		struct stat statbuf;
		ret = (*realstat)(sys_path, &statbuf);
		free(sys_path);
		sys_path = NULL;
		if (ret < 0)
			return NULL;
	}
	
	rest = strstr(subdir+1, "/");
	if (! rest)
		return NULL;

	if (! strncmp(subdir, "/lib/", 5)) {
		ret = asprintf(&sys_path, "%s/%s", GOBO_LIBRARIES, rest);
		if (ret < 0)
			return NULL;
	}

	return sys_path;
}

int stat(const char *path, struct stat *buf)
{
	static int (*realfn)(const char *, struct stat *) = NULL;
	LOAD_SYMBOL(realfn, "stat");
	RUN_STAT_HOOK((*realfn), path, buf);
}

int stat64(const char *path, struct stat64 *buf)
{
	static int (*realfn)(const char *, struct stat64 *) = NULL;
	LOAD_SYMBOL(realfn, "stat64");
	RUN_STAT_HOOK((*realfn), path, buf);
}

int lstat(const char *path, struct stat *buf)
{
	static int (*realfn)(const char *, struct stat *) = NULL;
	LOAD_SYMBOL(realfn, "lstat");
	RUN_STAT_HOOK((*realfn), path, buf);
}

int lstat64(const char *path, struct stat64 *buf)
{
	static int (*realfn)(const char *, struct stat64 *) = NULL;
	LOAD_SYMBOL(realfn, "lstat64");
	RUN_STAT_HOOK((*realfn), path, buf);
}

int __xstat(int ver, const char *path, struct stat *buf)
{
	static int (*realfn)(int ver, const char *, struct stat *) = NULL;
	LOAD_SYMBOL(realfn, "__xstat");
	RUN_XSTAT_HOOK((*realfn), ver, path, buf);
}

int __xstat64(int ver, const char *path, struct stat64 *buf)
{
	static int (*realfn)(int ver, const char *, struct stat64 *) = NULL;
	LOAD_SYMBOL(realfn, "__xstat64");
	RUN_XSTAT_HOOK((*realfn), ver, path, buf);
}

int __lxstat(int ver, const char *path, struct stat *buf)
{
	static int (*realfn)(int ver, const char *, struct stat *) = NULL;
	LOAD_SYMBOL(realfn, "__lxstat");
	RUN_XSTAT_HOOK((*realfn), ver, path, buf);
}

int __lxstat64(int ver, const char *path, struct stat64 *buf)
{
	static int (*realfn)(int ver, const char *, struct stat64 *) = NULL;
	LOAD_SYMBOL(realfn, "__lxstat64");
	RUN_XSTAT_HOOK((*realfn), ver, path, buf);
}

int open(const char *path, int flags, ...)
{
	static int (*realfn)(const char *, ...) = NULL;
	mode_t mode = 0;

	if (flags & O_CREAT) {
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, mode_t);
		va_end(arg);
	}
	
	LOAD_SYMBOL(realfn, "open");
	RUN_OPEN_HOOK((*realfn), path, flags, mode);
}

int open64(const char *path, int flags, ...)
{
	static int (*realfn)(const char *, int, ...) = NULL;
	mode_t mode = 0;
	
	if (flags & O_CREAT) {
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, mode_t);
		va_end(arg);
	}

	LOAD_SYMBOL(realfn, "open64");
	RUN_OPEN_HOOK((*realfn), path, flags, mode);
}
