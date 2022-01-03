/*
 * Copyright 2022 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#ifndef ASYNC_SAFE_H
#define ASYNC_SAFE_H

#include <stdio.h>
#include <stdlib.h>

// Async-safe analogs of common functions

void safe_fputs(int fd, const char *s);

// snprintf isn't officially async-safe but should be if not using floats
#define safe_fprintf(fd, fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    safe_fputs(fd, _buf); \
  } while(0)

typedef struct {
  size_t size;
} MemControlBlock;

#define AS_BLOCK(ptr) ((MemControlBlock *)(ptr))
#define RAW2USER(ptr) ((void *)(AS_BLOCK(ptr) + 1))
#define USER2RAW(ptr) ((void *)(AS_BLOCK(ptr) - 1))

void *safe_malloc(size_t n, int error_fd);
void safe_free(void *p, int error_fd);

char *safe_basename(char *f);

int safe_fnmatch(const char *p, const char *s);

#endif
