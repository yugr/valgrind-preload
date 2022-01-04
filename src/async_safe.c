/*
 * Copyright 2022 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#include "async_safe.h"
#include "common.h"

#include <unistd.h>
#include <sys/mman.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

void safe_fputs(int fd, const char *s) {
  ssize_t written, rem = strlen(s);
  while(rem) {
    written = write(fd, s, rem);
    assert(written >= 0 && "write() failed");
    rem -= written;
    s += written;
  }
}

// mmap(2) isn't officially async-safe but most probably is
void *safe_malloc(size_t n, int error_fd) {
  n = (n + sizeof(MemControlBlock) + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
  void *ret = mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!ret || ret == MAP_FAILED) {
    safe_fprintf(error_fd, PREFIX "safe_malloc() failed to allocate %zd bytes: %s\n", n, sys_errlist[errno]);
    abort();
  }
  AS_BLOCK(ret)->size = n;
  return RAW2USER(ret);
}

void safe_free(void *p, int error_fd) {
  void *buf = USER2RAW(p);
  size_t size = AS_BLOCK(buf)->size;
  if (0 != munmap(buf, size)) {
    safe_fprintf(error_fd, PREFIX "safe_free() failed to unmap memory at 0x%p (size %zd)\n", p, size);
    abort();
  }
}

char *safe_strdup(const char *s, int error_fd) {
  char *ss = safe_malloc(strlen(s) + 1, error_fd);
  strcpy(ss, s);
  return ss;
}

char *safe_basename(char *f) {
  char *sep = strrchr(f, '/');
  return sep ? sep + 1 : f;
}

// Not very efficient but enough for our simple usecase
int safe_fnmatch(const char *p, const char *s) {
  if(*p == 0 || *s == 0)
    return *p == *s;

  switch(*p) {
  case '?':
    return *s && safe_fnmatch(p + 1, s + 1);
  case '*':
    if(p[1] == 0)
      return 1;
    else if(p[1] == '*')
      return safe_fnmatch(p + 1, s);
    else {
      size_t i;
      for(i = 0; s[i]; ++i)
        if(p[1] == s[i] && safe_fnmatch(p + 2, s + i + 1))
          return 1;
      return 0;
    }
  default:
    return *p == *s && safe_fnmatch(p + 1, s + 1);
  }

  abort();
}


