/*
 * A generic Resources/Dependencies parser.
 * 
 * Copyright (C) 2008 Lucas C. Villa Real <lucasvr@gobolinux.org>
 *
 * Released under the GNU GPL version 2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "LinuxList.h"

static char * const currentString = "Current";
static char * goboPrograms;

typedef enum {
	GREATER_THAN,			// >
	GREATER_THAN_OR_EQUAL,	// >=
	EQUAL,					// =
	NOT_EQUAL,				// !=
	LESS_THAN,				// <
	LESS_THAN_OR_EQUAL,		// <=
} operator_t;

typedef enum {
	LOCAL_PROGRAMS,
	LOCAL_DIRECTORY,
	PACKAGE_STORE,
	RECIPE_STORE,
} repository_t;

struct version {
	char *version;			// version as listed in Resources/Dependencies
	operator_t op;			// one of the operators listed above
};

struct parse_data {
	char *workbuf;			// buffer from the latest line read in Resources/Dependencies
	char *saveptr;			// strtok_r pointer for safe reentrancy
	bool hascomma;			// cache if a comma was found glued together with the first version
	char *depname;			// dependency name, as listed in Resources/Dependencies
	struct version v1;		// first optional restriction operator
	struct version v2;		// second optional restriction operator
	char fversion[NAME_MAX];// final version after restrictions were applied.
	char url[NAME_MAX];		// url to dependency, when using the FindPackage backend
};

struct list_data {
	struct list_head list;	// link to the list on which we're inserted
	char path[PATH_MAX];	// full url or path to dependency
};

struct search_options {
	repository_t repository;
	int help;
	int quiet;
	char *dependency;
	char *depsfile;
	char *searchdir;
};

int VersionCmp(char *_candidate, char *_specified)
{
	char candidatestring[strlen(_candidate)+1];
	char specifiedstring[strlen(_specified)+1];
	strcpy(candidatestring, _candidate);
	strcpy(specifiedstring, _specified);
	char *candidate = candidatestring;
	char *specified = specifiedstring;
	int c, s, ret=0;

	// for text-based versions just propagate retval from strcmp().
	if (isalpha(candidate[0]) && isalpha(specified[0]))
		return strcmp(candidate, specified);

	while (*candidate && *specified) {
		c = 0;
		s = 0;
		// consume strings until a '.' is found
		while (c<strlen(candidate) && candidate[c] != '.')
			c++;
		while (s<strlen(specified) && specified[s] != '.')
			s++;

		if (c == 0 && s != 0)
			return 1;
		else if (c !=0 && s == 0)
			return -1;
		else if (c == 0 && s == 0)
			return 0;

		candidate[c] = 0;
		specified[s] = 0;
		if (strstr(candidate, "-r") || strstr(specified, "-r")) {
			// compare 2 numbers where at least one of them has a revision string
			char *cptr = strstr(candidate, "-");
			char *sptr = strstr(specified, "-");
			if (cptr)
				*cptr = 0;
			if (sptr)
				*sptr = 0;
			int a = atoi(candidate);
			int b = atoi(specified);
			ret = a == b ? 0 : (a > b) ? 1 : -1;
			if (ret == 0) {
				int revision_a = cptr ? atoi(cptr+2) : 0;
				int revision_b = sptr ? atoi(sptr+2) : 0;
				ret = revision_a == revision_b ? 0 : (revision_a > revision_b) ? 1 : -1;
			}
		} else {
			// compare 2 numbers
			int a = atoi(candidate);
			int b = atoi(specified);
			ret = a == b ? 0 : (a > b) ? 1 : -1;
		}
		if (ret > 0 || ret < 0)
			break;
		candidate = &candidate[++c];
		specified = &specified[++s];
	}
	return ret;
}

bool MatchRule(char *candidate, struct version *v)
{
	if (*candidate == '.' || !strcmp(candidate, "Variable") || !strcmp(candidate, "Settings") || !strcmp(candidate, "Current"))
		return false;
	if (!v->version)
		return true;
	switch (v->op) {
		case GREATER_THAN:
			return VersionCmp(candidate, v->version) > 0 ? true : false;
		case GREATER_THAN_OR_EQUAL:
			return VersionCmp(candidate, v->version) >= 0 ? true : false;
		case EQUAL: 
			return VersionCmp(candidate, v->version) == 0 ? true : false;
		case NOT_EQUAL: 
			return VersionCmp(candidate, v->version) != 0 ? true : false;
		case LESS_THAN: 
			return VersionCmp(candidate, v->version) < 0 ? true : false;
		case LESS_THAN_OR_EQUAL:
			return VersionCmp(candidate, v->version) <= 0 ? true : false;
		default:
			return false;
	}
}

bool RuleBestThanLatest(char *candidate, char *latest)
{
	if (latest[0] == '\0')
		return true;
	// If both versions are valid, the more recent one is taken
	return strcmp(candidate, latest) >= 0 ? true : false;
}

bool GetCurrentVersion(struct parse_data *data)
{
	ssize_t ret;
	char buf[PATH_MAX], path[PATH_MAX];

	snprintf(path, sizeof(path)-1, "%s/%s/Current", goboPrograms, data->depname);
	ret = readlink(path, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "WARNING: %s: %s, ignoring dependency.\n", path, strerror(errno));
		return false;
	}
	buf[ret] = '\0';
	data->fversion[sizeof(data->fversion)-1] = '\0';
	strncpy(data->fversion, buf, sizeof(data->fversion)-1);
	return true;
}

char **GetVersionsFromReadDir(struct parse_data *data)
{
	DIR *dp;
	int num = 0;
	struct dirent *entry;
	char path[PATH_MAX];
	char **versions;

	snprintf(path, sizeof(path)-1, "%s/%s", goboPrograms, data->depname);
	dp = opendir(path);
	if (! dp) {
		fprintf(stderr, "WARNING: %s: %s, ignoring dependency.\n", path, strerror(errno));
		return NULL;
	}
	
	while ((entry = readdir(dp))) {
		if (entry->d_name[0] != '.')
			num++;
	}
	
	versions = (char **) calloc(num+1, sizeof(char*));
	if (! versions) {
		perror("malloc");
		return NULL;
	}

	num = 0;
	rewinddir(dp);
	while ((entry = readdir(dp))) {
		if (entry->d_name[0] != '.')
			versions[num++] = strdup(entry->d_name);
	}
	closedir(dp);

	return versions;
}

char **GetVersionsFromStore(struct parse_data *data, struct search_options *options, char *cmdline)
{
	char buf[LINE_MAX], url[LINE_MAX];
	char **versions = NULL;
	int num = 0;
	FILE *fp;
	
	fp = popen(cmdline, "r");
	if (! fp) {
		fprintf(stderr, "WARNING: %s: %s\n", cmdline, strerror(errno));
		return NULL;
	}

	while (! feof(fp)) {
		char *name, *version, *end, *ptr;

		if (! fgets(buf, sizeof(buf), fp))
			break;
		end = &buf[strlen(buf)-1];
		if (*end == '\n')
			*end = '\0';
		if (buf[0] == '/' && options->repository != LOCAL_DIRECTORY) {
			// FindPackage returned a local directory
			continue;
		}
		strncpy(url, buf, sizeof(url));
		name = basename(buf);
		if (! name)
			continue;

		for (version=name; version < (end-1); version++) {
			if (*version == '-' && *(version+1) == '-') {
				version = version+2;
				break;
			}
		}
		for (ptr=version; ptr < (end-1); ptr++) {
			if (*ptr == '-' && *(ptr+1) == '-') {
				*ptr = '\0';
				break;
			}
		}
		if (num > 0 && !strcmp(versions[num-1], version))
			continue;

		versions = (char **) realloc(versions, (num+2) * sizeof(char*));
		if (! versions) {
			perror("realloc");
			return NULL;
		}
		/* use an empty character as separator from the version and the url */
		versions[num] = (char *) calloc(strlen(version)+1+strlen(url)+1, sizeof(char));
		memcpy(versions[num], version, strlen(version));
		memcpy(versions[num]+strlen(version)+1, url, strlen(url));
		versions[++num] = NULL;
	}

	pclose(fp);
	return versions;
}

