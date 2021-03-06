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

#define _GNU_SOURCE
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "argustree.h"
#include "arguscache.h"
#include "argusutil.h"

static struct arguswatch **watch_;
static struct stat *rootstat_;
static char foundpath_[PATH_MAX], pidc_[8];

/**
 * Validate watch root paths are sanity checked before performing any
 * operations on them.
 *
 * @param watch
 */
void validate_root_paths(struct arguswatch *const watch) {
    struct stat sb;
    int i, j;

    // Count the number of root paths and check that the paths are valid.
    for (i = 0; i < watch->rootpathc; ++i) {
        // Check the paths are directories.
        if (lstat(watch->rootpaths[i], &sb) == EOF) {
#if DEBUG
            fprintf(stderr, "`lstat` failed on '%s'\n", watch->rootpaths[i]);
            perror("lstat");
#endif
            continue;
        }

        if ((watch->flags & AW_ONLYDIR) &&
            !S_ISDIR(sb.st_mode)) {
#if DEBUG
            fprintf(stderr, "'%s' is not a directory\n", watch->rootpaths[i]);
#endif
            continue;
        }
    }

    if ((watch->rootstat = calloc(watch->rootpathc, sizeof(struct stat))) == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    for (i = 0; i < watch->rootpathc; ++i) {
        // If the same filesystem object appears more than once in the command
        // line, this will cause confusion if we later try to remove an object
        // from the set of root paths; reject such duplicates now. Note that we
        // can't just do simple string comparisons of the arguments, since
        // different path strings may refer to the same filesystem object
        // (e.g., "foo" and "./foo"). So we use `stat` to compare inode numbers
        // and containing device IDs.
        if (lstat(watch->rootpaths[i], &watch->rootstat[i]) == EOF) {
#if DEBUG
            perror("lstat");
#endif
        }

        for (j = 0; j < i; ++j) {
            if (watch->rootstat[i].st_ino == watch->rootstat[j].st_ino &&
                watch->rootstat[i].st_dev == watch->rootstat[j].st_dev) {
#if DEBUG
                fprintf(stderr, "duplicate filesystem objects: %s, %s\n",
                    watch->rootpaths[i], watch->rootpaths[j]);
                continue;
#endif
            }
        }
    }
}

/**
 * Return the address of the element in `rootpaths` that points to a string
 * matching `path`, or NULL if there is no match.
 *
 * @param watch
 * @param path
 * @return
 */
char **find_root_path(const struct arguswatch *const watch, const char *const path) {
    int i;
    for (i = 0; i < watch->rootpathc; ++i) {
        if (watch->rootpaths[i] != NULL &&
            strcmp(path, watch->rootpaths[i]) == 0) {
            return &watch->rootpaths[i];
        }
    }
    return NULL;
}

/**
 * Return the address of the element in `rootstat` that points to a stat struct
 * matching `path`, or NULL if there is no match.
 *
 * @param watch
 * @param path
 * @return
 */
static struct stat *find_root_stat(const struct arguswatch *const watch, const char *const path) {
    int i;
    for (i = 0; i < watch->rootpathc; ++i) {
        if (watch->rootpaths[i] != NULL &&
            strcmp(path, watch->rootpaths[i]) == 0) {
            return &watch->rootstat[i];
        }
    }
    return NULL;
}

/**
 * Ceased to monitor a root path name (probably because it was renamed). Remove
 * this path from the root path list.
 *
 * @param watch
 * @param path
 */
void remove_root_path(struct arguswatch **watch, const char *const path) {
    char **p;
#if DEBUG
    printf("%s: %s\n", __func__, path);
    fflush(stdout);
#endif
    if ((p = find_root_path(*watch, path)) == NULL) {
#if DEBUG
        printf("%s: path not found!\n", __func__);
        fflush(stdout);
#endif
        return;
    }
    *p = NULL;

    --(*watch)->rootpathc;
    if ((*watch)->rootpathc == 0) {
#if DEBUG
        printf("no more root paths left to monitor\n");
        fflush(stdout);
#endif
    }
}

int traverse_root(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    if (rootstat_->st_ino == sb->st_ino) {
        snprintf(foundpath_, sizeof(foundpath_), "/proc/%d/root%s", (*watch_)->pid,
            path + (strlen(pidc_) + 13));
        return FTW_STOP;
    }
    return FTW_CONTINUE;
}
/**
 * Find moved path by locating it in /proc/[pid]/root by previously-stored
 * inode value. If found, update root path in cached watch.
 *
 * @param watch
 * @param path
 * @return
 */
void find_replace_root_path(struct arguswatch **watch, const char *const path) {
    char procpath[PATH_MAX];
    char **p;
    struct stat *rootstat;

    if ((p = find_root_path(*watch, path)) == NULL) {
#if DEBUG
        printf("%s: path not found!\n", __func__);
        fflush(stdout);
#endif
        return;
    }
    if ((rootstat = find_root_stat(*watch, path)) == NULL) {
#if DEBUG
        printf("%s: root stat not found!\n", __func__);
        fflush(stdout);
#endif
        return;
    }
    snprintf(procpath, sizeof(procpath), "/proc/%d/root/.", (*watch)->pid);
    snprintf(pidc_, sizeof(pidc_), "%d", (*watch)->pid);

    watch_ = watch;
    rootstat_ = rootstat;
    foundpath_[0] = '\0';
    if (nftw(procpath, traverse_root, 20, FTW_ACTIONRETVAL | FTW_PHYS) == EOF) {
#if DEBUG
        printf("nftw: %s: %s (directory probably deleted before we could watch)\n",
            path, strerror(errno));
        fflush(stdout);
#endif
    }

    if (foundpath_[0] == '\0') {
#if DEBUG
        printf("%s: moved path not found!\n", __func__);
        fflush(stdout);
#endif
        return;
    }

#if DEBUG
    printf("%s: %s -> %s\n", __func__, path, foundpath_);
    fflush(stdout);
#endif

    if ((*p = realloc(*p, sizeof(foundpath_) + 1)) == NULL) {
#if DEBUG
        perror("realloc");
#endif
    }
    free(*p);
    *p = strdup(foundpath_);
}

/**
 * Check if we should ignore path in the recursive tree check. If watching for
 * only directories and path is a file, ignore. If `ignore` list is provided
 * and matches this path, ignore.
 *
 * @param watch
 * @param path
 * @return
 */
static bool should_ignore_path(const struct arguswatch *const watch, const char *const path) {
    struct stat sb;
    int i;

    // Check the paths are directories.
    if (lstat(path, &sb) == EOF) {
#if DEBUG
        fprintf(stderr, "`lstat` failed on '%s'\n", path);
        perror("lstat");
#endif
        return true;
    }
    // Keep if it is a directory.
    if (S_ISDIR(sb.st_mode)) {
        return false;
    }

    // If only watching for directories, ignore.
    if (watch->flags & AW_ONLYDIR) {
        return true;
    }
    // Make sure path is directly in provided rootpaths.
    for (i = 0; i < watch->rootpathc; ++i) {
        if (strcmp(path, watch->rootpaths[i]) == 0) {
            return false;
        }
    }

    // If all else fails, ignore by default.
    return true;
}

/**
 * Add `path` to the watch list of the `inotify` file descriptor. The process
 * is not recursive. Returns number of watches/cache entries added for this
 * subtree.
 *
 * @param watch
 * @param path
 * @return
 */
static int watch_path(struct arguswatch **watch, const char *const path) {
    int wd;
    uint32_t flags;

    // Dont add non-directories unless directly specified by `rootpaths` and
    // `AW_ONLYDIR` flag is not set.
    if (should_ignore_path(*watch, path)) {
        return 0;
    }

    // We need to watch certain events at all times for keeping a consistent
    // view of the filesystem tree.
    flags = IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;
    if ((*watch)->flags & AW_ONLYDIR) {
        flags |= IN_ONLYDIR;
    }
    if (find_root_path(*watch, path) != NULL) {
        flags |= IN_MOVE_SELF;
    }

    // Make directories for events.
    if ((wd = inotify_add_watch((*watch)->fd, path, (*watch)->event_mask | flags)) == EOF) {
        // By the time we come to create a watch, the directory might already
        // have been deleted or renamed, in which case we'll get an ENOENT
        // error. Log the error, but carry on execution. Other errors are
        // unexpected, and if we hit them, we give up.
#if DEBUG
        fprintf(stderr, "inotify_add_watch: %s: %s\n", path, strerror(errno));
        perror("inotify_add_watch");
#endif
        return (errno == ENOENT) ? 0 : -1;
    }

#if DEBUG
    if (find_watch(*watch, wd) > -1) {
        // This watch descriptor is already in the cache.
        printf("wd: %d already in cache (%s)\n", wd, path);
        fflush(stdout);
    }
#endif

    if (((*watch)->wd = realloc((*watch)->wd, ((*watch)->pathc + 1) * sizeof(int))) == NULL) {
#if DEBUG
        perror("realloc");
#endif
        return -1;
    }
    (*watch)->wd[(*watch)->pathc] = wd;

    if (((*watch)->paths = realloc((*watch)->paths, ((*watch)->pathc + 1) * sizeof(char *))) == NULL) {
#if DEBUG
        perror("realloc");
#endif
        return -1;
    }
    // No need to `free` before the `strdup` here because we clear out the
    // individual paths in `clear_watch` in the case of rebuilding.
    (*watch)->paths[(*watch)->pathc] = strdup(path);

    ++(*watch)->pathc;

    return 0;
}

/**
 * Function called by `nftw` to traverse a directory tree that adds a watch
 * for each directory in the tree. Each successful call to this function
 * should return 0 to indicate to `nftw` that the tree traversal should
 * continue.
 *
 * @param path
 * @param sb
 * @param tflag
 * @param ftwbuf
 * @return
 */
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    int i;
    if (((*watch_)->flags & AW_ONLYDIR) &&
        !S_ISDIR(sb->st_mode)) {
        // Ignore nondirectory files.
        return FTW_CONTINUE;
    }
    // Stop recursing subtree if path in ignores list.
    for (i = 0; i < (*watch_)->ignorec; ++i) {
        if (strcmp(&path[ftwbuf->base], (*watch_)->ignores[i]) == 0) {
            return FTW_SKIP_SUBTREE;
        }
    }
    // Stop recursing siblings if reached max depth.
    if ((*watch_)->max_depth &&
        ftwbuf->level + 1 > (*watch_)->max_depth) {
        return FTW_SKIP_SIBLINGS;
    }

#if DEBUG
    printf("    traverse_tree: %s; level = %d\n", path, ftwbuf->level);
    fflush(stdout);
#endif
    return watch_path(watch_, path);
}

