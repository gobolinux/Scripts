// Copyright (C) 2008 Michael Homer <=mwh>
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
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
#define DATAFILE "/Programs/Scripts/Current/Data/CommandNotFound.data"

void multiprogrammessage(char * executable, char * program, char * program2) {
	printf("The program '%s' is not currently installed.\nIt is available in the following packages:\n", executable);
	printf(" %s, %s", program, program2);
	while (program2 = strtok(NULL, " "))
		printf(", %s", program2); // The last program will include the newline in it, no need to be explicit
	printf("You can install one of these by typing (for example):\n InstallPackage %s\nor\n Compile %s\n", program, program);
}

void singleprogrammessage(char * executable, char * program) {
	printf("The program '%s' is not currently installed.\nYou can install it by typing:\n", executable);
	printf(" InstallPackage %sor\n Compile %s", program, program);
}

int foundexecutable(char * executable, char * target) {
	// As the message output is different for multiple-program entries, read
	// to check whether there is more than one and pass the data on accordingly
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
	if (0 != lo)
		fgets(entry, BUFLEN, fp);
	while (ftell(fp) <= hi) {
		fgets(entry, BUFLEN, fp);
		executable = strtok(entry, " ");
		if (strcmp(executable, target) == 0)
			return foundexecutable(executable, target);
	}
	return 1;
}

int binsearch(FILE * fp, char * target, int lo, int hi, char * last, int depth) {
	int mid = lo + (hi-lo)/2;
	char entry [BUFLEN];
	char * executable;
	if ((depth > MAX_ITERATIONS))
		return depth; // No infinite loops when we're not getting anywhere
	// Switch to a linear search when we're getting close, for the edge cases
	if (hi-lo<LINEAR_SEARCH_THRESHOLD)
		return linsearch(fp, target, lo, hi);
	// Jump to our current midpoint
	fseek(fp, mid, SEEK_SET);
	// We're probably in the middle of a line, so discard it, then use the
	// next, *unless* we're right at the start.
	if (0 != mid) 
		fgets(entry, BUFLEN, fp);
	fgets(entry, BUFLEN, fp);
	executable = strtok(entry, " ");
	// Terminate if we're at the same entry we were at last time
	if (last && (strcmp(last, executable) == 0))
		return depth;
	int cmpval = strcmp(executable, target);
	if (0 == cmpval)
		return foundexecutable(executable, target);
	else if (0 > cmpval)
		return binsearch(fp, target, mid, hi, executable, depth + 1);
	else if (0 < cmpval)
		return binsearch(fp, target, lo, mid, executable, depth + 1);
}

int main(int argc, char **argv) {
	FILE * fp;
	if (argc < 2)
		return 1;
	if (0 == strcmp("--help", argv[1])) {
		puts("Usage: CommandNotFound <command>\nIntended to be run automatically from shell hooks.");
		return 0;
	}
	// Stat for the filesize to initialise the binary search
	struct stat st;
	stat(DATAFILE, &st);
	
	if (!(fp = fopen(DATAFILE, "r")))
		return 1; // If file doesn't exist, fail silently
	if (binsearch(fp, argv[1], 0, st.st_size, NULL, 0)) {
		printf("The program '%s' is not currently installed, and no known package contains it.\n", argv[1]);
		return 1;
	}
	fclose(fp);
	return 0;
}