bool GetBestVersion(struct parse_data *data, struct search_options *options)
{
	int i, latestindex = -1;
	char *entry, **versions = NULL;
	char latest[NAME_MAX], cmdline[PATH_MAX];
	
	if (options->repository == LOCAL_PROGRAMS) {
		versions = GetVersionsFromReadDir(data);
	} else if (options->repository == LOCAL_DIRECTORY) {
		snprintf(cmdline, sizeof(cmdline), "bash -c \"ls %s/%s--*--*.tar.bz2 2> /dev/null\"", options->searchdir, data->depname);
		versions = GetVersionsFromStore(data, options, cmdline);
	} else if (options->repository == PACKAGE_STORE) {
		snprintf(cmdline, sizeof(cmdline), "FindPackage --types=official_package --full-list %s", data->depname);
		versions = GetVersionsFromStore(data, options, cmdline);
	} else if (options->repository == RECIPE_STORE) {
		snprintf(cmdline, sizeof(cmdline), "FindPackage --types=recipe --full-list %s", data->depname);
		versions = GetVersionsFromStore(data, options, cmdline);
	}

	if (! versions) {
		if (! options->quiet)
			fprintf(stderr, "WARNING: no packages were found for dependency %s\n", data->depname);
		return false;
	}
	
	memset(latest, 0, sizeof(latest));
	for (i=0; versions[i]; i++) {
		entry = versions[i];
		if (MatchRule(entry, &data->v1) && RuleBestThanLatest(entry, latest)) {
			latestindex = i;
			strcpy(latest, entry);
			continue;
		}
	}
	if (! data->v2.version) {
		data->fversion[sizeof(data->fversion)-1] = '\0';
		strncpy(data->fversion, latest, sizeof(data->fversion)-1);
		goto out;
	}

	if (! MatchRule(latest, &data->v2))
		memset(latest, 0, sizeof(latest));

	for (i=0; versions[i]; i++) {
		entry = versions[i];
		if (MatchRule(entry, &data->v2) && RuleBestThanLatest(entry, latest)) {
			latestindex = i;
			strcpy(latest, entry);
			continue;
		}
	}
	if (! MatchRule(latest, &data->v1))
		memset(latest, 0, sizeof(latest));

	data->fversion[sizeof(data->fversion)-1] = '\0';
	strncpy(data->fversion, latest, sizeof(data->fversion)-1);
out:
	if (! latest[0]) {
		if (! options->quiet)
			fprintf(stderr, "WARNING: No packages matching requirements were found, skipping dependency %s\n", data->depname);
	} else if (options->repository != LOCAL_PROGRAMS) {
		char *ptr = versions[latestindex] + strlen(versions[latestindex]) + 1;
		strncpy(data->url, ptr, sizeof(data->url));
	}
	for (i=0; versions[i]; i++)
		free(versions[i]);
	free(versions);
	return latest[0] ? true : false;
}