/**
 * Add `path` to the watch list of the `inotify` file descriptor. The process
 * is recursive: watch items are also created for all of the subdirectories of
 * `path`. Returns number of watches/cache entries added for this subtree.
 *
 * @param watch
 * @param path
 * @return
 */
static int watch_path_recursive(struct arguswatch **watch, const char *const path) {
    // Use FTW_PHYS to avoid following soft links to directories (which could
    // lead us in circles). By the time we come to process `path`, it may
    // already have been deleted, so we log errors from `nftw`, but keep on
    // going.
    watch_ = watch;
    if (nftw(path, traverse_tree, 20, FTW_ACTIONRETVAL | FTW_PHYS) == EOF) {
#if DEBUG
        printf("nftw: %s: %s (directory probably deleted before we could watch)\n",
            path, strerror(errno));
        fflush(stdout);
#endif
    }

    return (*watch)->pathc;
}

/**
 * Add watches and cache entries for a subtree, logging a message noting the
 * number entries added.
 *
 * @param watch
 */
void watch_subtree(struct arguswatch **watch) {
    int i;
    for (i = 0; i < (*watch)->rootpathc; ++i) {
        if ((*watch)->flags & AW_RECURSIVE) {
            watch_path_recursive(watch, (*watch)->rootpaths[i]);
        } else {
            watch_path(watch, (*watch)->rootpaths[i]);
        }
#if DEBUG
        printf("  watch_subtree: %s: %d entries added\n",
            (*watch)->rootpaths[i], (*watch)->pathc);
        fflush(stdout);
#endif
    }
}

