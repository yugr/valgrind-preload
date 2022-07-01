/*
 * Copyright 2022 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#ifndef COMMON_H
#define COMMON_H

// Common defs

#define PREFIX "libpregrind.so: "

#define EXPORT __attribute__((visibility("default")))

#define PAGE_SIZE (4 * 1024u)

// sys_errlist is not declared in newer Glibc's
extern const char *const sys_errlist[];

#endif
