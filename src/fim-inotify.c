#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "common.h"

static pid_t target_pid = -1;
static char *target_ns = NULL;
static char *target_paths[32] = {NULL};
static unsigned int target_pathc = 0;
static unsigned int target_events = 0x0;

static void __attribute__((__noreturn__)) usage(void) {
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, " %s -p<pid> -n<namespace> -t<path>... [-e<event>...] [-f<format>]\n", program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs("Watch for namespace events within paths of a target PID.\n", out);

	fputs(USAGE_OPTIONS, out);
	fputs(" -p, --pid <pid>        target PID to watch\n", out);
	fputs(" -n, --ns <namespace>   target namespace (ipc|net|mnt|pid|user|uts)\n", out);
	fputs(" -t, --path <path>      target watch path(s)\n", out);
	fputs(" -e, --event <event>    event(s) to watch (access|modify|attrib|open|close|move|create|delete|all)\n", out);
	fputs(" -f, --format <format>  log format\n", out);

	// @TODO: file|dir|only_dir
  /*
  #define IN_ONLYDIR				0x01000000	// only watch the path if it is a directory
  #define IN_DONT_FOLLOW		0x02000000	// don't follow a sym link
  #define IN_EXCL_UNLINK		0x04000000	// exclude events on unlinked objects
  #define IN_ONESHOT				0x80000000	// only send event once
  */

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));
	printf(USAGE_MAN_TAIL("fim-inotify(2)"));

	exit(EXIT_SUCCESS);
}

void parseArgs(int argc, char *argv[]) {
	enum { OPT_PRESERVE_CRED = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'v'},
		{ "pid", required_argument, NULL, 'p' },
		{ "ns", required_argument, NULL, 'n' },
		{ "path", required_argument, NULL, 't'},
		{ "event", optional_argument, NULL, 'e'},
		{ "format", optional_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0 }
	};

  int c;
	while ((c = getopt_long(argc, argv, "+hvp:n:t:e::f::", longopts, NULL)) != EOF) {
		switch (c) {
			case 'h':
				usage();
			case 'v':
				printf(UTIL_LINUX_VERSION);
				exit(EXIT_SUCCESS);
			case 'p':
				target_pid = strtoul_or_err(optarg, "failed to parse PID");
				break;
			case 'n':
				target_ns = optarg;
				break;
			case 't':
				target_paths[target_pathc++] = optarg;
				break;
			case 'e':
				if (optarg) {
					if (strcmp(optarg, "access") == 0)      target_events |= IN_ACCESS;
					else if (strcmp(optarg, "modify") == 0) target_events |= IN_MODIFY;
					else if (strcmp(optarg, "attrib") == 0) target_events |= IN_ATTRIB;
					else if (strcmp(optarg, "open") == 0)   target_events |= IN_OPEN;
					else if (strcmp(optarg, "close") == 0)  target_events |= IN_CLOSE;
					else if (strcmp(optarg, "move") == 0)   target_events |= IN_MOVE;
					else if (strcmp(optarg, "create") == 0) target_events |= IN_CREATE;
					else if (strcmp(optarg, "delete") == 0) target_events |= IN_DELETE;
					else if (strcmp(optarg, "all") == 0)    target_events |= IN_ALL_EVENTS;
				}
				break;
			case 'f':
				if (optarg) {
					printf("optarg found: %s", optarg);
				} else {
				}
				break;
			default:
				errtryhelp(EXIT_FAILURE);
		}
	}

	if (target_pid == -1) {
		errexit("no target PID specified for --pid|-p");
	}
	if (target_ns == NULL || *target_ns == '\0') {
		errexit("no target namespace specified for --ns|-n");
	}
	if (target_pathc == 0) {
		errexit("no target path(s) specified for --path|-t");
	}
	if (target_events == 0x0) {
		target_events = IN_OPEN | IN_MODIFY;
	}
}

/**
 * read all available inotify events from the file descriptor `fd`
 * `wd` is the table of watch descriptors for the directories in `paths`
 * `pathc` is the length of `wd` and `paths`
 * `paths` [0->N-1] is the list of watched directories
 */
