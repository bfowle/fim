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

#ifndef __ARGUS_TREE__
#define __ARGUS_TREE__

#include "argusutil.h"

void validate_root_paths(struct arguswatch *watch);
char **find_root_path(const struct arguswatch *watch, const char *path);
static struct stat *find_root_stat(const struct arguswatch *watch, const char *path);
void remove_root_path(struct arguswatch **watch, const char *path);
int traverse_root(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
void find_replace_root_path(struct arguswatch **watch, const char *path);
static bool should_ignore_path(const struct arguswatch *watch, const char *path);
static int watch_path(struct arguswatch **watch, const char *path);
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
static int watch_path_recursive(struct arguswatch **watch, const char *path);
void watch_subtree(struct arguswatch **watch);
void rewrite_cached_paths(struct arguswatch **watch, const char *oldpathpf, const char *oldname,
    const char *newpathpf, const char *newname);
int remove_subtree(struct arguswatch **watch, const char *path);

#endif
