/*                                                                                                                                                            * Copyright 2022 Yury Gribov
 *
 * The MIT License (MIT)
 *▫
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#include <stdlib.h>

#ifdef __clang__
# define noipa optnone
#elif __GNUC__ < 8
# define noipa noinline,noclone
#endif

int *buf;

__attribute__((noipa))
int error() {
  return buf[1];
}

int main() {
  buf = (int *)malloc(1);
  return error();
}
