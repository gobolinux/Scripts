#ifndef __libdependencies_h
#define __libdependencies_h 1

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

struct list_head *ParseDependencies(char *file, bool searchpackages, bool searchlocalprograms);

#endif /* __libdependencies_h */
