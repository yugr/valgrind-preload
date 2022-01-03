/*                                                                                                                                                            * Copyright 2022 Yury Gribov
 *
 * The MIT License (MIT)
 *▫
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#include <stdio.h>
#include <stdlib.h>

int main() {
  if (0 == system("./child")) {
    fprintf(stderr, "parent: child did not fail as expected");
  }
  return 0;
}
