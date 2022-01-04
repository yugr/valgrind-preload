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
#include <spawn.h>

extern char **environ;

int main() {
  char *argv[] = {"./child", 0};
  int pid;
  if (0 != posix_spawn(&pid, "./child", NULL, NULL, argv, environ)) {
    perror("parent: failed to spawn child");
    exit(1);
  }
  int wstatus;
  if (waitpid(pid, &wstatus, 0) < 0) {
    perror("parent: failed to wait for child");
    exit(1);
  }
  if (!WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != RC) {
    fprintf(stderr, "parent: child exited for different reason\n");
    exit(1);
  }
  return 0;
}
