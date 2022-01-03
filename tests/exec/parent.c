/*                                                                                                                                                            * Copyright 2022 Yury Gribov
 *
 * The MIT License (MIT)
 *▫
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/wait.h>

int main() {
  int pid = fork();
  if (0 == pid) {
    // Child
    execl("./child", "./child", NULL);
    fprintf(stderr, "parent: failed to execute child\n");
    exit(1);
  }
  // Parent
  if (pid < 0) {
    fprintf(stderr, "parent: failed to fork\n");
    exit(1);
  }
  int wstatus;
  if (waitpid(pid, &wstatus, 0) < 0) {
    fprintf(stderr, "parent: failed to wait\n");
    exit(1);
  }
  if (WEXITSTATUS(wstatus) != RC) {
    fprintf(stderr, "parent: child exited for different reason\n");
    exit(1);
  }
  return 0;
}