/**
 * The directory `oldpathpf`/`oldname` was renamed to `newpathpf`/`newname`.
 * Fix up cache entries for `oldpathpf`/`oldname` and all of its subdirectories
 * to reflect the change.
 *
 * @param watch
 * @param oldpathpf
 * @param oldname
 * @param newpathpf
 * @param newname
 */
void rewrite_cached_paths(struct arguswatch **watch, const char *const oldpathpf, const char *const oldname,
    const char *const newpathpf, const char *const newname) {

    char fullpath[PATH_MAX], newpf[PATH_MAX], newpath[PATH_MAX + 1];
    size_t len;
    int i;

    FORMAT_PATH(fullpath, oldpathpf, oldname);
    FORMAT_PATH(newpf, newpathpf, newname);
    len = strlen(fullpath);

#if DEBUG
    printf("rename: %s -> %s\n", fullpath, newpf);
    fflush(stdout);
#endif

    for (i = 0; i < (*watch)->pathc; ++i) {
        if (strncmp(fullpath, (*watch)->paths[i], len) == 0 &&
            ((*watch)->paths[i][len] == '/' ||
            (*watch)->paths[i][len] == '\0')) {

            FORMAT_PATH(newpath, newpf, &(*watch)->paths[i][len]);
            free((*watch)->paths[i]);
            (*watch)->paths[i] = strdup(newpath);
#if DEBUG
            printf("    wd %d => %s\n", (*watch)->wd[i], newpath);
            fflush(stdout);
#endif
        }
    }
}