void ListAppend(struct list_head *head, struct parse_data *data, struct search_options *options)
{
	struct list_data *ldata = (struct list_data *) malloc(sizeof(struct list_data));
	if (options->repository == LOCAL_PROGRAMS) {
		snprintf(ldata->path, sizeof(ldata->path), "%s/%s/%s", goboPrograms, data->depname, data->fversion);
	} else {
		snprintf(ldata->path, sizeof(ldata->path), "%s", data->url);
	}
	list_add_tail(&ldata->list, head);
}

bool AlreadyInList(struct list_head *head, struct parse_data *data, char *depfile)
{
	struct list_data *ldata;
	if (list_empty(head))
		return false;
	list_for_each_entry(ldata, head, list) {
		// XXX: fix 'Foo' / 'Foobar' false positives
		if (strstr(ldata->path, data->depname)) {
			fprintf(stderr, "WARNING: '%s' is included twice in %s\n", data->depname, depfile);
			return true;
		}
	}
	return false;
}

char *ReadLine(char *buf, int size, FILE *fp)
{
	char *ptr, *ret = fgets(buf, size, fp);
	if (ret) {
		if (buf[strlen(buf)-1] == '\n')
			buf[strlen(buf)-1] = '\0';
		ptr = strstr(buf, "#");
		if (ptr)
			*ptr = '\0';
	}
	return ret;
}

