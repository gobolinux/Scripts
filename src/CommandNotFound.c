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
#include <dirent.h>
#include <stdlib.h>

#define BUFLEN 512
// To hardwire each CNF to its own version's data, make the data file configurable.
#ifndef DATAFILE
#define DATAFILE "/Programs/Scripts/Current/Data/CommandNotFound.data"
#endif

// Defined like this so it can easily be changed back to stderr if
// desired.
#define OUTPUT stdout

void multiprogrammessage(char * executable, char * program, char * program2) {
	fprintf(OUTPUT, "The program '%s' is not currently installed.\n"
	                "It is available in the following packages:\n",
	                executable);
	fprintf(OUTPUT, " %s, %s", program, program2);
	while ((program2 = strtok(NULL, " ")))
		fprintf(OUTPUT, ", %s", program2);
	// The last program will include the newline in it, no need to be explicit
	fprintf(OUTPUT, "You can install one of these by typing (for example):\n"
                    " InstallPackage %s\nor\n Compile %s\n",
                    program, program);
}

void singleprogrammessage(char * executable, char * program) {
	fprintf(OUTPUT, "The program '%s' is not currently installed.\n"
	                "You can install it by typing:\n", executable);
	fprintf(OUTPUT, " InstallPackage %sor\n Compile %s", program, program);
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

int binsearch(FILE * fp, char * target, int lo, int hi) {
	int mid = lo + (hi - lo) / 2;
	char entry [BUFLEN];
	char * executable;
	// Definitely not going to find anything, so quit here.
        if ((lo == mid && (lo != 0 || hi == 0)) || (lo == 0 && hi == 1))
		return 1;
	// Jump to our current midpoint
	fseek(fp, mid, SEEK_SET);
	// We're probably in the middle of a line, so discard it, then use
	// the next, *unless* we're right at the start.
	if (0 != mid) 
		if (fgets(entry, BUFLEN, fp) != entry)
			return 1;;
	if (fgets(entry, BUFLEN, fp) != entry)
		return 1;
	executable = strtok(entry, " ");
	int cmpval = strcmp(executable, target);
	if (0 > cmpval)
		return binsearch(fp, target, mid, hi);
	else if (0 < cmpval)
		return binsearch(fp, target, lo, mid);
	return foundexecutable(executable, target);
}

// Calculate the Damerau-Levenshtein distance between two strings.
// This is the number of additions, deletions, substitutions, and
// transpositions needed to transform one into the other.
// This algorithm is O(m) in space complexity and O(n*m) in time.
int damlev(char *word1, char *word2) {
	int i, j;
	// Cache the lengths.
	int lw1 = strlen(word1);
	int lw2 = strlen(word2);
	// Swap the words so word2 is always shortest - better space
	// complexity, and time complexity is the same.
	if (lw2 > lw1) {
		char *tmp = word1;
		word1 = word2;
		word2 = tmp;
		int ltmp;
		ltmp = lw1;
		lw1 = lw2;
		lw2 = ltmp;
	}
	// We only need to keep the current and two previous rows of the
	// matrix around - call these thisrow, oneago, twoago, and
	// initialise thisrow to 0 1 2 3 ....
	int *oneago = NULL, *twoago = NULL;
	int *thisrow = calloc(lw2 + 1, sizeof(int));
	for (i = 0; i <= lw2; i++) {
		thisrow[i] = i;
	}
	for (i = 0; i < lw1; i++) {
		// Shift rows back and initialise thisrow for current location.
		free(twoago);
		twoago = oneago;
		oneago = thisrow;
		thisrow = calloc(lw2 + 1, sizeof(i));
		thisrow[0] = i + 1;
		for (j = 0; j < lw2; j++) {
			int delcost = oneago[j + 1] + 1;
			int addcost = thisrow[j] + 1;
			int subcost = oneago[j] + (word1[i] != word2[j]);
			// Cost is here the minimum of the above three.
			int cost = delcost < addcost ? delcost : addcost;
			cost = cost < subcost ? cost : subcost;
			// Now check for a transposition - if that gives better
			// cost, use it instead.
			if (i > 0 && j > 0 && word1[i] == word2[i-1] &&
				word1[i-1] == word2[i] && word1[i] != word2[i])
				cost = cost < twoago[j - 1] + 1 ? cost : twoago[j - 1] + 1;
			thisrow[j + 1] = cost;
		}
	}
	free(twoago);
	free(oneago);
	i = thisrow[lw2];
	free(thisrow);
	return i;
}

// True if word is in list - used to prevent duplicates below,
// inline function for clarity's sake.
static inline int in_list(char list[16][32], char *word, int len) {
	int i;
	for (i=0; i<len; i++)
		if (strcmp(list[i], word) == 0)
			return 1;
	return 0;
}

// Search CNF database for possible typo executables, and suggest their
// programs as well.
void suggest_similar_uninstalled(char *target, char already[16][32], FILE *fp,
				 int threshold, int acount) {
	char els[16][128];
	char entry[BUFLEN];
	char *executable;
	char tmp[BUFLEN], firstprog[BUFLEN];
	int d, i;
	int eli = 0;
	firstprog[0] = 0;
	fseek(fp, 0, SEEK_SET);
	//puts("a");
	while (!feof(fp)) {
		if (fgets(entry, BUFLEN, fp) != entry)
			break;
		executable = strtok(entry, " ");
		d = damlev(target, executable);
		if (d <= threshold) {
			if (!in_list(already, executable, acount)) {
				strcpy(els[eli], executable);
				strcat(els[eli], " is part of ");
				executable = strtok(NULL, " ");
				strcpy(tmp, executable);
				while ((executable = strtok(NULL, " "))) {
					strcat(els[eli], tmp);
					strcat(els[eli], ", ");
					strcpy(tmp, executable);
				}
				strcat(els[eli], tmp);
				if (!firstprog[0])
					strcpy(firstprog, tmp);
				eli++;
			}
			if (eli == 15)
				break;
		}
	}
	if (eli > 0) {
		if (acount == 0)
			printf("\nD");
		else
			printf("\nOr d");
		if (eli == 1)
			printf("id you mean this uninstalled command?\n");
		else
			printf("id you mean one of these "
			       "uninstalled commands?\n");
		for (i = 0; i < eli; i++)
			printf("  %s", els[i]);
		if (firstprog[strlen(firstprog) - 1] == '\n')
			firstprog[strlen(firstprog) - 1] = 0;
		fprintf(OUTPUT, "You can install one of these by typing "
			"(for example):\n"
			" InstallPackage %s\nor\n Compile %s\n",
			firstprog, firstprog);
	}

}

// Look through PATH for executables that are close to our target,
// and suggest typo corrections. After that look through the CNF
// database again for similar names and suggest them too.
void suggest_similar(char *target, FILE *fp) {
	// Set a minimum closeness we'll care about. At least half
	// the executable name must be the same to count.
	int mindl = strlen(target) / 2;
	DIR *dp;
	struct dirent *ep;
	// Fixed size for ease later. We never want to have that many entries
	// anyway, it becomes unwieldy.
	char els[16][32];
	// For non-Gobo systems, we'll need to iterate through all the
	// PATH elements. It may be desirable even here for user-added
	// paths (perhaps ~/bin).
	char *pathvar = getenv("PATH");
	char *pathel;
	int eli = 0;
	int i;
	pathel = strtok(pathvar, ":");
	while (pathel != NULL) {
		dp = opendir(pathel);
		if (dp != NULL) {
			while ((ep = readdir(dp))) {
				if (strlen(ep->d_name) > 31)
					continue;
				int d = damlev(target, ep->d_name);
				if (d < mindl) {
					mindl = d;
					eli = 0;
				}
				if (d == mindl && eli < 15) {
					if (!in_list(els, ep->d_name, eli)) {
						strcpy(els[eli], ep->d_name);
						eli++;
					}
				}
			}
			closedir(dp);
		}
		pathel = strtok(NULL, ":");
	}
	if (eli > 0) {
		if (eli == 1)
			printf("Did you mean this?\n  ");
		else
			printf("Did you mean one of these?\n  ");
		for (i = 0; i < eli; i++)
			printf("%s%s", els[i], (i + 1 < eli ? ", " : ""));
		puts("");
	}
	suggest_similar_uninstalled(target, els, fp, mindl, eli);
}

int main(int argc, char **argv) {
	FILE * fp;
	char shortexec [ 50 ];
	size_t hyphenpos;
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
		// Not found, but check whether this was a versioned name,
		// and try the bare executable if it was.
		hyphenpos = (size_t) strrchr(argv[1], '-');
		if (hyphenpos) {
			hyphenpos -= (size_t) argv[1];
			strncpy(shortexec, argv[1], (size_t)hyphenpos);
			shortexec[hyphenpos] = '\0';
			if (binsearch(fp, shortexec, 0, st.st_size)) {
				fclose(fp);
				return 1;
			}
		} else {
			// Not found, so see if there's a close existing command
			suggest_similar(argv[1], fp);
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}
// Local variables:
// mode: c
// indent-tabs-mode: t
// tab-width: 8
// c-basic-offset: 8
// End:
