/*
 * LD_PRELOAD hooks for seamless integration of Runner.
 *
 * Written by Lucas C. Villa Real <lucasvr@gobolinux.org>
 * Released under the GNU GPL version 2.
 *
 * Build with:
 * gcc RunnerRedirect.c -shared -fpic -ldl -o RunnerRedirect.so
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <spawn.h>
#include <assert.h>

static int   (*execl_orig)(const char *filename, const char *arg, ...);
static int   (*execlp_orig)(const char *filename, const char *arg, ...);
static int   (*execle_orig)(const char *filename, const char *arg, ...);
static int   (*execv_orig)(const char *filename, char *const argv[]);
static int   (*execvp_orig)(const char *filename, char *const argv[]);
static int   (*execvpe_orig)(const char *filename, char *const argv[], char *const envp[]);
static int   (*execve_orig)(const char *filename, char *const argv[], char *const envp[]);
static int   (*execveat_orig)(int dirfd, const char *filename, char *const argv[], char *const envp[], int flags);
static int   (*fexecve_orig)(int fd, char *const argv[], char *const envp[]);
static int   (*system_orig)(const char *command);
static FILE *(*popen_orig)(const char *command, const char *type);
static FILE *(*_IO_popen_orig)(const char *command, const char *type);
int          (*posix_spawn_orig)(pid_t *pid, const char *file,
                                 const posix_spawn_file_actions_t *file_actions,
                                 const posix_spawnattr_t *attrp,
                                 char *const argv[], char *const envp[]);
int          (*posix_spawnp_orig)(pid_t *pid, const char *file,
                                 const posix_spawn_file_actions_t *file_actions,
                                 const posix_spawnattr_t *attrp,
                                 char *const argv[], char *const envp[]);

__attribute__((constructor)) static void init()
{
	execl_orig        = dlsym(RTLD_NEXT, "execl");
	execlp_orig       = dlsym(RTLD_NEXT, "execlp");
	execle_orig       = dlsym(RTLD_NEXT, "execle");
	execv_orig        = dlsym(RTLD_NEXT, "execv");
	execvp_orig       = dlsym(RTLD_NEXT, "execvp");
	execvpe_orig      = dlsym(RTLD_NEXT, "execvpe");
	execve_orig       = dlsym(RTLD_NEXT, "execve");
	execveat_orig     = dlsym(RTLD_NEXT, "execveat");
	fexecve_orig      = dlsym(RTLD_NEXT, "fexecve");
	system_orig       = dlsym(RTLD_NEXT, "system");
	popen_orig        = dlsym(RTLD_NEXT, "popen");
	_IO_popen_orig    = dlsym(RTLD_NEXT, "_IO_popen");
	posix_spawn_orig  = dlsym(RTLD_NEXT, "posix_spawn");
	posix_spawnp_orig = dlsym(RTLD_NEXT, "posix_spawnp");

	assert(execl_orig != execl);
	assert(execv_orig != execv);
	assert(execvp_orig != execvp);
	assert(execvpe_orig != execvpe);
}

#define RESET_ENV() \
    unsetenv("LD_PRELOAD")

#define PRINT_CMD() \
	fprintf(stderr, "%s\n", cmd)

#define DECLARE_CMD() \
	char *cmd = NULL; \
	asprintf(&cmd, "/bin/Runner -f %s", command); \
	RESET_ENV(); \
	PRINT_CMD()

#define PRINT_ARGS() \
	for (i=0; args[i]; ++i) { fprintf(stderr, "%s ", args[i]); } \
	fprintf(stderr, "\n")

#define DECLARE_ARGS() \
	int i, argcount = 0; \
	while (argv[argcount++]) {} \
	char *args[argcount+3]; \
	args[0] = "/bin/Runner"; \
	args[1] = "-f"; \
	for (i=1; i<argcount; ++i) { args[1+i] = argv[i-1]; } \
	args[1+argcount] = NULL; \
	RESET_ENV(); \
	PRINT_ARGS()

#define DECLARE_VA_ARGS() \
	int i, argcount = 1; \
	const char **args = (const char **) malloc(sizeof(const char *) * 4); \
	args[0] = "/bin/Runner"; \
	args[1] = "-f"; \
	args[2] = arg; \
	args[3] = NULL; \
	va_list ap; \
	va_start(ap, arg); \
	while (args[argcount++]) { \
		args = (const char **) realloc(args, sizeof(const char *) * (argcount + 4)); \
		args[1+argcount] = va_arg(ap, char *); \
	} \
	va_end(ap); \
	RESET_ENV(); \
	PRINT_ARGS()

int execl(const char *filename, const char *arg, ...)
{
	DECLARE_VA_ARGS();
	return execv_orig(args[0], (char * const *) args);
}

int execlp(const char *filename, const char *arg, ...)
{
	DECLARE_VA_ARGS();
	return execvp_orig(args[0], (char * const *) args);
}

int execle(const char *filename, const char *arg, ...)
{
	DECLARE_VA_ARGS();
	return execv_orig(args[0], (char * const *) args);
}

int execv(const char *filename, char *const argv[])
{
	DECLARE_ARGS();
	return execv_orig(args[0], args);
}

int execvp(const char *filename, char *const argv[])
{
	DECLARE_ARGS();
	return execvp_orig(args[0], args);
}

int execvpe(const char *filename, char *const argv[], char *const envp[])
{
	DECLARE_ARGS();
	return execvpe_orig(args[0], args, envp);
}

int execve(const char *filename, char *const argv[], char *const envp[])
{
	DECLARE_ARGS();
    return execve_orig(args[0], args, envp);
}

int execveat(int dirfd, const char *filename, char *const argv[], char *const envp[], int flags)
{
	DECLARE_ARGS();
    return execveat_orig(dirfd, args[0], args, envp, flags);
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
	DECLARE_ARGS();
    return fexecve_orig(fd, args, envp);
}

int system(const char *command)
{
	DECLARE_CMD();
    return system_orig(cmd);
}

FILE *popen(const char *command, const char *type)
{
	DECLARE_CMD();
    return popen_orig(cmd, type);
}

FILE *_IO_popen(const char *command, const char *type)
{
	DECLARE_CMD();
    return _IO_popen_orig(cmd, type);
}

int posix_spawn(pid_t *pid, const char *file,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[])
{
	DECLARE_ARGS();
    return posix_spawn_orig(pid, file, file_actions, attrp, args, envp);
}

int posix_spawnp(pid_t *pid, const char *file,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[])
{
	DECLARE_ARGS();
    return posix_spawnp_orig(pid, file, file_actions, attrp, args, envp);
}
