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

#include "LinuxList.h"
#include "FindDependencies.h"

#define GOBO_INDEX_DIR    "/System/Index"
#define GOBO_PROGRAMS_DIR "/Programs"

#ifdef DEBUG
#define debug_print printf
#else
#define debug_printf(msg...) do { } while(0)
#endif

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
			executable, path, GOBO_PROGRAMS_DIR);
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
			executable, path, GOBO_PROGRAMS_DIR);
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
prepare_merge_string(const char *dependencies, bool quiet)
{
	struct search_options options;
	struct list_data *entry;
	struct list_head *deps;
	char *mergedirs = NULL;
	int mergedirs_len = 0;
	struct stat statbuf;

	memset(&options, 0, sizeof(options));
	options.repository = LOCAL_PROGRAMS;
	options.depsfile = dependencies;
	options.quiet = quiet;
	options.goboPrograms = GOBO_PROGRAMS_DIR;

	deps = ParseDependencies(&options);
	if (!deps || list_empty(deps)) {
		fprintf(stderr, "Could not parse dependencies from %s\n", dependencies);
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
			strcat(mergedirs, entry->path);
			strcat(mergedirs, ":");
		}
	}
out_free:
	if (deps)
		FreeDependencies(&deps);
	return mergedirs;
}

/**
 * mount_overlay:
 * @executable
 */
static int
mount_overlay(const char *executable, const char *dependencies, bool quiet)
{
	int res = -1;
	char *fname = NULL;
	char *programdir = NULL;
	int merge_len = 0;
	char *mergedirs_user = NULL;
	char *mergedirs_program = NULL;
	char *lower = NULL;
	struct stat statbuf;

	if (dependencies) {
		/* take user-provided dependencies file */
		fname = strdup(dependencies);
		if (! fname) {
			fprintf(stderr, "Not enough memory\n");
			goto out_free;
		}
		res = stat(fname, &statbuf);
		if (res < 0) {
			perror(fname);
			goto out_free;
		}
		mergedirs_user = prepare_merge_string(fname, quiet);
		merge_len += mergedirs_user ? strlen(mergedirs_user) : 0;
		free(fname);
		fname = NULL;
	}

	programdir = get_program_dir(executable);
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
		mergedirs_program = prepare_merge_string(fname, quiet);
		merge_len += mergedirs_program ? strlen(mergedirs_program) : 0;
		free(fname);
		fname = NULL;
	}

	/* Safeguard againt the case where only one path is set for lowerdir.
	 * Overlayfs doesn't like that, so we always set the root path as a
	 * source too. */
	res = asprintf(&lower, "lowerdir=%s%s%s",
			mergedirs_user ? mergedirs_user : "",
			mergedirs_program ? mergedirs_program : "",
			GOBO_INDEX_DIR);
	if (res < 0) {
		fprintf(stderr, "Not enough memory\n");
		goto out_free;
	}

	res = mount("overlay", GOBO_INDEX_DIR, "overlay",
			MS_MGC_VAL | MS_RDONLY, lower);
	if (res != 0) {
		fprintf(stderr, "Failed to mount overlayfs\n");
		debug_printf("%s\n", lower);
	}
out_free:
	if (programdir) { free(programdir); }
	if (mergedirs_program) { free(mergedirs_program); }
	if (mergedirs_user) { free(mergedirs_user); }
	if (fname) { free(fname); }
	if (lower) { free(lower); }
	return res;
}

/**
 * update_env_var_list:
 */
static void
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
			fprintf(stderr, "Out of memory!\n");
		} else {
			setenv(var, value, 1);
			free(value);
		}
	}
}

void
show_usage_and_exit(char *exec, int err)
{
	printf(
	"Executes a command with a read-only view of /System/Index overlaid with\n"
	"dependencies extracted from the program's Resources/Dependencies and/or\n"
	"from the given dependencies file.\n"
	"\n"
	"Syntax: %s [options] <command>\n"
	"\n"
	"Available options are:\n"
	"  -d, --dependencies=FILE   Path to GoboLinux Dependencies file to use\n"
	"  -h, --help                This help\n"
	"  -q, --quiet               Don't warn on bogus dependencies file(s)\n"
	"\n", exec);
	exit(err);
}

/**
 * parse_arguments:
 */
char **
parse_arguments(int argc, char *argv[], char **executable, char **dependencies, bool *quiet)
{
	char **child_argv;
	bool valid = true;
	int next = optind;

	/* Don't stop on errors */
	opterr = 0;

	while (valid) {
		int option_index = 0;
		static struct option long_options[] = {
			{"dependencies",  required_argument, 0,  'd'},
			{"help",          no_argument,       0,  'h'},
			{"quiet",         no_argument,       0,  'q'},
			{0,               0,                 0,   0 }
		};

		int c = getopt_long(argc, argv, "+d:hq", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
			case 'd':
				*dependencies = optarg;
				break;
			case 'h':
				show_usage_and_exit(argv[0], 0);
				break;
			case 'q':
				*quiet = true;
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
		child_argv = calloc(num, sizeof(char *));
		if (child_argv == NULL) {
			fprintf(stderr, "Out of memory!\n");
			return NULL;
		}
		for (i = optind; i < argc; i++)
			child_argv[i-optind] = argv[i];
		*executable = argv[optind];
		return child_argv;
	} else {
		fprintf(stderr, "Error: no executable was specified.\n\n");
		show_usage_and_exit(argv[0], 1);
	}
	/* Should be never reached */
	return NULL;
}

/**
 * main:
 */
int
main(int argc, char *argv[])
{
	int ret = 1;
	bool quiet = false;
	struct utsname uts_data;
	char *executable = NULL;
	char **child_argv = NULL;
	char *dependencies = NULL;
	uid_t uid = getuid(), euid = geteuid();

	if ((uid > 0) && (uid == euid)) {
		fprintf (stderr, "This program needs its suid bit to be set\n");
		goto fallback;
	}

	child_argv = parse_arguments(argc,argv, &executable, &dependencies, &quiet);
	if (! child_argv)
		return 1;

	uname(&uts_data);
	if (compare_kernel_versions("4.0", uts_data.release) > 0) {
		fprintf(stderr, "Running on Linux %s. At least Linux 4.0 is needed.\n",
				uts_data.release);
		goto fallback;
	}

	ret = create_mount_namespace();
	if (ret > 0)
		goto fallback;

	ret = mount_overlay(executable, dependencies, quiet);
	if (ret != 0)
		goto fallback;

fallback:
	/* Now we have everything we need CAP_SYS_ADMIN for, so drop setuid */
	setuid(getuid());

	setenv("GOBOLINUX_RUNNER", "1", 1);

	/* add generic library path */
	update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib");
	update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib64");

	/* add generic binary directory to PATH */
	update_env_var_list("PATH", GOBO_INDEX_DIR "/bin");

	ret = execvp(executable, child_argv);
	perror(executable);
	return ret;
}
