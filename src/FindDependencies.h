#ifndef __FIND_DEPENDENCIES_H
#define __FIND_DEPENDENCIES_H

#include "LinuxList.h"

typedef enum {
	NONE,
	GREATER_THAN,          // >
	GREATER_THAN_OR_EQUAL, // >=
	EQUAL,                 // =
	NOT_EQUAL,             // !=
	LESS_THAN,             // <
	LESS_THAN_OR_EQUAL,    // <=
} operator_t;

typedef enum {
	LOCAL_PROGRAMS,
	LOCAL_DIRECTORY,
	PACKAGE_STORE,
	RECIPE_STORE,
} repository_t;

struct version {
	struct list_head list;  // link to the list on which we're inserted
	char *version;          // version as listed in Resources/Dependencies
	operator_t op;          // one of the operators listed above
};

struct parse_data {
	char *workbuf;              // buffer from the latest line read in Resources/Dependencies
	char *saveptr;              // strtok_r pointer for safe reentrancy
	char *depname;              // dependency name, as listed in Resources/Dependencies
	struct list_head *versions; // link to the list of versions we've extracted
	struct list_head *ranges;   // link to the list of ranges we've built
	char fversion[NAME_MAX];    // final version after restrictions were applied.
	char url[NAME_MAX];         // url to dependency, when using the FindPackage backend
};

struct range {
	struct list_head list;      // link to the list on which we're inserted
	struct version low;         // low limit
	struct version high;        // high limit
};

struct list_data {
	struct list_head list;  // link to the list on which we're inserted
	char path[PATH_MAX];    // full url or path to dependency
};

struct search_options {
	repository_t repository;
	bool quiet;
	operator_t noOperator;
	const char *wantedArch;
	const char *dependency;
	const char *depsfile;
	const char *searchdir;
	const char *goboPrograms;
};

// Function prototypes
struct list_head *ParseDependencies(struct search_options *options);
void FreeDependencies(struct list_head **deps);

#endif /* __FIND_DEPENDENCIES_H */
