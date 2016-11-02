/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2015 Lucas C. Villa Real <lucasvr@gobolinux.org>
 * Copyright (C) 2014 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2012 Alexander Larsson <alexl@redhat.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE /* Required for CLONE_NEWNS */

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <linux/version.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/statfs.h>

#include "LinuxList.h"
#include "FindDependencies.h"

#define GOBO_INDEX_DIR    "/System/Index"
#define GOBO_PROGRAMS_DIR "/Programs"
#define OVERLAYFS_MAGIC   0x794c7630

#define debug_printf(msg...) if (args.verbose) fprintf(stderr, msg)
#define error_printf(fun, msg) fprintf(stderr, "Error:%s: %s at %s:%d\n", #fun, msg, __FILE__, __LINE__)

#define ERR_LEN 255
#define CHECK(x, use_perror) do { \
    int retval = (x); \
    char err_msg[ERR_LEN] = "Unexpected error"; \
    if (retval) { \
        if (use_perror) { \
            strncpy(err_msg, strerror(errno), ERR_LEN-1); \
        } \
        else { \
            switch(retval) { \
                case ERR_OUTMEMORY: \
                    strncpy(err_msg, "Not enough memory", ERR_LEN-1); \
                    break; \
            } \
        } \
        error_printf(x, err_msg); \
        exit(retval); \
    } \
} while (0)

#define ERR_OUTMEMORY         1      /* Out of memory */
#define ERR_NOEXECUTABLE      2      /* No executable */
#define ERR_NOSANDBOX         3      /* No sandbox available */
#define ERR_MNT_NAMESPACE     4      /* Error creating mount namespace */
#define ERR_MNT_OVERLAY       5      /* Error mounting overlay fs */
#define ERR_BAD_ARGS          6      /* Bad arguments */

struct runner_args {
	bool quiet;                /* Run in quiet mode? */
	bool verbose;              /* Run in verbose mode? */
	bool check;                /* Run in check mode? */
	bool fallback;             /* Run in fallback mode if sandbox is not available */
	const char *executable;    /* Executable to run */
	const char **dependencies; /* NULL-terminated */
	char **arguments;          /* Arguments to pass to executable */
	char *architecture;        /* Architecture of dependencies to consider */
};
static struct runner_args args;


/**
 * compare_kernel_versions:
 *
 * Compare alpha and numeric segments of two versions.
 * This algorithm is also used in RPM and licensed under a GPLv2+
 * license.
 *
 * Returns: 1: a is newer than b
 *		  0: a and b are the same version
 *		 -1: b is newer than a
 */