/**
 * Remove watches and cache entries for directory `path` and all of its
 * subdirectories. Returns number of entries that we (tried to) remove, or -1
 * if an `inotify_rm_watch` call failed.
 *
 * @param watch
 * @param path
 * @return
 */
int remove_subtree(struct arguswatch **watch, const char *const path) {
    size_t len = strlen(path);
    int i, cnt = 0;
    // The argument we receive might be a pointer to a path string that is
    // actually stored in the cache. If we remove that path part way through
    // scanning the whole cache then chaos ensues; so, create a temporary copy.
    char *pn = strdup(path);

#if DEBUG
    printf("removing subtree: %s\n", path);
    fflush(stdout);
#endif

    for (i = 0; i < (*watch)->pathc; ++i) {
        if (strncmp(pn, (*watch)->paths[i], len) == 0 &&
            ((*watch)->paths[i][len] == '/' ||
            (*watch)->paths[i][len] == '\0')) {
#if DEBUG
            printf("  removing watch: wd = %d (%s)\n",
                (*watch)->wd[i], (*watch)->paths[i]);
            fflush(stdout);
#endif

            if (inotify_rm_watch((*watch)->fd, (*watch)->wd[i]) == EOF) {
#if DEBUG
                printf("    inotify_rm_watch wd = %d (%s): %s\n", (*watch)->wd[i],
                    (*watch)->paths[i], strerror(errno));
                fflush(stdout);
#endif

                // When we have multiple renamers, sometimes
                // `inotify_rm_watch` fails. In this case, force a cache
                // rebuild by returning -1.
                cnt = -1;
                break;
            }

            mark_cache_slot_empty(i);
            ++cnt;
        }
    }

    free(pn);
    return cnt;
}
