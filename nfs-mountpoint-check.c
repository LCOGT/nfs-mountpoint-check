/*
 * A utility to check an NFS mount to make sure it is working correctly
 * (neither hung, nor stale).
 *
 * Copyright 2019 Ira W. Snyder <isnyder@lco.global>
 * Copyright 2019 William Lindstrom <llindstrom@lco.global>
 * Copyright 2019 Las Cumbres Observatory <https://lco.global/>
 *
 * This code heavily inspired by:
 * https://github.com/acdha/mountstatus/blob/master/legacy-c-version/main.c
 */

#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

/* Maximum errno */
#define ERRNO_MAX 256

/*
 * Error status code which means "we were unable to determine the status
 * of the mountpoint." There is no errno code for this situation.
 */
static const int EUNKNOWN = 255;

/* Global variables */
static int verbosity = 1;
static pid_t child = 0;

/*
 * Build a logging function which formats a message and outputs it to the
 * screen when the user has requested the given logging level.
 */
#define DEFINE_LOG_FN(_name, _value)                                          \
static int _name(const char *fmt, ...) __attribute__((format(printf, 1, 2))); \
static int _name(const char *fmt, ...)                                        \
{                                                                             \
	int ret = 0;                                                          \
                                                                              \
	if (verbosity >= _value) {                                            \
		va_list args;                                                 \
		va_start(args, fmt);                                          \
		ret = vfprintf(stdout, fmt, args);                            \
		va_end(args);                                                 \
		fflush(stdout);                                               \
	}                                                                     \
                                                                              \
	return ret;                                                           \
}

/* Build logging functions */
DEFINE_LOG_FN(error,	1);
DEFINE_LOG_FN(verbose,	2);
DEFINE_LOG_FN(debug,	3);

/* Check methods */
static const int CHECK_METHOD_STAT	= 0x1;
static const int CHECK_METHOD_READDIR	= 0x2;

/*
 * Check an NFS mountpoint using the readdir method.
 *
 * - Open the mountpoint as a directory
 * - Attempt to read the first directory entry
 * - Close the directory
 *
 * This method has the side effect of performing some read only I/O on the
 * NFS server, but also has a good chance of detecting problems very quickly.
 *
 * Reading a single directory entry is sufficient to check if the server is
 * alive: the current directory (".") is always present, and is also always
 * the first entry returned by readdir().
 *
 * The CentOS 5 NFS client will successfully open and read the contents of
 * a mount point for up to several minutes after the server has crashed. Newer
 * versions behave sensibly (they hang immediately when the server has crashed).
 */
static int check_mountpoint_readdir(const char *path)
{
	struct dirent *dirent;
	DIR *dirp;

	dirp = opendir(path);
	if (dirp == NULL) {
		const int errsave = errno;
		debug("opendir failed: %s\n", strerror(errsave));
		return errsave;
	}

	dirent = readdir(dirp);
	if (dirent == NULL) {
		const int errsave = errno;
		debug("readdir failed: %s\n", strerror(errsave));
		closedir(dirp);
		return errsave;
	}

	if (closedir(dirp) < 0) {
		const int errsave = errno;
		debug("closedir failed: %s\n", strerror(errsave));
		return errsave;
	}

	/* success */
	return 0;
}

/*
 * Check an NFS mountpoint using the stat method.
 *
 * - Open the mountpoint as a directory
 * - Call fstat to get information about the directory
 * - Close the directory
 *
 * This method performs a minimal amount of read I/O on the NFS server, and
 * has a decent chance of detecting problems quickly. This method is
 * method is more reliable on newer systems.
 *
 * The CentOS 5 NFS client will successfully open the directory and retrieve
 * the status information for up to several minutes after the server has
 * crashed (or the mount has been made stale), with no indication of failure.
 * Newer versions behave better, though there can still be a small delay
 * between the server side problems and being able to detect them on the
 * client side.
 */