int
compare_kernel_versions(const char* a, const char *b)
{
	/* easy comparison to see if versions are identical */
	if (strcmp(a, b) == 0)
		return 0;

	char oldch1, oldch2;
	char abuf[strlen(a)+1], bbuf[strlen(b)+1];
	char *str1 = abuf, *str2 = bbuf;
	char *one, *two;
	int rc;
	bool isnum;

	strcpy(str1, a);
	strcpy(str2, b);

	one = str1;
	two = str2;

	/* loop through each version segment of str1 and str2 and compare them */
	while (*one || *two) {
		while (*one && !isalnum(*one) && *one != '~') one++;
		while (*two && !isalnum(*two) && *two != '~') two++;

		/* handle the tilde separator, it sorts before everything else */
		if (*one == '~' || *two == '~') {
			if (*one != '~') return 1;
			if (*two != '~') return -1;
			one++;
			two++;
			continue;
		}

		/* If we ran to the end of either, we are finished with the loop */
		if (!(*one && *two)) break;

		str1 = one;
		str2 = two;

		/* grab first completely alpha or completely numeric segment */
		/* leave one and two pointing to the start of the alpha or numeric */
		/* segment and walk str1 and str2 to end of segment */
		if (isdigit(*str1)) {
			while (*str1 && isdigit(*str1)) str1++;
			while (*str2 && isdigit(*str2)) str2++;
			isnum = true;
		} else {
			while (*str1 && isalpha(*str1)) str1++;
			while (*str2 && isalpha(*str2)) str2++;
			isnum = false;
		}

		/* save character at the end of the alpha or numeric segment */
		/* so that they can be restored after the comparison */
		oldch1 = *str1;
		*str1 = '\0';
		oldch2 = *str2;
		*str2 = '\0';

		/* this cannot happen, as we previously tested to make sure that */
		/* the first string has a non-null segment */
		if (one == str1) return -1;	/* arbitrary */

		/* take care of the case where the two version segments are */
		/* different types: one numeric, the other alpha (i.e. empty) */
		/* numeric segments are always newer than alpha segments */
		if (two == str2) return isnum ? 1 : -1;

		if (isnum) {
			size_t onelen, twolen;
			/* this used to be done by converting the digit segments */
			/* to ints using atoi() - it's changed because long  */
			/* digit segments can overflow an int - this should fix that. */

			/* throw away any leading zeros - it's a number, right? */
			while (*one == '0') one++;
			while (*two == '0') two++;

			/* whichever number has more digits wins */
			onelen = strlen(one);
			twolen = strlen(two);
			if (onelen > twolen) return 1;
			if (twolen > onelen) return -1;
		}

		/* strcmp will return which one is greater - even if the two */
		/* segments are alpha or if they are numeric.  don't return  */
		/* if they are equal because there might be more segments to */
		/* compare */
		rc = strcmp(one, two);
		if (rc) return rc < 1 ? -1 : 1;

		/* restore character that was replaced by null above */
		*str1 = oldch1;
		one = str1;
		*str2 = oldch2;
		two = str2;
	}

	/* this catches the case where all numeric and alpha segments have */
	/* compared identically but the segment sepparating characters were */
	/* different */
	if ((!*one) && (!*two)) return 0;

	/* whichever version still has characters left over wins */
	if (!*one) return -1; else return 1;
}

/**
 * Finds the path where an executable is installed.
 * @param executable Executable to search
 * @return A malloc'd string with the resolved path on success, or NULL
 * on failure.
 */
char *
which(const char *executable)
{
	const char *default_path = "/bin";
	const char *path;
	char *testpath, *path_copy, *ptr, *start;
	struct stat statbuf;

	path = getenv("PATH");
	if (! path)
		path = default_path;

	path_copy = strdup(path);
	if (! path_copy) {
		fprintf(stderr, "Not enough memory\n");
		return NULL;
	}
	for (start=ptr=path_copy; /* no-op */; ptr++) {
		bool eos = *ptr == '\0';
		if (*ptr == ':' || *ptr == '\0') {
			*ptr = '\0';
			if (asprintf(&testpath, "%s/%s", start, executable) <= 0) {
				fprintf(stderr, "Not enough memory\n");
				free(path_copy);
				return NULL;
			}
			if (stat(testpath, &statbuf) == 0) {
				free(path_copy);
				return testpath;
			}
			free(testpath);
			start = ++ptr;
		}
		if (eos)
			break;
	}
	return NULL;
}

/**
 * Get the path to /Programs/App/Version of a given executable.
 * @param executable Executable to search the goboPrograms entry for
 * @return A malloc'd string with the resolved path on success, or NULL
 * on failure.
 */ 
char *
get_program_dir(const char *executable)
{
	char *path, *exec, *ptr;
	int i, count;
	
	path = realpath(executable, NULL);
	if (path == NULL) {
		exec = which(executable);
		if (! exec) {
			fprintf(stderr, "Unable to resolve path to '%s': %s\n",
					executable, strerror(errno));
			return NULL;
		}
		path = realpath(exec, NULL);
		if (path == NULL) {
			fprintf(stderr, "Unable to resolve path to '%s': %s\n",
					executable, strerror(errno));
			free(exec);
			return NULL;
		}
		free(exec);
	}

	if (strlen(path) < strlen(GOBO_PROGRAMS_DIR)) {
		debug_printf("'%s' ('%s') is not in a $goboPrograms subdirectory\n",
			executable, path);
		free(path);
		return NULL;
	}

	ptr = &path[strlen(GOBO_PROGRAMS_DIR)];
	for (count=0, i=0; i<strlen(GOBO_PROGRAMS_DIR); ++i)
		if (path[i] == '/')
			count++;
	if (count != 1) {
		// Too many '/' components!
		debug_printf("'%s' ('%s') has too many '/' components\n",
			executable, path);
		free(path);
		return NULL;
	}
	while (*ptr) {
		if (*ptr == '/' && ++count == 4) {
			// Found it
			*ptr = '\0';
			return path;
		}
		ptr++;
	}
	free(path);
	return NULL;
}