static void handle_events(int fd, int *wd, int pathc, char *paths[]) {
	/**
	 * some systems cannot read integer variables if they are not properly aligned
	 * on other systems, incorrect alignment may decrease performance
	 * hence, the buffer used for reading from the inotify file descriptor should
	 * have the same alignment as struct inotify_event
	 */
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	int i;
	ssize_t len;
	char *ptr;

	// loop while events can be read from the inotify file descriptor
	for (;;) {
		// read some events
		len = read(fd, buf, sizeof(buf));
		if (len == -1 && errno != EAGAIN) {
			errexit("read");
		}

		// if the non-blocking `read()` found no events to read, then it
		// returns with -1 with `errno` set to `EAGAIN`; exit the loop
		if (len <= 0) {
			break;
		}

		// loop over all events in the buffer
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			event = (const struct inotify_event *)ptr;

			// print event type
			if (event->mask & IN_ACCESS)        printf("IN_ACCESS: ");
			if (event->mask & IN_MODIFY)        printf("IN_MODIFY: ");
			if (event->mask & IN_ATTRIB)        printf("IN_ATTRIB: ");
			if (event->mask & IN_OPEN)          printf("IN_OPEN: ");
			if (event->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE: ");
			if (event->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE: ");
			if (event->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM: ");
			if (event->mask & IN_MOVED_TO)      printf("IN_MOVED_TO: ");
			if (event->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF: ");
			if (event->mask & IN_CREATE)        printf("IN_CREATE: ");
			if (event->mask & IN_DELETE)        printf("IN_DELETE: ");
			if (event->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF: ");

			// print the name of the watched directory
			for (i = 0; i < pathc; i++) {
				if (wd[i] == event->wd) {
					printf("%s", paths[i]);
					break;
				}
			}

			// print the name of the file
			if (event->len) {
				printf("/%s", event->name);
			}

			// @TODO: make file|directory watch configurable
			// print the type of filesystem object
			printf(" [%s]\n", (event->mask & IN_ISDIR ? "directory" : "file"));

			fflush(stdout);
		}
	}
}

int main(int argc, char *argv[]) {
	char buf, file[1024];
	int fdns, fdin, i, poll_num;
	int *wd;
	nfds_t nfds;
	struct pollfd fds[1];

  parseArgs(argc, argv);

  // -- JOIN THE NAMESPACE

  // get file descriptor for namespace
	sprintf(file, "/proc/%d/ns/%s", target_pid, target_ns);
  fdns = open(file, O_RDONLY);
  if (fdns == -1) {
    errexit("open");
  }

  // join namespace
  if (setns(fdns, 0) == -1) {
    errexit("setns");
  }

  // close namespace file descriptor
  close(fdns);

  // -- START THE INOTIFY WATCHER

  // create the file descriptor for accessing the inotify API
  fdin = inotify_init1(IN_NONBLOCK);
  if (fdin == -1) {
    errexit("inotify_init1");
  }

  // allocate memory for watch descriptors
  wd = calloc(target_pathc, sizeof(int));
  if (wd == NULL) {
    errexit("calloc");
  }

  // make directories for events
  for (i = 0; i < target_pathc; ++i) {
    wd[i] = inotify_add_watch(fdin, target_paths[i], target_events);
    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s'\n", target_paths[i]);
      errexit("inotify_add_watch");
    }
  }

  // prepare for polling
  nfds = 1;
  // inotify input
  fds[0].fd = fdin;
  fds[0].events = POLLIN;

  printf("Listening for events.\n");
  fflush(stdout);

  // wait for events
  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) {
        continue;
      }
      errexit("poll");
    }

    if (poll_num > 0) {
      if (fds[0].revents & POLLIN) {
				// inotify events are available
				handle_events(fdin, wd, target_pathc, target_paths);
      }
    }
  }

  printf("Listening for events stopped.\n");
  fflush(stdout);

  // close inotify file descriptor
  close(fdin);
  free(wd);

  exit(EXIT_SUCCESS);
}
