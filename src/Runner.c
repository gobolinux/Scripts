/*
 * Runner: GoboLinux filesystem virtualization tool
 *
 * Copyright (C) 2015-2017 Lucas C. Villa Real <lucasvr@gobolinux.org>
 *
 * compare_kernel_versions() and create_mount_namespace() by:
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
#include <dirent.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/version.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/time.h>      /* getrlimit() */
#include <sys/resource.h>  /* getrlimit() */
#include <ftw.h>

#include "LinuxList.h"
#include "FindDependencies.h"

#define GOBO_INDEX_DIR    "/System/Index"
#define GOBO_PROGRAMS_DIR "/Programs"
#define OVERLAYFS_MAGIC   0x794c7630

#define debug_printf(msg...)   if (args.debug) fprintf(stderr, msg)
#define verbose_printf(msg...) if (args.verbose) fprintf(stderr, msg)
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
#define ERR_MNT_WRITEDIR      6      /* Error creating write directory */
#define ERR_BAD_ARGS          7      /* Bad arguments */

struct runner_args {
	bool quiet;                /* Run in quiet mode? */
	bool debug;                /* Print debug messages? */
	bool verbose;              /* Run in verbose mode? */
	bool check;                /* Run in check mode? */
	bool fallback;             /* Run in fallback mode if sandbox is not available */
	bool removedeps;           /* Remove conflicting dependencies from /System/Index? */
	const char *executable;    /* Executable to run */
	const char **dependencies; /* NULL-terminated */
	char **arguments;          /* Arguments to pass to executable */
	char *architecture;        /* Architecture of dependencies to consider */