/**
 * create_mount_namespace:
 */
static int
create_mount_namespace()
{
	int mount_count;
	int res;

	debug_printf("creating new namespace\n");
	res = unshare(CLONE_NEWNS);
	if (res != 0) {
		fprintf(stderr, "Failed to create namespace: %s\n", strerror(errno));
		return 1;
	}

	mount_count = 0;
	res = mount(GOBO_INDEX_DIR, GOBO_INDEX_DIR,
				NULL, MS_PRIVATE, NULL);
	debug_printf("mount(private) = %d\n", res);
	if (res != 0 && errno == EINVAL) {
		/* Maybe if failed because there is no mount
		 * to be made private at that point, lets
		 * add a bind mount there. */
		res = mount(GOBO_INDEX_DIR, GOBO_INDEX_DIR,
					NULL, MS_BIND, NULL);
		debug_printf("mount(bind) = %d\n", res);
		/* And try again */
		if (res == 0) {
			mount_count++; /* Bind mount succeeded */
			res = mount(GOBO_INDEX_DIR, GOBO_INDEX_DIR,
						NULL, MS_PRIVATE, NULL);
			debug_printf("mount(private) = %d\n", res);
		}
	}

	if (res != 0) {
		fprintf(stderr, "Failed to make prefix namespace private\n");
		goto error_out;
	}

	return 0;

error_out:
	while (mount_count-- > 0)
		umount(GOBO_INDEX_DIR);
	return 1;
}

