/**
 * MIT License
 *
 * Copyright (c) 2018 ClusterGarage
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __ARGUS_UTIL__
#define __ARGUS_UTIL__

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#define AW_ONLYDIR   0x00000001
#define AW_RECURSIVE 0x00000002
#define AW_FOLLOW    0x00000004

#define IN_EVENT_LEN (sizeof(struct inotify_event))
#define IN_BUFFER_SIZE (IN_EVENT_LEN + NAME_MAX + 1)
#define IN_EVENT_NEXT(evt, len, evtlen) ((struct inotify_event *)(((char *)(evt)) + (evtlen)))
#define IN_EVENT_OK(evt, buf, len) ((char *)(evt) < (char *)(buf) + (len))

#define FORMAT_PATH(fp, dir, file) do {           \
    snprintf(fp, sizeof(fp), "%s/%s", dir, file); \
} while(0)

#define DUMP_CACHE(watch) do {                                                           \
    printf("  $$$$ watch = %p:\n", (void *)(watch));                                     \
    printf("    $$   pid = %d; sid = %d\n", (watch)->pid, (watch)->sid);                 \
    printf("    $$   slot = %d\n", (watch)->slot);                                       \
    printf("    $$   fd = %d; processevtfd = %d\n", (watch)->fd, (watch)->processevtfd), \
    printf("    $$   rootpathc = %d\n", (watch)->rootpathc);                             \
    for (int i = 0; i < (watch)->rootpathc; ++i) {                                       \
        printf("     $     rootpaths[%d] = %s\n", i, (watch)->rootpaths[i]);             \
    }                                                                                    \
    printf("    $$   ignorec = %d\n", (watch)->ignorec);                                 \
    for (int i = 0; i < (watch)->ignorec; ++i) {                                         \
        printf("     $     ignore[%d] = %s\n", i, (watch)->ignores[i]);                  \
    }                                                                                    \
    printf("    $$   pathc = %d\n", (watch)->pathc);                                     \
    for (int i = 0; i < (watch)->pathc; ++i) {                                           \
        printf("     $     [%d] wd = %d; path = %s\n", i, (watch)->wd[i],                \
            (watch)->paths[i]);                                                          \
    }                                                                                    \
    printf("    $$   event_mask = %d\n", (watch)->event_mask);                           \
    printf("    $$   only_dir = %d\n", ((watch)->flags & AW_ONLYDIR));                   \
    printf("    $$   recursive = %d\n", ((watch)->flags & AW_RECURSIVE));                \
    if ((watch)->flags & AW_RECURSIVE) {                                                 \
        printf("    $$     max_depth = %d\n", (watch)->max_depth);                       \
    }                                                                                    \
    printf("    $$   follow_move = %d\n", ((watch)->flags & AW_FOLLOW));                 \
    fflush(stdout);                                                                      \
} while(0)

struct arguswatch {
    struct epoll_event epollevt[2];   // `epoll` structures for polling watchers.
    const char *name;                 // Name of ArgusWatcher.
    const char *node_name, *pod_name; // Name of node, pod in which process is running.
    const char *tags;                 // Custom tags for printing ArgusWatcher event.
    const char *log_format;           // Custom logging format for printing ArgusWatcher event.
    char **rootpaths;                 // Cached path name(s).
    char **ignores;                   // Ignore path patterns.
    char **paths;                     // Cached path name(s), including recursive traversal.
    int *wd;                          // Array of watch descriptors (-1 if slot unused).
    struct stat *rootstat;            // `stat` structures for root directories.
    unsigned int rootpathc;           // Cached path count.
    unsigned int ignorec;             // Ignore path pattern count.
    unsigned int pathc;               // Cached path count, including recursive traversal.
    uint32_t event_mask;              // Event mask for `inotify`.
    uint32_t flags;                   // Flags for ArgusWatcher.
    int pid, sid, slot;               // PID, Subject ID, `wlcache` slot.
    int fd, processevtfd, efd;        // `inotify` fd, anonymous pipe to send watch kill signal, `epoll` fd.
    int max_depth;                    // Max `nftw` depth to recurse through.
};

struct arguswatch_event {
    struct arguswatch *watch;
    const char *path_name, *file_name;
    uint32_t event_mask;
    bool is_dir;
};

typedef void (*arguswatch_logfn)(struct arguswatch_event *);

extern struct arguswatch **wlcache; // Array of cached watches.
extern int wlcachec;

#endif
