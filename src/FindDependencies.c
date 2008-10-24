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
#ifdef __CYGWIN__
   #include <sys/syslimits.h>
#endif

#include "LinuxList.h"

#ifdef DEBUG
#define DBG(a...) fprintf(stderr,a)
#else
#define DBG(a...)
#endif

static char * const currentString = "Current";
static char * goboPrograms;

typedef enum {
	NONE,
	GREATER_THAN,			// >
	GREATER_THAN_OR_EQUAL,		// >=
	EQUAL,				// =
	NOT_EQUAL,			// !=
	LESS_THAN,			// <
	LESS_THAN_OR_EQUAL,		// <=
} operator_t;

typedef enum {
	LOCAL_PROGRAMS,
	LOCAL_DIRECTORY,
	PACKAGE_STORE,
	RECIPE_STORE,
} repository_t;

struct version {
	struct list_head list;		// link to the list on which we're inserted
	char *version;			// version as listed in Resources/Dependencies
	operator_t op;			// one of the operators listed above
};

struct parse_data {
	char *workbuf;			// buffer from the latest line read in Resources/Dependencies
	char *saveptr;			// strtok_r pointer for safe reentrancy
	char *depname;			// dependency name, as listed in Resources/Dependencies
	struct list_head *versions;	// link to the list of versions we've extracted
	struct list_head *ranges;	// link to the list of ranges we've built
	char fversion[NAME_MAX];	// final version after restrictions were applied.
	char url[NAME_MAX];		// url to dependency, when using the FindPackage backend
};

struct range {
	struct list_head list;		// link to the list on which we're inserted
	struct version low;		// low limit
	struct version high;		// high limit
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

const char *GetRangeOperatorString(operator_t op)
{
	switch (op) {
		case GREATER_THAN: return "]";
		case GREATER_THAN_OR_EQUAL: return "[";
		case EQUAL: return "=";
		case NOT_EQUAL: return "!=";
		case LESS_THAN: return "[";
		case LESS_THAN_OR_EQUAL: return "]";
		case NONE: return "";
		default: return "?";
	}
}

void PrintVersion(struct version *version) 
{
	fprintf(stderr, "%s %s\n",GetOperatorString(version->op),version->version);
}

void PrintRange(struct range *range)
{
	fprintf(stderr, "%s%s %s%s\n", 
			GetRangeOperatorString(range->low.op),
			range->low.version, range->high.version,
			GetRangeOperatorString(range->high.op));
}

char *GetFirstAlpha(char *version)
{
	char *ptr;
	for (ptr = version; *ptr; ptr++)
		if (isalpha(*ptr))
			return ptr;
	return NULL;
}

bool HasAlpha(char *version)
{
	return GetFirstAlpha(version) ? true : false;
}

int VersionCmp(char *_candidate, char *_specified)
{
	char candidatestring[strlen(_candidate)+1];
	char specifiedstring[strlen(_specified)+1];
	strcpy(candidatestring, _candidate);
	strcpy(specifiedstring, _specified);
	char *candidate = candidatestring;
	char *specified = specifiedstring;
	char *ptr;
	int c_len, s_len;
	int c, s, ret=0;

	c_len = strlen(candidate);
	s_len = strlen(specified);

	// find and remove arguments such as [!cross]. 
	// we need to take care of them later.
	ptr = strstr(specified, "[");
	if (ptr) {
		do {
			*ptr = 0;
			ptr--;
		} while (*ptr == ' ');
	}

	// for text-based versions just propagate retval from strcmp().
	if (isalpha(candidate[0]) && isalpha(specified[0]))
		return strcmp(candidate, specified);

	while (*candidate && *specified) {
		c = 0;
		s = 0;
		// consume strings until a '.' is found
		while (c<c_len && candidate[c] != '.')
			c++;
		while (s<s_len && specified[s] != '.')
			s++;

		if (c == c_len || s == s_len) {
			// return a comparison of the major numbers
			int a = atoi(candidate);
			int b = atoi(specified);
			if (a == b && HasAlpha(candidate) && HasAlpha(specified)) {
				char *alphaa = GetFirstAlpha(candidate);
				char *alphab = GetFirstAlpha(specified);
				return strcmp(alphaa, alphab);
			}
			return a == b ? 0 : (a > b) ? 1 : -1;
		}

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
			if (ret == 0 && sptr) {
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
	if (!v->version || strlen(v->version) == 0) 
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
		case NONE:
			return true;
		default:
			return false;
	}
}

bool VersionMatchRange(char *bufversion, struct range *range) 
{
	return (MatchRule(bufversion, &range->low) && MatchRule(bufversion, &range->high));
}

bool VersionMatchRangeList(char *bufversion, struct list_head *rangelist)
{
	struct range *rangeentry;
	list_for_each_entry(rangeentry, rangelist, list) {
		if (VersionMatchRange(bufversion, rangeentry))
			return true;
	}
	return false;
}

struct range *VersionInRangeList(struct version *version, struct list_head *rangelist)
{
	struct range *rangeentry;
	list_for_each_entry(rangeentry, rangelist, list) {
		if (VersionMatchRange(version->version, rangeentry)) 
			return rangeentry;
	}
	return NULL;
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
			fprintf(stderr, "WARNING: No packages were found for dependency %s\n", data->depname);
		return false;
	}

	memset(latest, 0, sizeof(latest));
	for (i=0; versions[i]; i++) {
		entry = versions[i];
		if (VersionMatchRangeList(entry,data->ranges) && RuleBestThanLatest(entry, latest)) {
			latestindex = i;
			strcpy(latest, entry);
			strcpy(data->fversion, entry);
			continue;
		}
	}

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
		// if no version exists in fversion it's because we parsed an application without a version
		// in Dependencies/BuildDependencies. Just resolve the "Current" symlink, returning "any"
		// available version as result.
		if (!data->fversion || !strlen(data->fversion))
			GetCurrentVersion(data);
		snprintf(ldata->path, sizeof(ldata->path), "%s/%s/%s", goboPrograms, data->depname, data->fversion);
	} else {
		snprintf(ldata->path, sizeof(ldata->path), "%s", data->url);
	}
	list_add_tail(&ldata->list, head);
}

bool AlreadyInList(struct list_head *head, struct parse_data *data, char *depfile)
{
	struct list_data *ldata;
	char bufname[NAME_MAX+3];
	if (list_empty(head))
		return false;
	sprintf(bufname,"/%s--",data->depname);
	list_for_each_entry(ldata, head, list) {
		if (strstr(ldata->path, bufname)) {
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
		ptr = strstr(buf, "[");
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

void PrintRestrictions(struct parse_data *data) 
{
	struct range *rentry;

	if (list_empty(data->ranges)) {
		printf("Conflicting dependency caused invalid restrictions\n");
	} else {
		list_for_each_entry(rentry, data->ranges, list) {
			switch (rentry->low.op) {
				case EQUAL:
				case NOT_EQUAL:
					PrintVersion(&rentry->low);
					break;
				case GREATER_THAN:
				case GREATER_THAN_OR_EQUAL:
				case LESS_THAN:
				case LESS_THAN_OR_EQUAL:
					PrintRange(rentry);
					break;
				default:
					break;
			}
		}
	}
}

bool ParseName(struct parse_data *data, struct search_options *options)
{
	data->depname = strtok_r(data->workbuf, " \t", &data->saveptr);
	if (options->dependency && strcmp(data->depname, options->dependency))
		return false;
	return data->depname ? true : false;
}

bool MakeVersion(char *buf, struct version *v)
{
	// Remove white space
	while(!strncmp(buf," ",1))
		buf++;

	if (! strncmp(buf, ">=",2)) {
		v->op = GREATER_THAN_OR_EQUAL;
		buf += 2;
	} else if (! strncmp(buf, ">",1)) {
		v->op = GREATER_THAN;
		buf++;
	} else if (! strncmp(buf, "==",2)) {
		v->op = EQUAL;
		buf += 2;
	} else if (! strncmp(buf, "=",1)) {
		v->op = EQUAL;
		buf++;
	} else if (! strncmp(buf, "!=",2)) {
		v->op = NOT_EQUAL;
		buf += 2;
	} else if (! strncmp(buf, "<=",2)) {
		v->op = LESS_THAN_OR_EQUAL;
		buf += 2;
	} else if (! strncmp(buf, "<",1)) {
		v->op = LESS_THAN;
		buf++;
	} else {
		// ptr holds the version alone. Consider operator as GREATER_THAN_OR_EQUAL (XXX)
		v->op = GREATER_THAN_OR_EQUAL;
		v->version = buf;
		return true;
	}
	// Remove white space
	while(!strncmp(buf," ",1))
		buf++;

	v->version = buf;
	return true;
}

bool ParseVersions(struct parse_data *data, struct search_options *options)
{
	struct version *version = NULL;
	char *ptr;

	data->versions = (struct list_head *) malloc(sizeof(struct list_head));
	if (! data->versions) {
		perror("malloc");
		return false;
	}
	INIT_LIST_HEAD(data->versions);

	for (ptr = strtok_r(NULL, ",", &data->saveptr); ptr != NULL; ptr = strtok_r(NULL, ",", &data->saveptr))	{
		version = (struct version*) calloc(1, sizeof(struct version));
		if (! version) {
			perror("malloc");
			free(data->versions);
			return false;
		}
		if (! MakeVersion(ptr,version)) {
			perror("Syntax error");
			free(version);
			continue;
		}
		list_add_tail(&version->list, data->versions);
	}
	// If there was no version given at all, i.e. strtok_r returns nothng
	if (version == NULL) {
		version = (struct version*) calloc(1, sizeof(struct version));
		if (! version) {
			perror("malloc");
			free(data->versions);
			return false;
		}
		if (! MakeVersion(">= 0",version)) {
			perror("Syntax error");
			free(version);
		}
		list_add_tail(&version->list, data->versions);
	}
	
	return true;
}

struct range *CreateRangeFromVersion(struct version *version)
{
	struct range *range;

	range = (struct range*) calloc(1, sizeof(struct range));
	if (! range) {
		perror("malloc");
		return NULL;
	}
	switch (version->op) {

		case GREATER_THAN:
		case GREATER_THAN_OR_EQUAL:
			range->low.op = version->op;
			range->low.version = version->version;
			range->high.op = LESS_THAN;
			range->high.version = "";
			break;
		case LESS_THAN: 
		case LESS_THAN_OR_EQUAL:
			range->low.op = GREATER_THAN;
			range->low.version = "";
			range->high.op = version->op;
			range->high.version = version->version;
			break;
		case EQUAL: 
		case NOT_EQUAL:
			range->low.op = EQUAL;
			range->low.version = version->version;
			range->high.op = NONE;
			range->high.version = "";
			break;
		default:
			range->low.op = NONE;
			range->low.version = "";
			range->high.op = NONE;
			range->high.version = "";
			break;
	}
	return range;
}

bool LimitRange(struct parse_data *data, struct range *range, struct version *version)
{
	struct range *highrange = NULL;

	switch (version->op) {
	 case GREATER_THAN:
	 case GREATER_THAN_OR_EQUAL:
		range->low.op = version->op;
		range->low.version = version->version;
		break;
	 case LESS_THAN: 
	 case LESS_THAN_OR_EQUAL:
		range->high.op = version->op;
		range->high.version = version->version;
		break;
	 case EQUAL: 
		range->low.op = EQUAL;
		range->low.version = version->version;
		range->high.op = EQUAL;
		range->high.version = version->version;
		break;
	 case NOT_EQUAL:
		highrange = (struct range*) calloc(1, sizeof(struct range));
		if (! highrange) {
			perror("malloc");
			return false;
		}
		highrange->low.op = GREATER_THAN;
		highrange->low.version = version->version;
		highrange->high.op = range->high.op;
		highrange->high.version = range->high.version;
		range->high.op = LESS_THAN;
		range->high.version = version->version;
		list_add_tail(&highrange->list, data->ranges);
		break;
	default:
		range->low.op = NONE;
		range->low.version = "";
		range->high.op = NONE;
		range->high.version = "";
		break;
	}
	return true;
}

bool ParseRanges(struct parse_data *data, struct search_options *options)
{
	struct range *matchrange, *rangeentry, *rangestore;
	struct version *verentry;

	data->ranges = (struct list_head *) malloc(sizeof(struct list_head));
	if (! data->ranges) {
		perror("malloc");
		return false;
	}
	INIT_LIST_HEAD(data->ranges);
	list_for_each_entry(verentry, data->versions, list) {
		if (list_empty(data->ranges)) {
			rangestore = CreateRangeFromVersion(verentry);
			list_add_tail(&rangestore->list,data->ranges);
		} else {
			matchrange = VersionInRangeList(verentry,data->ranges);
			if (matchrange)
				LimitRange(data,matchrange,verentry);
			else if (verentry->op != NOT_EQUAL) {
				list_for_each_entry_safe(rangeentry, rangestore, data->ranges, list) {
				
					list_del(&rangeentry->list);
					free(rangeentry);
				}
			return true;
			}
		}
	}
	return true;
}

struct list_head *ParseDependencies(struct search_options *options)
{
	int line = 0;
	FILE *fp = fopen(options->depsfile, "r");
	struct list_head *head;

	if (! fp) {
		fprintf(stderr, "WARNING: %s: %s\n", options->depsfile, strerror(errno));
		return NULL;
	}

	head = (struct list_head *) malloc(sizeof(struct list_head));
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
		if (! ParseVersions(data, options)) {
			fprintf(stderr, "WARNING: %s:%d: syntax error, ignoring dependency %s.\n", options->depsfile, line, data->depname);
			free(data);
			continue;
		}
		if (ParseRanges(data, options))	{
			if (GetBestVersion(data, options))
				ListAppend(head, data, options);
		else
			free(data);

		}
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
			"        local-programs       look for locally installed packages\n"
			"        local-dir:<path>     look for packages/recipes under <path>\n"
			"        package-store        look for packages in the package store\n"
			"        recipe-store)        look for recipes in the recipe store\n"
			"  -q, --quiet                Do not warn when a dependency is not found\n"
			"  -h, --help                 This help\n", appname);
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

	goboPrograms = getenv("goboPrograms");
	if (! goboPrograms && options.repository == LOCAL_PROGRAMS) {
		fprintf(stderr, "To use local programs as repository you need to 'source GoboPath' before running this program.\n");
		return 1;
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