static char *
prepare_merge_string(const char *callerprogram, const char *dependencies)
{
	struct search_options options;
	struct list_data *entry;
	struct list_head *deps;
	char *mergedirs = NULL;
	int mergedirs_len = callerprogram ? strlen(callerprogram) + 1: 0;
	struct stat statbuf;

	if (stat(dependencies, &statbuf) == 0 && statbuf.st_size == 0) {
		/* nothing to parse */
		return NULL;
	}

	memset(&options, 0, sizeof(options));
	options.repository = LOCAL_PROGRAMS;
	options.depsfile = dependencies;
	options.quiet = args.quiet;
	options.wantedArch = args.architecture;
	options.goboPrograms = GOBO_PROGRAMS_DIR;
	options.noOperator = EQUAL;

	deps = ParseDependencies(&options);
	if (!deps || list_empty(deps)) {
		if (! args.quiet)
			fprintf(stderr, "Could not resolve dependencies from %s\n", dependencies);
		goto out_free;
	}

	/* Allocate and prepare the mergedirs string */
	list_for_each_entry(entry, deps, list)
		mergedirs_len += strlen(entry->path) + 1;
	mergedirs_len++;
	
	mergedirs = calloc(mergedirs_len, sizeof(char));
	if (! mergedirs) {
		fprintf(stderr, "Not enough memory\n");
		goto out_free;
	}
	list_for_each_entry(entry, deps, list) {
		if (stat(entry->path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
			debug_printf("adding dependency at %s\n", entry->path);
			strcat(mergedirs, entry->path);
			strcat(mergedirs, ":");
		}
	}
	if (callerprogram) {
		strcat(mergedirs, callerprogram);
		strcat(mergedirs, ":");
	}
out_free:
	if (deps)
		FreeDependencies(&deps);
	return mergedirs;
}

static int
make_path(const char *namestart, const char *subdir, char *out, bool *found)
{
	struct stat statbuf;
	const char *nameend;
	int res;

	/* non-empty strings returned by prepare_merge_string() always terminate with ":" */
	nameend = strchr(namestart, ':');
	sprintf(out, "%.*s/%s", (int)(nameend-namestart), namestart, subdir);
	if (stat(out, &statbuf) != 0) {
		*out = '\0';
		return 0;
	}
	res = strlen(out);
	out[res] = ':';
	*found = true;
	return res+1;
}

static int
mount_overlay_dirs(const char *mergedirs, const char *mountpoint)
{
	const char *sources[] = {"bin", "include", "lib",  "libexec", "share", NULL};
	const char *aliases[] = {"sbin", NULL,     "lib64", NULL,      NULL,   NULL};
	const char *targets[] = {"bin", "include", "lib",  "libexec", "share", NULL};
	const char *dirptr;

	char *lower, mp[strlen(mountpoint)+strlen("libexec")+2];
	int i, j, res = 0, lower_idx = 0, dircount = 0;
	size_t lower_size = 0;

	for (i=0; i<strlen(mergedirs); ++i)
		if (mergedirs[i] == ':')
			dircount++;

	lower_size = strlen("lowerdir=");
	lower_size += (strlen(mergedirs) + dircount * (strlen("libexec")+1) + 1) * 2;
	lower_size += strlen(mountpoint) + strlen("libexec") + 2;
	lower = (char *) malloc(lower_size * sizeof(char));
	if (! lower) {
		perror("calloc");
		return -ENOMEM;
	}
	/* Mount directories from sources[] as overlays on /System/Index/targets[] */
	for (i=0; sources[i]; ++i) {
		bool have_entries = false;
		sprintf(lower, "lowerdir=");
		for (lower_idx=strlen(lower), j=0; j<2; ++j) {
			const char *source = j == 0 ? sources[i] : aliases[i];
			for (dirptr=mergedirs; source && dirptr; dirptr=strchr(dirptr, ':')) {
				if (dirptr != mergedirs) { dirptr++; }
				if (strlen(dirptr)) { lower_idx += make_path(dirptr, source, &lower[lower_idx], &have_entries); }
			}
		}
		if (have_entries) {
			sprintf(mp, "%s/%s", mountpoint, targets[i]);
			sprintf(&lower[lower_idx], "%s", mp);
			debug_printf("mount -t overlay none -o %s %s\n", lower, mp);
			res = mount("overlay", mp, "overlay", MS_MGC_VAL | MS_RDONLY, lower);
			if (res != 0)
				goto out_free;
		}
	}
out_free:
	if (res != 0) {
		fprintf(stderr, "Failed to mount overlayfs\n");
		debug_printf("%s\n", lower);
	}
	free(lower);
	return res;
}

static char *
parse_architecture_file(const char *archfile)
{
	struct stat statbuf;
	char line[64];
	size_t n;

	int res = stat(archfile, &statbuf);
	if (res < 0 && errno == EACCES) {
		perror(archfile);
		return NULL;
	}

	FILE *fp = fopen(archfile, "r");
	if (fp) {
		memset(line, 0, sizeof(line));
		n = fread(line, sizeof(char), sizeof(line)-1, fp);
		if (n > 0 && line[n-1] == '\n')
			line[n-1] = '\0';
		fclose(fp);
		return strdup(line);
	}
	return NULL;
}

/**
 * mount_overlay:
 */
static int
mount_overlay()
{
	int i, res = -1;
	char *archfile = NULL, *fname = NULL, *tmpstr;
	char *programdir = NULL;
	int merge_len = 0;
	char *mergedirs_user = NULL;
	char *mergedirs_program = NULL;
	char *mergedirs = NULL;
	struct stat statbuf;

	for (i=0; args.dependencies[i]; ++i) {
		/* take user-provided dependencies file */
		fname = strdup(args.dependencies[i]);
		if (! fname) {
			fprintf(stderr, "Not enough memory\n");
			goto out_free;
		}
		res = stat(fname, &statbuf);
		if (res < 0) {
			perror(fname);
			goto out_free;
		}
		tmpstr = prepare_merge_string(NULL, fname);
		merge_len += tmpstr ? strlen(tmpstr) : 0;
		free(fname);
		fname = NULL;

		/* concatenate */
		if (mergedirs_user == NULL)
			mergedirs_user = tmpstr;
		else if (tmpstr) {
			char *newstr;
			res = asprintf(&newstr, "%s%s", mergedirs_user, tmpstr);
			if (res < 0) {
				perror("asprintf");
				free(tmpstr);
				goto out_free;
			}
			free(mergedirs_user);
			mergedirs_user = newstr;
			res = 0;
		}
	}

	programdir = get_program_dir(args.executable);
	if (programdir) {
		/* check if the software's Resources/Dependencies file exists */
		if (asprintf(&fname, "%s/Resources/Dependencies", programdir) <= 0) {
			fprintf(stderr, "Not enough memory\n");
			goto out_free;
		}
		res = stat(fname, &statbuf);
		if (res < 0 && errno == EACCES) {
			perror(fname);
			goto out_free;
		}
		if (args.architecture == NULL) {
			/* Try to determine architecture based on the Resources/Architecture metadata file */
			if (asprintf(&archfile, "%s/Resources/Architecture", programdir) <= 0) {
				fprintf(stderr, "Not enough memory\n");
				goto out_free;
			}
			/* TODO: args.architecture is never freed */
			args.architecture = parse_architecture_file(archfile);
		}
		mergedirs_program = prepare_merge_string(programdir, fname);
		merge_len += mergedirs_program ? strlen(mergedirs_program) : 0;
		free(fname);
		fname = NULL;
	}

	res = asprintf(&mergedirs, "%s%s",
			mergedirs_user ? mergedirs_user : "",
			mergedirs_program ? mergedirs_program : "");
	if (res < 0) {
		perror("asprintf");
		goto out_free;
	}
	res = mount_overlay_dirs(mergedirs, GOBO_INDEX_DIR);
out_free:
	if (programdir) { free(programdir); }
	if (mergedirs_program) { free(mergedirs_program); }
	if (mergedirs_user) { free(mergedirs_user); }
	if (mergedirs) { free(mergedirs); }
	if (archfile) { free(archfile); }
	if (fname) { free(fname); }
	return res;
}

/**
 * update_env_var_list:
 */
static int
update_env_var_list(const char *var, const char *item)
{
	const char *env;
	char *value;
	int ret;

	env = getenv(var);
	if (env == NULL || *env == 0) {
		setenv(var, item, 1);
	} else {
		ret = asprintf(&value, "%s:%s", item, env);
		if (ret <= 0) {
			return ERR_OUTMEMORY;
		} else {
			setenv(var, value, 1);
			free(value);
		}
	}

	return 0;
}

void
show_usage_and_exit(char *exec, int err)
{
	struct utsname uts_data;
	uname(&uts_data);

	printf(
	"Executes a command with a read-only view of /System/Index overlaid with\n"
	"dependencies extracted from the program's Resources/Dependencies and/or\n"
	"from the given dependencies file(s).\n"
	"\n"
	"Syntax: %s [options] <command> [arguments]\n"
	"\n"
	"Available options are:\n"
	"  -a, --arch=ARCH           Look for dependencies whose architecture is ARCH (default: taken from\n"
	"                            Resources/Architecture, otherwise assumed to be %s)\n"
	"  -d, --dependencies=FILE   Path to GoboLinux Dependencies file to use\n"
	"  -h, --help                This help\n"
	"  -q, --quiet               Don't warn on bogus dependencies file(s)\n"
	"  -v, --verbose             Run in verbose mode\n"
	"  -c, --check               Check if Runner can be used in this system\n"
	"  -f, --fallback            Run the command without the sandbox in case this is not available\n"
	"\n", exec, uts_data.machine);
	exit(err);
}

/**
 * parse_arguments:
 */
int
parse_arguments(int argc, char *argv[])
{
	struct option long_options[] = {
		{"arch",          required_argument, 0,  'a'},
		{"dependencies",  required_argument, 0,  'd'},
		{"help",          no_argument,       0,  'h'},
		{"quiet",         no_argument,       0,  'q'},
		{"check",         no_argument,       0,  'c'},
		{"fallback",      no_argument,       0,  'f'},
		{"verbose",       no_argument,       0,  'v'},
		{0,               0,                 0,   0 }
	};
	const char *short_options = "+d:a:hcqvf";
	bool valid = true;
	int next = optind;
	int num_deps = 0;
	int dep_nr = 0;

	/* Don't stop on errors */
	opterr = 0;

	/* Count how many dependency files were given */
	while (valid) {
		int c = getopt_long(argc, argv, short_options, long_options, NULL);
		if (c == -1)
			break;
		else if (c == 'd')
			num_deps++;
		else if (!(c == 'a' || c == 'h' || c == 'q' || c == 'c' || c == 'f' || c == 'v'))
			valid = false;
	}

	/* Default values */
	args.check = false;
	args.verbose = false;
	args.quiet = false;
	args.fallback = false;
	args.executable = NULL;
	args.arguments = NULL;
	args.architecture = NULL;

	args.dependencies = (const char **) calloc(num_deps+1, sizeof(char *));
	if (! args.dependencies)
		return ERR_OUTMEMORY;

	optind = 1;
	while (valid) {
		int c = getopt_long(argc, argv, short_options, long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
			case 'a':
				args.architecture = optarg;
				break;
			case 'd':
				args.dependencies[dep_nr++] = optarg;
				break;
			case 'h':
				show_usage_and_exit(argv[0], 0);
				break;
			case 'q':
				args.quiet = true;
				break;
			case 'v':
				args.verbose = true;
				break;
			case 'f':
				args.fallback = true;
				break;
			case 'c':
				args.check = true;
				break;
			case '?':
			default:
				valid = false;
				break;
		}
		if (valid)
			next = optind;
	}

	optind = next;
	if (optind < argc) {
		int i, num = argc - optind + 1;
		args.arguments = calloc(num, sizeof(char *));
		if (args.arguments == NULL)
			return ERR_OUTMEMORY;
		for (i = optind; i < argc; i++)
			args.arguments[i-optind] = argv[i];
		args.executable = argv[optind];
	}

	return 0;
}

/**
 * Checks if the sandbox execution model can be constructed
 */
bool check_availability()
{
	uid_t uid = getuid(), euid = geteuid();
	struct utsname uts_data;
	struct statfs statbuf;
	bool is_available=true;	

	// Check uid
	if ((uid >0) && (uid == euid)) {
		debug_printf("This program needs its suid bit to be set.\n");
		is_available = false;
	}

	// Check kernel version
	uname(&uts_data);
	if (compare_kernel_versions("4.0", uts_data.release) > 0) {
		debug_printf("Running on Linux %s. At least Linux 4.0 is needed.\n",
			uts_data.release);;
		is_available = false;
	}

	// Check if maximum filesystem stacking count will be reached
	if (statfs("/", &statbuf) == 0 && statbuf.f_type == OVERLAYFS_MAGIC) {
		debug_printf("Rootfs is an overlayfs, max filesystem stacking count will be reached\n");
		is_available = false;
	}

	return is_available;
}

/**
 * main:
 */
int
main(int argc, char *argv[])
{
	int ret = 1;
	int available = 1;

	CHECK(parse_arguments(argc, argv), false);

	if (args.quiet && args.verbose) {
		error_printf(main, "--quiet and --verbose are mutually exclusive");
		exit(ERR_BAD_ARGS);
	}

	// Check if sandbox is available it this system
	available = check_availability(&args);

	// Check mode?
	if (args.check == 1) {
		exit(!available);
	}

	// Do we have an executable?
	if (args.executable == NULL) {
		error_printf(main, "no executable was specified");
		exit(ERR_NOEXECUTABLE);
	}

	// Can we run a sandbox?
	if (!available) {
		if (!args.fallback) {
			error_printf(main, "Sandbox is not available, use -f for fallback mode");
			exit(ERR_NOSANDBOX);
		}
	} else {
		ret = create_mount_namespace();
		if (ret > 0) 
				exit(ERR_MNT_NAMESPACE);

		ret = mount_overlay(&args);
		if (ret != 0)
				exit(ERR_MNT_OVERLAY);
	}

	/* Now we have everything we need CAP_SYS_ADMIN for, so drop setuid */
	CHECK(setuid(getuid()), true);

	setenv("GOBOLINUX_RUNNER", "1", 1);

	/* add generic library path */
	CHECK(update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib"), false);
	CHECK(update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib64"), false);

	/* add generic binary directory to PATH */
	CHECK(update_env_var_list("PATH", GOBO_INDEX_DIR "/bin"), false);

	ret = execvp(args.executable, args.arguments);
	perror(args.executable);
	return ret;
}