bool EmptyLine(char *buf)
{
	char *start;
	if (!buf || strlen(buf) == 0)
		return true;
	for (start=buf; *start; start++) {
		if (*start == ' ' || *start == '\t')
			continue;
		else if (*start == '\n')
			return true;
		else
			return false;
	}
	return false;
}

const char *GetOperatorString(operator_t op)
{
	switch (op) {
		case GREATER_THAN: return ">";
		case GREATER_THAN_OR_EQUAL: return ">=";
		case EQUAL: return "=";
		case NOT_EQUAL: return "!=";
		case LESS_THAN: return "<";
		case LESS_THAN_OR_EQUAL: return "<=";
		default: return "?";
	}
}

void PrintRestrictions(struct parse_data *data)
{
	if (data->v1.version)
		printf("%s %s", GetOperatorString(data->v1.op), data->v1.version);
	if (data->v2.version)
		printf(", %s %s", GetOperatorString(data->v2.op), data->v2.version);
}

bool ParseName(struct parse_data *data, struct search_options *options)
{
	data->depname = strtok_r(data->workbuf, " \t", &data->saveptr);
	if (options->dependency && strcmp(data->depname, options->dependency))
		return false;
	return data->depname ? true : false;
}

bool ParseVersion(struct parse_data *data, struct version *v)
{
	char *ptr = strtok_r(NULL, " \t", &data->saveptr);
	if (! ptr)
		return false;
	if (ptr[strlen(ptr)-1] == ',') {
		ptr[strlen(ptr)-1] = '\0';
		data->hascomma = true;
	}
	v->version = ptr;
	return true;
}

bool ParseOperand(struct parse_data *data, struct version *v, struct search_options *options)
{
	char *ptr = strtok_r(NULL, " \t", &data->saveptr);
	if (! ptr && options->repository == LOCAL_PROGRAMS) {
		// Only a program name without a version was supplied. Return version based on 'Current'.
		return GetCurrentVersion(data);
	} else if (! ptr) {
		// Only a program name without a version was supplied. Take version from the most recent package available.
		v->op = GREATER_THAN_OR_EQUAL;
		v->version = NULL;
		return true;
	}
	if (! strcmp(ptr, ">")) {
		v->op = GREATER_THAN;
	} else if (! strcmp(ptr, ">=")) {
		v->op = GREATER_THAN_OR_EQUAL;
	} else if (! strcmp(ptr, "==")) {
		v->op = EQUAL;
	} else if (! strcmp(ptr, "=")) {
		v->op = EQUAL;
	} else if (! strcmp(ptr, "!=")) {
		v->op = NOT_EQUAL;
	} else if (! strcmp(ptr, "<")) {
		v->op = LESS_THAN;
	} else if (! strcmp(ptr, "<=")) {
		v->op = LESS_THAN_OR_EQUAL;
	} else {
		// ptr holds the version alone. Consider operator as GREATER_THAN_OR_EQUAL (XXX)
		v->op = GREATER_THAN_OR_EQUAL;
		v->version = ptr;
		return true;
	}
	return ParseVersion(data, v);
}

bool ParseComma(struct parse_data *data)
{
	char *ptr;
	if (data->hascomma)
		return true;
	ptr = strtok_r(NULL, " \t", &data->saveptr);
	if (! ptr)
		return false;
	return strcmp(ptr, ",") == 0 ? true : false;
}

struct list_head *ParseDependencies(struct search_options *options)
{
	int line = 0;
	FILE *fp = fopen(options->depsfile, "r");
	if (! fp) {
		fprintf(stderr, "WARNING: %s: %s\n", options->depsfile, strerror(errno));
		return NULL;
	}
	struct list_head *head = (struct list_head *) malloc(sizeof(struct list_head));
	if (! head) {
		perror("malloc");
		return NULL;
	}
	INIT_LIST_HEAD(head);

