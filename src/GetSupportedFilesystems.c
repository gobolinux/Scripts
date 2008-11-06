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

/* Get a list of supported filesystems, separated by linebreaks '\n' */
char *getvfs(void)
{
#ifdef __FreeBSD__
#define MAXLEN (sizeof(xvfsp[0].vfc_name))

	struct xvfsconf *xvfsp = NULL;
	size_t xvfslen;
	size_t bufferlen;
	char *buffer = NULL;
	size_t copied;
	int cnt, i;

	/* get the array of xvfsconf structs */
	if (sysctlbyname("vfs.conflist", NULL, &xvfslen, NULL, 0) < 0)
		goto fail;

	if (!(xvfsp = malloc(xvfslen)))
		goto fail;

	if (sysctlbyname("vfs.conflist", xvfsp, &xvfslen, NULL, 0) < 0)
		goto fail;

	/* allocate enough space for the list of filesystems */
	cnt = xvfslen / sizeof(struct xvfsconf);
	bufferlen = (cnt * sizeof(xvfsp[0].vfc_name))+1;

	if (!(buffer = malloc(bufferlen)))
		goto fail;

	/* extract filesystem names from xvfsconf structs */
	buffer[0] = '\0';
	copied = 0;
	for (i = 0; i < cnt; i++)
	{
		char *src = xvfsp[i].vfc_name;
		if (src[MAXLEN-1] != '\0')
		{
			src[MAXLEN-1] = '\0';
		}

		copied += snprintf(buffer + copied, copied < bufferlen ? bufferlen - copied : 0, "%s\n", src);
	}

done:
	if (xvfsp)
		free(xvfsp), xvfsp = NULL;

	return buffer;

fail:
	if (buffer)
		free(buffer), buffer = NULL;

	goto done;

#undef MAXLEN
#else
	FILE *fh = NULL;
	char readbuffer[1024];
	char *buffer = NULL;
	size_t copied = 0;
	size_t bufferlen = 0;

	fh = fopen(FILESYSTEMS_FILE, "r");
	if (!fh)
		goto fail;

	while (fgets(readbuffer, sizeof(readbuffer), fh) != NULL)
	{
		size_t readlen;
		size_t fslen;
		char *field;
		char *fs;

		/* nullterminate (fgets should actually do this for
		 * us, but better safe than sorry)
		 */
		readbuffer[sizeof(readbuffer)-1] = '\0';

		readlen = strlen(readbuffer);
		if (readbuffer[readlen-1] != '\n' && readbuffer[readlen-1] != '\r' && !feof(fh))
			goto fail;

		/* fields are separated by tab '\t' */
		field = readbuffer;
		if (strsep(&field, "\t\n\r") == NULL)
			continue;

		/* filesystem name is the second field on each line */
		fs = strsep(&field, "\t\n\r");
		if (fs == NULL || fs[0] == '\0')
			continue;
		
		fslen = strlen(fs);
		if (copied + fslen+1 >= bufferlen)
		{
			char *newptr = realloc(buffer, bufferlen + sizeof(readbuffer));
			if (newptr == NULL)
				goto done;

			buffer = newptr;
			bufferlen += sizeof(readbuffer);
		}
		copied += snprintf(buffer + copied, copied < bufferlen ? bufferlen - copied : 0, "%s\n", fs);
	}

done:
	if (fh != NULL)
		fclose(fh), fh = NULL;

	return buffer;

fail:
	if (buffer != NULL)
		free(buffer), buffer = NULL;

	goto done;
#endif
}

int main(int argc, char* argv[])
{
	char *list = getvfs();
	if (list != NULL)
	{
		printf("%s", list);
		free(list);
	}
	return 0;
}