static int check_mountpoint_stat(const char *path)
{
	struct stat buf;
	int fd;

	/* open the directory */
	fd = open(path, O_RDONLY | O_SYNC);
	if (fd < 0) {
		const int errsave = errno;
		debug("open failed: %s\n", strerror(errsave));
		return errsave;
	}

	/* call fstat on the directory */
	if (fstat(fd, &buf) < 0) {
		const int errsave = errno;
		debug("fstat failed: %s\n", strerror(errsave));
		close(fd);
		return errsave;
	}

	/* close the directory */
	if (close(fd) < 0) {
		const int errsave = errno;
		debug("close failed: %s\n", strerror(errsave));
		return errsave;
	}

	/* success */
	return 0;
}

/*
 * Check a NFS mount point to see if it is working, stale, or hung.
 *
 * We attempt to avoid hangs or stalls in this process, but that is not always
 * possible. We also attempt to detect server-side failures quickly, however
 * the Linux Kernel NFS client has several attribute caches which cannot be
 * bypassed. It may take several minutes for the Linux Kernel NFS client to
 * notice that the server has hung, or that the mount point has become stale.
 *
 * This behavior varies widely across Linux Kernel versions. Older Kernel
 * versions are especially bad (they take up to minutes to notice that the
 * server has crashed).
 *
 * Please read "man 5 nfs" very carefully, especially the section
 * titled "DATA AND METADATA COHERENCE".
 */