	char *upperlayer;          /* Overlayfs' upper layer */
	char *writelayer;          /* Overlayfs' write layer */
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
		verbose_printf("'%s' ('%s') is not in a $goboPrograms subdirectory\n",
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
		verbose_printf("'%s' ('%s') has too many '/' components\n",
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

	verbose_printf("creating new namespace\n");
	res = unshare(CLONE_NEWNS);
	if (res != 0) {
		fprintf(stderr, "Failed to create namespace: %s\n", strerror(errno));
		return -1;
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
	return -1;
}

static bool
program_blacklisted(const char *programname)
{
	/*
	 * Don't let GoboLinux tools be overlay-mounted on /System/Index.
	 * The reason is that many of our scripts expect to find contents
	 * under $(readlink -f $argv[0])/../Functions, and we don't have
	 * a Functions directory under /System/Index.
	 */
	if (strstr(programname, "/Scripts/") ||
		strstr(programname, "/Compile/") ||
		strstr(programname, "/DevelScripts/"))
		return true;
	return false;
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
			verbose_printf("adding dependency at %s\n", entry->path);
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
	if (stat(out, &statbuf) != 0 || program_blacklisted(out)) {
		*out = '\0';
		return 0;
	}
	res = strlen(out);
	out[res] = ':';
	*found = true;
	return res+1;
}

static int
test_and_remove(char *srcdir, char *indexdir)
{
	int indexfd, ret = 0;
	struct stat statbuf;
	struct dirent *entry;
	DIR *dp = opendir(srcdir);
	if (!dp)
		return -errno;

	indexfd = open(indexdir, O_DIRECTORY|O_PATH);
	if (indexfd < 0) {
		ret = -errno;
		closedir(dp);
		return ret;
	}

	while ((entry = readdir(dp))) {
		if (fstatat(indexfd, entry->d_name, &statbuf, AT_SYMLINK_NOFOLLOW) < 0)
			continue;

		if (S_ISDIR(statbuf.st_mode) && (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")))
			continue;

		else if (S_ISDIR(statbuf.st_mode)) {
			/* recurse */
			char *new_srcdir, *new_indexdir;
			ret = asprintf(&new_srcdir, "%s/%s", srcdir, entry->d_name);
			if (ret < 0) {
				perror("asprintf");
				goto out;
			}
			ret = asprintf(&new_indexdir, "%s/%s", indexdir, entry->d_name);
			if (ret < 0) {
				perror("asprintf");
				free(new_srcdir);
				goto out;
			}
			test_and_remove(new_srcdir, new_indexdir);
			free(new_indexdir);
			free(new_srcdir);
		}

		else if (S_ISLNK(statbuf.st_mode)) {
			/* test and remove */
			char *target = calloc(PATH_MAX, sizeof(char));
			if (! target) {
				perror("calloc");
				ret = -ENOMEM;
				goto out;
			}
			if (readlinkat(indexfd, entry->d_name, target, PATH_MAX-1) < 0) {
				ret = -errno;
				perror("readlinkat");
				free(target);
				goto out;
			}
			if (strstr(target, srcdir)) {
				/* symlink points to conflicting dependency, so remove it */
				verbose_printf("%s: removing %s/%s -> %s\n", __func__, indexdir, entry->d_name, target);
				unlinkat(indexfd, entry->d_name, 0);
			}
			free(target);
		}
	}
	ret = 0;
out:
	close(indexfd);
	closedir(dp);
	return ret;
}

static void
remove_conflicting_deps(char *good_dep)
{
	char *version, srcdir[PATH_MAX], indexdir[PATH_MAX];
	struct stat statbuf;
	struct dirent *entry;
	DIR *dp;
	int i;

	version = strrchr(good_dep, '/');
	if (! version)
		return;

	*version = '\0';
	dp = opendir(good_dep);
	if (! dp) {
		perror(good_dep);
		*version = '/';
		return;
	}

	while ((entry = readdir(dp))) {
		const char *blacklist[] = { ".", "..", "Current", "Settings", "Variable", version+1, NULL };
		const char *sources[] = {"bin", "sbin", "include", "lib",  "lib64", "libexec", "share", NULL};
		const char *targets[] = {"bin", "bin",  "include", "lib",  "lib",   "libexec", "share", NULL};
		bool skip = false;

		for (i=0; blacklist[i]; ++i)
			if (! strcmp(blacklist[i], entry->d_name)) {
				skip = true;
				break;
			}
		if (skip)
			continue;

		for (i=0; sources[i]; ++i) {
			snprintf(srcdir, sizeof(srcdir)-1, "%s/%s/%s", good_dep, entry->d_name, sources[i]);
			snprintf(indexdir, sizeof(indexdir)-1, "%s/%s", GOBO_INDEX_DIR, targets[i]);
			if (stat(srcdir, &statbuf) != 0)
				continue;
			test_and_remove(srcdir, indexdir);
		}
	}

	closedir(dp);
	*version = '/';
}

static int
mount_overlay_dirs(const char *mergedirs, const char *mountpoint)
{
	const char *sources[] = {"bin", "include", "lib",  "libexec", "share", NULL};
	const char *aliases[] = {"sbin", NULL,     "lib64", NULL,      NULL,   NULL};
	const char *targets[] = {"bin", "include", "lib",  "libexec", "share", NULL};
	const char *dirptr;

	char *unionfs, mp[strlen(mountpoint)+strlen("libexec")+2];
	int i, j, res = 0, unionfs_idx = 0, dircount = 0;
	size_t unionfs_size = 0;

	for (i=0; i<strlen(mergedirs); ++i)
		if (mergedirs[i] == ':')
			dircount++;

	unionfs_size = strlen("lowerdir=");
	unionfs_size += (strlen(mergedirs) + dircount * (strlen("libexec")+1) + 1) * 2;
	unionfs_size += strlen(mountpoint) + strlen("libexec") + 2;
	unionfs_size += strlen("workdir=");
	unionfs_size += strlen(args.writelayer) + strlen("libexec") + 2;
	unionfs_size += strlen("upperdir=");
	unionfs_size += strlen(args.upperlayer) + strlen("libexec") + 2;
	unionfs = (char *) malloc(unionfs_size * sizeof(char));
	if (! unionfs) {
		perror("calloc");
		return -ENOMEM;
	}
	/* Mount directories from sources[] as overlays on /System/Index/targets[] */
	for (i=0; sources[i]; ++i) {
		bool have_entries = false;
		sprintf(unionfs, "lowerdir=");
		for (unionfs_idx=strlen(unionfs), j=0; j<2; ++j) {
			const char *source = j == 0 ? sources[i] : aliases[i];
			for (dirptr=mergedirs; source && dirptr; dirptr=strchr(dirptr, ':')) {
				if (dirptr != mergedirs) { dirptr++; }
				if (strlen(dirptr)) { unionfs_idx += make_path(dirptr, source, &unionfs[unionfs_idx], &have_entries); }
			}
		}
		if (have_entries) {
			sprintf(mp, "%s/%s", mountpoint, targets[i]);
			sprintf(&unionfs[unionfs_idx], "%s,upperdir=%s/%s,workdir=%s/%s",
				mp, args.upperlayer, sources[i], args.writelayer, sources[i]);
			debug_printf("mount -t overlay none -o %s %s\n", unionfs, mp);
			res = mount("overlay", mp, "overlay", 0, unionfs);
			if (res != 0)
				goto out_free;
		}
	}
	/* Do not keep other conflicting versions of dependencies on /System/Index */
	if (args.removedeps) {
		char *mergecopy = strdup(mergedirs);
		if (! mergecopy) {
			perror("strdup");
			goto out_free;
		}
		char *start = mergecopy, *end;
		while (start && strlen(start) > 0) {
			end = strstr(start, ":");
			if (end) { *end = '\0'; }

			remove_conflicting_deps(start);

			start = end ? end+1 : NULL;
			end = start ? strstr(start, ":") : NULL;
		}
		free(mergecopy);
	}
out_free:
	if (res != 0) {
		fprintf(stderr, "Failed to mount overlayfs\n");
		verbose_printf("%s\n", unionfs);
	}
	free(unionfs);
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
	struct stat statbuf;
	int i, res = -1, merge_len = 0;
	char *programdir = NULL, *callerprogram;
	char *archfile = NULL, *fname = NULL, *tmpstr;
	char *mergedirs_user = NULL, *mergedirs_program = NULL, *mergedirs = NULL;

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
		callerprogram = program_blacklisted(programdir) ? NULL : programdir;
		mergedirs_program = prepare_merge_string(callerprogram, fname);
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

int
create_write_layer(void)
{
	const char *sources[] = {"bin", "include", "lib",  "libexec", "share", NULL};
	const char *home;
	char path[PATH_MAX];
	int ret, i;

	home = getenv("HOME");
	if (home == NULL)
		home = "/tmp";

	/* mkdir -p ~/.local/Runner */
	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path)-1, "%s/.local", home);
	mkdir(path, 0755);
	chown(path, getuid(), getgid());
	strcat(path, "/Runner");
	mkdir(path, 0755);
	chown(path, getuid(), getgid());

	/* Write layer */
	ret = asprintf(&args.writelayer, "%s/.local/Runner/write_layer-XXXXXX", home);
	if (ret < 0) {
		ret = -ENOMEM;
		perror("asprintf");
		goto out_error;
	}
	if (mkdtemp(args.writelayer) == NULL) {
		ret = -errno;
		perror("mkdtemp");
		goto out_error;
	}
	chown(args.writelayer, getuid(), getgid());
	for (i=0; sources[i]; ++i) {
		snprintf(path, sizeof(path)-1, "%s/%s", args.writelayer, sources[i]);
		mkdir(path, 0755);
		chown(path, getuid(), getgid());
	}

	/* Upper layer. Overlayfs requires it to activate the write branch */
	ret = asprintf(&args.upperlayer, "%s/.local/Runner/upper_layer-XXXXXX", home);
	if (ret < 0) {
		ret = -ENOMEM;
		perror("asprintf");
		goto out_error;
	}
	if (mkdtemp(args.upperlayer) == NULL) {
		ret = -errno;
		perror("mkdtemp");
		goto out_error;
	}
	chown(args.upperlayer, getuid(), getgid());
	for (i=0; sources[i]; ++i) {
		snprintf(path, sizeof(path)-1, "%s/%s", args.upperlayer, sources[i]);
		mkdir(path, 0755);
		chown(path, getuid(), getgid());
	}

	return 0;

out_error:
	free(args.upperlayer);
	free(args.writelayer);
	return ret;
}

void
cleanup_directory(char *dirname)
{
	int cleanup_entry(const char *path, const struct stat *statbuf, int typeflag, struct FTW *ftwbuf)
	{
		if (typeflag == FTW_F || typeflag == FTW_SLN)
			unlink(path);
		else if (typeflag == FTW_D || typeflag == FTW_DP)
			rmdir(path);
		return 0;
	}

	struct rlimit limit;
	int ret = getrlimit(RLIMIT_NOFILE, &limit);
	if (ret < 0) {
		perror("getrusage");
		limit.rlim_cur = 1024;
	}
	ret = nftw(dirname, cleanup_entry, limit.rlim_cur, FTW_DEPTH);
	if (ret < 0)
		perror("nftw");
	rmdir(dirname);
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
	"  -v, --verbose             Run in verbose mode (type twice to enable debug messages)\n"
	"  -c, --check               Check if Runner can be used in this system\n"
	"  -f, --fallback            Run the command without the sandbox in case this is not available\n"
	"  -R, --no-removedeps       Do not remove conflicting versions of dependencies from /System/Index view\n"
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
		{"no-removedeps", no_argument,       0,  'R'},
		{"verbose",       no_argument,       0,  'v'},
		{0,               0,                 0,   0 }
	};
	const char *short_options = "+d:a:hcqvfR";
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
		else if (!(c == 'a' || c == 'h' || c == 'q' || c == 'c' || c == 'f' || c == 'v' || c == 'R'))
			valid = false;
	}

	/* Default values */
	args.check = false;
	args.debug = false;
	args.verbose = false;
	args.quiet = false;
	args.fallback = false;
	args.removedeps = true;
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
			case 'R':
				args.removedeps = false;
				break;
			case 'v':
				if (args.verbose)
					args.debug = true;
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
	char hostname[512];
	bool is_available = true;

	/* Not currently available on the LiveCD */
	if (gethostname(hostname, sizeof(hostname)-1) == 0 && !strcmp(hostname, "LiveCD"))
		return false;

	/* Check uid */
	if ((uid >0) && (uid == euid)) {
		verbose_printf("This program needs its suid bit to be set.\n");
		is_available = false;
	}

	/* Check kernel version */
	uname(&uts_data);
	if (compare_kernel_versions("4.0", uts_data.release) > 0) {
		verbose_printf("Running on Linux %s. At least Linux 4.0 is needed.\n",
			uts_data.release);;
		is_available = false;
	}

	/* Check if maximum filesystem stacking count will be reached */
	if (statfs("/", &statbuf) == 0 && statbuf.f_type == OVERLAYFS_MAGIC) {
		verbose_printf("Rootfs is an overlayfs, max filesystem stacking count will be reached\n");
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
	int status, ret = 1, available = 1;
	pid_t pid;

	CHECK(parse_arguments(argc, argv), false);

	if (args.quiet && args.verbose) {
		error_printf(main, "--quiet and --verbose are mutually exclusive");
		exit(ERR_BAD_ARGS);
	}

	/* Check if sandbox is available it this system */
	available = check_availability(&args);

	/* Check mode? */
	if (args.check == 1) {
		exit(!available);
	}

	/* Do we have an executable? */
	if (args.executable == NULL) {
		error_printf(main, "no executable was specified");
		exit(ERR_NOEXECUTABLE);
	}

	/* Can we run a sandbox? */
	if (!available) {
		if (!args.fallback) {
			error_printf(main, "Sandbox is not available, use -f for fallback mode");
			exit(ERR_NOSANDBOX);
		}
	} else {
		ret = create_mount_namespace();
		if (ret < 0)
			exit(ERR_MNT_NAMESPACE);

		ret = create_write_layer();
		if (ret < 0)
			exit(ERR_MNT_WRITEDIR);

		ret = mount_overlay(&args);
		if (ret < 0)
			exit(ERR_MNT_OVERLAY);
	}

	pid = fork();
	if (pid == 0) {
		/* Now we have everything we need CAP_SYS_ADMIN for, so drop setuid */
		CHECK(setuid(getuid()), true);

		setenv("GOBOLINUX_RUNNER", "1", 1);

		/* Add generic library path */
		CHECK(update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib"), false);
		CHECK(update_env_var_list("LD_LIBRARY_PATH", GOBO_INDEX_DIR "/lib64"), false);

		/* Add generic binary directory to PATH */
		CHECK(update_env_var_list("PATH", GOBO_INDEX_DIR "/bin"), false);

		/* Launch the program provided by the user */
		ret = execvp(args.executable, args.arguments);
		perror(args.executable);
	} else if (pid > 0) {
		/*
		 * Wait for child and clean up files and directories left on its write
		 * and upper unionfs layers. Note that this effectively causes all
		 * operations made under /System/Index to be discarded. Runner is not
		 * meant to be used for regular system management anyway.
		 */
		waitpid(pid, &status, 0);
		ret = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		cleanup_directory(args.upperlayer);
		cleanup_directory(args.writelayer);
	} else if (pid < 0) {
		ret = -errno;
		perror("fork");
	}
	return ret;
}

/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
