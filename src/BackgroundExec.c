/**
 * BackgroundExec: a simple nohup-like utility that does not cause log files
 * to be created and that does not echo messages to the console.
 *
 * Written by Lucas C. Villa Real <lucasvr@gobolinux.org>
 * Released under the GNU GPL version 2 or above.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>

#define error_printf(fun, msg) fprintf(stderr, "Error:%s: %s at %s:%d\n", #fun, msg, __FILE__, __LINE__)

#define ERR_LEN 255
#define CHECK(x) do { \
	int retval = (x); \
	char err_msg[ERR_LEN] = "Unexpected error"; \
	if (retval) { \
		switch(retval) { \
			case ERR_OUTMEMORY: \
				snprintf(err_msg, ERR_LEN-1, "Not enough memory"); \
				break; \
		} \
		error_printf(x, err_msg); \
		exit(retval); \
	} \
} while (0)

#define ERR_OUTMEMORY         1      /* Out of memory */
#define ERR_NOEXECUTABLE      2      /* No executable */
#define ERR_BAD_ARGS          3      /* Bad arguments */

struct args {
	bool verbose;              /* Run in verbose mode? */
	const char *executable;    /* Executable to run */
	char **arguments;          /* Arguments to pass to executable */
};
static struct args args;


void show_usage_and_exit(char *exec, int err)
{
	printf(
	"Executes a command in the background, similarly to nohup, but without\n"
	"echoing messages to the console by default, and without appending output\n"
	"to a log file.\n"
	"\n"
	"Syntax: %s [options] <command> [arguments]\n"
	"\n"
	"Available options are:\n"
	"  -h, --help                This help\n"
	"  -v, --verbose             Run in verbose mode\n"
	"\n", exec);
	exit(err);
}

int parse_arguments(int argc, char *argv[])
{
	struct option long_options[] = {
		{"help",     no_argument, 0,  'h'},
		{"verbose",  no_argument, 0,  'v'},
		{0,          0,           0,   0 }
	};
	const char *short_options = "+hv";
	bool valid = true;
	int next = optind;

	/* Default values */
	args.verbose = false;
	args.executable = NULL;
	args.arguments = NULL;

	optind = 1;
	while (valid) {
		int c = getopt_long(argc, argv, short_options, long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
			case 'h':
				show_usage_and_exit(argv[0], 0);
				break;
			case 'v':
				args.verbose = true;
				break;
			case '?':
			default:
				valid = false;
				break;
		}
		if (valid)
			next = optind;
	}

	optind = next;
	if (optind < argc) {
		int i, num = argc - optind + 1;
		args.arguments = calloc(num, sizeof(char *));
		if (args.arguments == NULL) {
			perror("calloc");
			return ERR_OUTMEMORY;
		}
		for (i = optind; i < argc; i++)
			args.arguments[i-optind] = argv[i];
		args.executable = argv[optind];
	}

	return 0;
}


int main(int argc, char **argv)
{
	CHECK(parse_arguments(argc, argv));

	if (args.executable == NULL) {
		fprintf(stderr, "%s: no executable was specified. See --help.\n", argv[0]);
		exit(ERR_NOEXECUTABLE);
	}

	pid_t pid = fork();
	if (pid == 0) {
		if (! args.verbose) {
			int devnull_in = open("/dev/null", O_WRONLY);
			int devnull_out = open("/dev/null", O_RDONLY);
			dup2(devnull_in, STDIN_FILENO);
			dup2(devnull_out, STDOUT_FILENO);
			dup2(devnull_out, STDERR_FILENO);
			close(devnull_in);
			close(devnull_out);
		}
		execvp(args.executable, args.arguments);
		perror(args.executable);
	}
	return 0;
}