static int check_mountpoint(const char *path, const int check_method)
{
	int ret = 0;

	if (check_method & CHECK_METHOD_STAT) {
		debug("before check_mountpoint_stat\n");
		ret = check_mountpoint_stat(path);
		debug("check_mountpoint_stat: ret=%d\n", ret);
		if (ret) {
			debug("check method stat failed: %d\n", ret);
			return ret;
		}
	}

	if (check_method & CHECK_METHOD_READDIR) {
		debug("before check_mountpoint_readdir\n");
		ret = check_mountpoint_readdir(path);
		debug("check_mountpoint_readdir: ret=%d\n", ret);
		if (ret) {
			debug("check method readdir failed: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

/*
 * Wait for a child process to exit, with a maximum time limit.
 *
 * The check process can hang for an arbitrary amount of time if the NFS
 * server is hung. This method implements a way to wait for a bounded time
 * for the check process to end, and then kill it if it takes too long.
 *
 * A subtlety here is that we need to handle the case where we are interrupted
 * by our ALRM signal while we are waiting inside waitpid(). We do this by
 * retrying our waitpid() if it was interrupted by a system call.
 */
static int wait_for_child(const int timeout)
{
	int status;

	/*
	 * Set an alarm so that we are interrupted if the child does not exit
	 * in a timely manner (the mount point is hung).
	 */
	alarm(timeout);

	/*
	 * Wait for the child to exit, making sure to safely handle the case
	 * where we are interrupted by our SIGALRM signal handler.
	 */
	while (1) {
		if (waitpid(child, &status, 0) < 0) {
			/* interrupted by signal, try again */
			if (errno == EINTR) {
				continue;
			}

			debug("waitpid failed: %s\n", strerror(errno));
			exit(1);
		}

		/*
		 * The call to waitpid succeeded, we have the status
		 * information we need. Break out of the loop.
		 */
		break;
	}

	/* Cancel the alarm */
	alarm(0);

	/* Child exited normally, return the child's exit status */
	if (WIFEXITED(status)) {
		const int ret = WEXITSTATUS(status);
		debug("child exited with ret = %d\n", ret);
		return ret;
	}

	/* Child was killed by a signal */
	if (WIFSIGNALED(status)) {
		const int ret = WTERMSIG(status);
		debug("child exited from signal = %d\n", ret);
		/*
		 * The child was most likely killed by the timeout handler, so
		 * we assume the mount is hung. We'll use ETIMEDOUT for this
		 * error case.
		 */
		if (ret == SIGKILL) {
			debug("child was killed by timeout, mountpoint hung\n");
			return ETIMEDOUT;
		}

		/*
		 * Otherwise, the child was killed through some unknown means
		 * that we did not initiate. We could not determine anything
		 * about the status of the mountpoint.
		 */
		debug("mountpoint in unknown status\n");
		return EUNKNOWN;
	}

	/* Child was stopped */
	if (WIFSTOPPED(status)) {
		debug("child was stopped\n");
		return EUNKNOWN;
	}

	/* Child was continued */
	if (WIFCONTINUED(status)) {
		debug("child was continued\n");
		return EUNKNOWN;
	}

	/* not sure how we'd possibly get here ... */
	return EUNKNOWN;
}

/*
 * Handler for SIGALRM signal.
 *
 * This is used to implement a hard timeout for the check process (child
 * process).
 *
 * Note that only signal-safe functions may be called here. Please see
 * "man 7 signal" for the list of functions you are allowed to call from
 * this context. (You cannot use printf, for example.)
 */
static void handle_sigalrm(int signum)
{
	/* dummy assignment, to eliminate gcc warning */
	signum = signum;

	/* no child process, nothing to kill */
	if (child <= 0) {
		return;
	}

	/*
	 * Attempt to kill the child process. If that fails, exit using the
	 * signal-safe variant of exit. There isn't anything else we can do.
	 */
	if (kill(child, SIGKILL) < 0) {
		_exit(EUNKNOWN);
	}
}

/* Help and usage information */
static void usage(char *argv[])
{
	printf("Usage: %s [options] <path>\n", argv[0]);
	printf("\n");
	printf("Check an NFS mount to determine whether it is operating correctly.\n");
	printf("\n");
	printf("Options:\n");
	printf("-h, --help              display this help information\n");
	printf("-i, --ignore-errno=x    ignore specific errno value\n");
	printf("-m, --method=x          check method (comma separated: default=stat,readdir)\n");
	printf("-t, --timeout=x         check timeout (seconds, default=2)\n");
	printf("-v, --verbose           increase verbosity (min=0, default=1, max=3)\n");
	printf("-q, --quiet             decrease verbosity (see above)\n");
	printf("\n");
}

/* Parse the user's specified check methods into a bitmask */
static int parse_check_method(char *s)
{
	int check_method = 0;
	char *tok = NULL;

	while ((tok = strtok(s, ",")) != NULL) {
		/* strtok requires NULL for subsequent calls */
		s = NULL;

		/* figure out what the user asked for */
		if (strcasecmp(tok, "stat") == 0) {
			debug("check_method |= stat\n");
			check_method |= CHECK_METHOD_STAT;
		} else if (strcasecmp(tok, "readdir") == 0) {
			debug("check_method |= readdir\n");
			check_method |= CHECK_METHOD_READDIR;
		} else {
			error("Unknown check method '%s'\n", tok);
			exit(EINVAL);
		}
	}

	return check_method;
}

/*
 * Equivalent to atoi(), except that it exits with an error message if the
 * user gave us a bogus value.
 */
static int safe_atoi(const char *s)
{
	char *end = NULL;
	long ret = 0;

	ret = strtol(s, &end, 10);
	if (s != end && errno != ERANGE && ret >= INT_MIN && ret <= INT_MAX) {
		return (int)ret;
	}

	error("Unable to parse integer: %s\n", s);
	exit(EINVAL);
}

int main(int argc, char *argv[])
{
	int exitcode_map[ERRNO_MAX];
	struct sigaction action;
	const char *path = NULL;
	int check_method_set = 0;
	int check_method = 0;
	int timeout = 2;
	int c = 0;
	int i;

	/*
	 * Initialize the mapping from child process return code to
	 * process exit status. By default, this process exits with
	 * the status code equal to the errno returned by any system
	 * call which failed, or 0 on success.
	 *
	 * We give the user an option to ignore any errno value as
	 * they see fit for their needs.
	 */
	for (i = 0; i < ERRNO_MAX; i++) {
		exitcode_map[i] = i;
	}

	/* option parsing: see the GNU getopt manual */
	while (1) {
		static struct option long_options[] = {
			{ "help", no_argument, NULL, 'h', },
			{ "method", no_argument, NULL, 'm', },
			{ "timeout", no_argument, NULL, 't', },
			{ "verbose", no_argument, NULL, 'v', },
			{ "quiet", no_argument, NULL, 'q', },
			{ "ignore-errno", no_argument, NULL, 'i', },
			{ NULL, no_argument, NULL, 0, },
		};

		int option_index = 0;
		int tmp = 0;

		c = getopt_long(argc, argv, "hm:t:vqi:", long_options, &option_index);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'h':
			usage(argv);
			exit(0);
		case 'i':
			tmp = safe_atoi(optarg);
			exitcode_map[tmp] = 0;
			break;
		case 'm':
			check_method_set = 1;
			check_method = parse_check_method(optarg);
			break;
		case 't':
			timeout = safe_atoi(optarg);
			break;
		case 'v':
			if (verbosity <= 2) {
				verbosity += 1;
			}
			break;
		case 'q':
			/* make sure verbosity doesn't drop below zero */
			if (verbosity >= 1) {
				verbosity -= 1;
			}
			break;
		case '?':
			/* getopt_long already printed an error message */
			break;
		default:
			/* unhandled error */
			abort();
		}
	}

	/* default check method: use all available methods */
	if (check_method_set == 0) {
		debug("No check method specified, using default: stat,readdir\n");
		check_method |= CHECK_METHOD_STAT;
		check_method |= CHECK_METHOD_READDIR;
	}

	debug("Argument check_method = 0x%.8x\n", check_method);
	debug("Argument timeout = %d\n", timeout);
	debug("Argument verbosity = %d\n", verbosity);
	for (i = 0; i < ERRNO_MAX; i++) {
		if (exitcode_map[i] != i) {
			debug("Exit status code %d ignored\n", i);
		}
	}

	/* the user did not specify any path to check */
	if ((argc - optind) <= 0) {
		error("No path was specified!\n");
		exit(EINVAL);
	}

	/* the user specified too many paths to check */
	if ((argc - optind) > 1) {
		error("Too many paths were specified!\n");
		exit(EINVAL);
	}

	/* this is the path the user specified */
	path = argv[optind];

	/* check that this program is being run as root */
	if (geteuid() > 0) {
		error("This program must be run as root\n");
		exit(EINVAL);
	}

	/* setup a signal handler for SIGALRM */
	memset(&action, 0, sizeof(action));
	action.sa_handler = handle_sigalrm;
	action.sa_flags = SA_RESTART;

	if (sigaction(SIGALRM, &action, 0) < 0) {
		const int errsave = errno;
		error("Unable to install SIGALRM handler: %s\n", strerror(errsave));
		exit(errno);
	}

	/* Print an informational message */
	verbose("About to check path: %s\n", path);

	/* Make sure all output has been processed */
	fflush(stdout);

	/*
	 * Fork into two processes.
	 *
	 * This is used as a safety measure, to make sure that this program
	 * does not hang forever if the NFS server is not responding to
	 * requests (it has crashed, etc.) and one of the system calls it makes
	 * hangs.
	 *
	 * The child process handles all of the interaction with the
	 * filesystem.
	 *
	 * The parent process waits for the child to exit. If the child does
	 * not exit in a timely manner, it is killed with the assumption that
	 * it is hung within a system call.
	 */
	child = fork();
	if (child < 0) {
		const int errsave = errno;
		error("Unable to create child process: %s\n", strerror(errsave));
		exit(errsave);
	} else if (child == 0) {
		/* this happens within the child process only */
		const int ret = check_mountpoint(path, check_method);
		exit(ret);
	} else {
		/* this happens within the parent process only */
		const int ret = wait_for_child(timeout);
		debug("wait_for_child(%d) = %d\n", timeout, ret);
		verbose("Check process exited with status code %d\n", ret);

		/*
		 * Exit with the return code the check process gave us, while
		 * also possibly ignoring any return codes that the user
		 * instructed us to ignore.
		 */
		exit(exitcode_map[ret]);
	}

	/* this is never reached */
	return 0;
}

/* vim: set ts=8 sts=8 sw=8 noet: */