	while (!feof(fp)) {
		char buf[LINE_MAX];

		line++;
		if (! ReadLine(buf, sizeof(buf), fp))
			break;
		else if (EmptyLine(buf))
			continue;

		struct parse_data *data = (struct parse_data*) calloc(1, sizeof(struct parse_data));
		if (! data) {
			perror("malloc");
			free(head);
			return NULL;
		}
		data->workbuf = buf;

		if (! ParseName(data, options) || AlreadyInList(head, data, options->depsfile)) {
			free(data);
			continue;
		}

		if (! ParseOperand(data, &data->v1, options)) {
			fprintf(stderr, "WARNING: %s:%d: syntax error, ignoring dependency %s.\n", options->depsfile, line, data->depname);
			free(data);
			continue;
		}

		if (! ParseComma(data)) {
			if (GetBestVersion(data, options))
				ListAppend(head, data, options);
			else
				free(data);
			continue;
		}

		if (! ParseOperand(data, &data->v2, options)) {
			fprintf(stderr, "WARNING: %s:%d: syntax error, ignoring dependency %s.\n", options->depsfile, line, data->depname);
			free(data);
			continue;
		}

		if (GetBestVersion(data, options))
			ListAppend(head, data, options);
		else
			free(data);
	}

	fclose(fp);
	return head;
}

void usage(char *appname, int retval)
{
	fprintf(stderr, "Usage: %s [options] <Dependencies file>\n"
			"Available options are:\n"
			"  -d, --dependency=<dep>     Only process dependency 'dep' from the input file\n"
			"  -r, --repository=<repo>    Specify which repository to use: [local-programs]\n"
			"        local-programs       look for packages under %s\n"
			"        local-dir:<path>     look for packages/recipes under <path>\n"
			"        package-store        look for packages in the package store\n"
			"        recipe-store)        look for recipes in the recipe store\n"
			"  -q, --quiet                Do not warn when a dependency is not found\n"
			"  -h, --help                 This help\n", appname, goboPrograms);
	exit(retval);
}

int main(int argc, char **argv)
{
	int c, index;
	struct list_head *deps;
	struct search_options options;
	char shortopts[] = "hqd:r:";
	struct option longopts[] = {
		{"dependency",   1, NULL, 'd'},
		{"repository",   1, NULL, 'r'},
		{"quiet",        0, NULL, 'q'},
		{"help",         0, NULL, 'h'},
		{0, 0, 0, 0}
	};

	goboPrograms = getenv("goboPrograms");
	if (! goboPrograms) {
		fprintf(stderr, "Please ensure to 'source GoboPath' before running this program.\n");
		return 1;
	}

	memset(&options, 0, sizeof(options));
	options.repository = LOCAL_PROGRAMS;

	while ((c=getopt_long(argc, argv, shortopts, longopts, &index)) != -1) {
		switch (c) {
			case 0:
			case '?':
				break;
			case 'd':
				options.dependency = optarg;
				break;
			case 'r':
				if (! strcasecmp(optarg, "package-store"))
					options.repository = PACKAGE_STORE;
				else if (! strcasecmp(optarg, "recipe-store"))
					options.repository = RECIPE_STORE;
				else if (! strcasecmp(optarg, "local-programs"))
					options.repository = LOCAL_PROGRAMS;
				else if (strstr(optarg, "local-dir:")) {
					char *dir = strstr(optarg, ":") + 1;
					options.repository = LOCAL_DIRECTORY;
					options.searchdir = dir;
				}
				else {
					fprintf(stderr, "Invalid value '%s' for --repository.\n", optarg);
					usage(argv[0], 1);
				}
				break;
			case 'q':
				options.quiet = 1;
				break;
			case 'h':
				usage(argv[0], 0);
				break;
			default:
				fprintf(stderr, "invalid option '%c'\n", (int)c);
				usage(argv[0], 1);
		}
	}

	if (optind >= argc)
		usage(argv[0], 1);

	while (optind < argc) {
		struct list_data *entry;
		options.depsfile = argv[optind++];
		//printf("*** %s ***\n", options.depsfile);
		deps = ParseDependencies(&options);
		if (!deps || list_empty(deps))
			continue;
		list_for_each_entry(entry, deps, list) {
			printf("%s\n", entry->path);
		}
	}
	return 0;
}
