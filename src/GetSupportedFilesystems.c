/* 
 * Copyright (C) 2008 David Karell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifdef __FreeBSD__
#include <sys/types.h>	/* sysctlbyname() */
#include <sys/sysctl.h>	/* sysctlbyname() */
#include <sys/mount.h>	/* struct xvfsconf */
#endif
#include <stdlib.h>	/* malloc()/realloc()/free() */
#include <string.h>	/* strlen()/strsep() */
#include <stdio.h>	/* printf() / snprintf() / fgets() */

#define FILESYSTEMS_FILE "/proc/filesystems"


/* 
 * Extract a list of supported filesystems out of sysctls and print that, 
 * separating entries by linebreaks '\n' 
 */
#ifdef __FreeBSD__
static int print_vfs_sysctl(void)
{
	struct xvfsconf *xvfsp = NULL;
	size_t xvfslen;
	int cnt, i;

	/* get the array of xvfsconf structs */
	if (sysctlbyname("vfs.conflist", NULL, &xvfslen, NULL, 0) < 0)
		return -1;

	if (!(xvfsp = malloc(xvfslen)))
		return -1;

	if (sysctlbyname("vfs.conflist", xvfsp, &xvfslen, NULL, 0) < 0) {
		free(xvfsp);
		return -1;
	}

	/* extract filesystem names from xvfsconf structs */
	cnt = xvfslen / sizeof(struct xvfsconf);
	for (i = 0; i < cnt; i++)
		printf("%s\n", xvfsp[i].vfc_name);

	free(xvfsp);
	return 0;
}
#endif

/* 
 * Extract a list of supported filesystems out of FILESYSTEMS_FILE and print that, 
 * separating entries by linebreaks '\n' 
 */
#ifndef __FreeBSD__
static int print_vfs_procfs(void)
{
	FILE *fh;
	char readbuffer[1024];

	fh = fopen(FILESYSTEMS_FILE, "r");
	if (! fh)
		return -1;

	memset(readbuffer, 0, sizeof(readbuffer));
	while (fgets(readbuffer, sizeof(readbuffer)-1, fh) != NULL) {
		size_t readlen;
		char *field, *fs;

		readlen = strlen(readbuffer);
		if (readbuffer[readlen-1] != '\n' && readbuffer[readlen-1] != '\r' && !feof(fh)) {
			fclose(fh);
			return -1;
		}

		/* fields are separated by tab '\t' */
		field = readbuffer;
		if (strsep(&field, "\t\n\r") == NULL)
			continue;

		/* filesystem name is the second field on each line */
		fs = strsep(&field, "\t\n\r");
		if (fs == NULL || fs[0] == '\0')
			continue;
		
		printf("%s\n", fs);
	}
	fclose(fh);
	return 0;
}
#endif

int main(int argc, char* argv[])
{
#ifdef __FreeBSD__
	return print_vfs_sysctl();
#else
	return print_vfs_procfs();
#endif
}
