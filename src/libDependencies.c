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
#include "LinuxList.h"

static char * const currentString = "Current";
static char * const goboPrograms = "/Programs";

typedef enum {
	GREATER_THAN,			// >
	GREATER_THAN_OR_EQUAL,	// >=
	EQUAL,					// =
	NOT_EQUAL,				// !=
	LESS_THAN,				// <
	LESS_THAN_OR_EQUAL,		// <=
} operator_t;

struct version {
	char *version;			// version as listed in Resources/Dependencies
	operator_t op;			// one of the operators listed above
};

struct parse_data {
	char *workbuf;			// buffers the latest line read in Resources/Dependencies
	char *saveptr;			// strtok_r pointer for safe reentrancy
	bool hascomma;			// cache if a comma was found glued together with the first version
	char *depname;			// dependency name, as listed in Resources/Dependencies
	struct version v1;		// first optional restriction operator
	struct version v2;		// second optional restriction operator
	char fversion[NAME_MAX];// final version after restrictions were applied.
};

struct list_data {
	struct list_head list;	// link to the list on which we're inserted
	char path[PATH_MAX];	// full path to dependency
};

bool MatchRule(char *candidate, struct version *v)
{
	if (*candidate == '.' || !strcmp(candidate, "Variable") || !strcmp(candidate, "Settings") || !strcmp(candidate, "Current"))
		return false;
	switch (v->op) {
		case GREATER_THAN:
			return strcmp(candidate, v->version) > 0 ? true : false;
		case GREATER_THAN_OR_EQUAL:
			return strcmp(candidate, v->version) >= 0 ? true : false;
		case EQUAL: 
			return strcmp(candidate, v->version) == 0 ? true : false;
		case NOT_EQUAL: 
			return strcmp(candidate, v->version) != 0 ? true : false;
		case LESS_THAN: 
			return strcmp(candidate, v->version) < 0 ? true : false;
		case LESS_THAN_OR_EQUAL:
			return strcmp(candidate, v->version) <= 0 ? true : false;
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
		fprintf(stderr, "%s: %s, ignoring dependency.\n", path, strerror(errno));
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
		fprintf(stderr, "%s: %s, ignoring dependency.\n", path, strerror(errno));
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

char **GetVersionsFromList(struct parse_data *data)
{
	char cmdline[PATH_MAX], buf[LINE_MAX];
	char **versions = NULL;
	int num = 0;
	FILE *fp;
	
	snprintf(cmdline, sizeof(cmdline), "FindPackage -t official_package --full-list %s", data->depname);
	fp = popen(cmdline, "r");
	if (! fp) {
		fprintf(stderr, "%s: %s\n", cmdline, strerror(errno));
		return NULL;
	}

	while (! feof(fp)) {
		char *name, *version, *end, *ptr;

		if (! fgets(buf, sizeof(buf), fp))
			break;
		end = &buf[strlen(buf)-1];
		if (*end == '\n')
			*end = '\0';
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
		versions[num++] = strdup(version);
		versions[num] = NULL;
	}

	pclose(fp);
	return versions;
}

bool GetBestVersion(struct parse_data *data, bool searchpackages, bool searchlocalprograms)
{
	int i;
	char *entry, latest[NAME_MAX];
	char **versions = searchlocalprograms ? GetVersionsFromReadDir(data) : GetVersionsFromList(data);

	if (! versions)
		return false;

	memset(latest, 0, sizeof(latest));
	for (i=0; versions[i]; i++) {
		entry = versions[i];
		if (MatchRule(entry, &data->v1) && RuleBestThanLatest(entry, latest)) {
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
			strcpy(latest, entry);
			continue;
		}
	}
	if (! MatchRule(latest, &data->v1))
		memset(latest, 0, sizeof(latest));

	data->fversion[sizeof(data->fversion)-1] = '\0';
	strncpy(data->fversion, latest, sizeof(data->fversion)-1);
out:
	if (! latest[0])
		fprintf(stderr, "No packages matching requirements were found, skipping dependency %s\n", data->depname);
	for (i=0; versions[i]; i++)
		free(versions[i]);
	free(versions);
	return latest[0] ? true : false;
}

void ListAppend(struct list_head *head, struct parse_data *data)
{
	struct list_data *ldata = (struct list_data *) malloc(sizeof(struct list_data));
	snprintf(ldata->path, sizeof(ldata->path), "%s/%s/%s", goboPrograms, data->depname, data->fversion);
	list_add_tail(&ldata->list, head);
}

bool AlreadyInList(struct list_head *head, struct parse_data *data, char *depfile)
{
	struct list_data *ldata;
	if (list_empty(head))
		return false;
	list_for_each_entry(ldata, head, list) {
		if (strstr(ldata->path, data->depname)) {
			printf("WARNING: '%s' is included twice in %s\n", data->depname, depfile);
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

bool ParseName(struct parse_data *data)
{
	data->depname = strtok_r(data->workbuf, " \t", &data->saveptr);
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

bool ParseOperand(struct parse_data *data, struct version *v)
{
	char *ptr = strtok_r(NULL, " \t", &data->saveptr);
	if (! ptr) {
		// Only a program name without a version was supplied. Create symlinks for the View based on 'Current'.
		return GetCurrentVersion(data);
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

struct list_head *ParseDependencies(char *file, bool searchpackages, bool searchlocalprograms)
{
	int line = 0;
	FILE *fp = fopen(file, "r");
	if (! fp) {
		fprintf(stderr, "%s: %s\n", file, strerror(errno));
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

		if (! ParseName(data) || AlreadyInList(head, data, file))
			continue;

		if (! ParseOperand(data, &data->v1))
			continue;

		if (! ParseComma(data)) {
			if (GetBestVersion(data, searchpackages, searchlocalprograms))
				ListAppend(head, data);
			continue;
		}

		if (! ParseOperand(data, &data->v2)) {
			fprintf(stderr, "%s:%d: syntax error, ignoring dependency %s.\n", file, line, data->depname);
			continue;
		}

		if (GetBestVersion(data, searchpackages, searchlocalprograms))
			ListAppend(head, data);
	}

	fclose(fp);
	return head;
}

#ifdef DEBUG
#define _GNU_SOURCE
#include <getopt.h>

void ShowUsage(char *appname, int retval)
{
	printf("Syntax: %s <options> <Dependencies file>\n"
			"Available options are:\n"
			" -p, --packages          Search for Dependencies in the official packages repository\n"
			" -l, --local-programs    Search for Dependencies in the local programs tree\n", appname);
	exit(retval);
}

int main(int argc, char **argv)
{
	struct list_head *head;
	struct list_data *data;
	struct option options[] = {
		{ "packages",       0, 0, 'p' },
		{ "local-programs", 0, 0, 'l' },
		{ 0, 0, 0, 0 },
	};
	char *depfile = NULL;
	bool searchpackages = false;
	bool searchlocalprograms = false;

	while (true) {
		int index;
		int c = getopt_long(argc, argv, "plh", options, &index);
		if (c == -1)
			break;
		switch (c) {
			case 'h':
				ShowUsage(argv[0], 0);
			case 'p':
				printf("Looking at the package store\n");
				searchpackages = true;
				break;
			case 'l':
				printf("Looking at the local programs tree\n");
				searchlocalprograms = true;
				break;
			case '?':
			default:
				break;
		}
		if (optind < argc)
			depfile = argv[optind++];
	}
	if (! depfile || (! searchpackages && ! searchlocalprograms))
		ShowUsage(argv[0], 1);

	head = ParseDependencies(depfile, searchpackages, searchlocalprograms);
	if (! head)
		return 1;
	list_for_each_entry(data, head, list)
		printf("Mounting %s\n", data->path);

	return 0;
}
#endif
