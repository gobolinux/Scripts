// Copyright (C) 2008 Michael Homer <=mwh>
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

// Arbitrary, but much larger than we need here
#define MAX_ITERATIONS 30
#define BUFLEN 512
// Determined by exhaustive experimentation on the data; double the minimum
// tested working value (2008-02-26)
#define LINEAR_SEARCH_THRESHOLD 256
// For possible Rootless support, or hardwiring each CNF to its own version's
// data, make the data file configurable.
#ifndef DATAFILE
#define DATAFILE "/Programs/Scripts/Current/Data/CommandNotFound.data"
#endif

void multiprogrammessage(char * executable, char * program, char * program2) {
	fprintf(stderr, "The program '%s' is not currently installed.\n"
	                "It is available in the following packages:\n",
	                executable);
	fprintf(stderr, " %s, %s", program, program2);
	while (program2 = strtok(NULL, " "))
		fprintf(stderr, ", %s", program2);
	// The last program will include the newline in it, no need to be explicit
	fprintf(stderr, "You can install one of these by typing (for example):\n"
                    " InstallPackage %s\nor\n Compile %s\n",
                    program, program);
}

void singleprogrammessage(char * executable, char * program) {
	fprintf(stderr, "The program '%s' is not currently installed.\n"
	                "You can install it by typing:\n", executable);
	fprintf(stderr, " InstallPackage %sor\n Compile %s", program, program);
}

int foundexecutable(char * executable, char * target) {
	// As the message output is different for multiple-program entries, read
	// to check whether there's more than one and pass the data on accordingly
	char * program = strtok(NULL, " ");
	char * program2 = strtok(NULL, " ");
	if (NULL != program2)
		multiprogrammessage(executable, program, program2);
	else
		singleprogrammessage(executable, program);
	return 0;
}

int linsearch(FILE * fp, char * target, int lo, int hi) {
	char entry [BUFLEN];
	char * executable;
	fseek(fp, lo, SEEK_SET);
	// Unless we're right at the beginning, we're probably in the middle of a
	// line, so skip it.
	if (0 != lo)
		fgets(entry, BUFLEN, fp);
	// Perform a linear search from here to the upper bound of the range, and
	// hand off to foundexecutable() if the target is found.
	while (ftell(fp) <= hi) {
		fgets(entry, BUFLEN, fp);
		executable = strtok(entry, " ");
		if (strcmp(executable, target) == 0)
			return foundexecutable(executable, target);
	}
	// Not found
	return 1;
}

int binsearch(FILE * fp, char * target, int lo, int hi) {
	int mid = lo + (hi - lo) / 2;
	char entry [BUFLEN];
	char * executable;
	// Switch to a linear search when we're getting close, for the edge cases
	if (hi - lo < LINEAR_SEARCH_THRESHOLD)
		return linsearch(fp, target, lo, hi);

	// Jump to our current midpoint
	fseek(fp, mid, SEEK_SET);
	// We're probably in the middle of a line, so discard it, then use the
	// next, *unless* we're right at the start.
	if (0 != mid) 
		fgets(entry, BUFLEN, fp);
	fgets(entry, BUFLEN, fp);
	executable = strtok(entry, " ");
	int cmpval = strcmp(executable, target);
	if (0 == cmpval)
		return foundexecutable(executable, target);
	else if (0 > cmpval)
		return binsearch(fp, target, mid, hi);
	else if (0 < cmpval)
		return binsearch(fp, target, lo, mid);
}

int main(int argc, char **argv) {
	FILE * fp;
	if ((argc < 2) || (0 == strcmp("--help", argv[1]))) {
		puts("Usage: CommandNotFound <command>\n"
		     "Intended to be run automatically from shell hooks.");
		return 0;
	}
	// Stat for the filesize to initialise the binary search
	struct stat st;
	if (stat(DATAFILE, &st))
		// If the file doesn't exist or isn't readable for some reason,
		// just quit.
		return 1;
	
	fp = fopen(DATAFILE, "r");
	if (binsearch(fp, argv[1], 0, st.st_size)) {
		fprintf(stderr, "The program '%s' is not currently installed, and "
		                "no known package contains it.\n",
		                argv[1]);
		fclose(fp);
		return 1;
	}
	fclose(fp);
	return 0;
}
