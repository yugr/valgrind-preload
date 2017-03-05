/*
 * Copyright 2017 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define PAGE_SIZE (4 * 1024)

#define EXPORT __attribute__((visibility("default")))

#define PREFIX "libpregrind.so: "

int (*real_execl)(const char *path, const char *arg, ...);
int (*real_execlp)(const char *file, const char *arg, ...);
int (*real_execle)(const char *path, const char *arg, ...);
int (*real_execv)(const char *path, char *const argv[]);
int (*real_execvp)(const char *file, char *const argv[]);
int (*real_execve)(const char *path, char *const argv[], char *const envp[]);
int (*real_execvpe)(const char *file, char *const argv[], char *const envp[]); 

int log_fd = STDERR_FILENO;
char *vg_log_path_templ;
char *log_file;
int v;
int disable;
int i_am_root;
char *blacklist[64];
volatile int is_initialized;

// Async-safe print
static void write_string(const char *s) {
  ssize_t written, rem = strlen(s);
  while(rem) {
    written = write(log_fd, s, rem);
    assert(written >= 0);
    rem -= written;
    s += written;
  }
}

// Async-safe printf (snprintf isn't officially async-safe but should be if not using floats)
#define Printf(fmt, ...) do { \
    char _buf[128]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    write_string(_buf); \
  } while(0)

// Async-safe malloc (map isn't officially async-safe but most probably is)
static void *Malloc(size_t n) {
  n = (n + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
  void *ret = mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!ret || ret == (void *)-1) {
    Printf(PREFIX "Malloc() failed to allocate %zd bytes: %s\n", n, sys_errlist[errno]);
    abort();
  }
  return ret;
}

static char *Basename(char *f) {
  char *sep = strrchr(f, '/');
  return sep ? sep + 1 : f;
}

// Not very efficient but enough for our simple usecase
static int Fnmatch(const char *p, const char *s) {
  if(*p == 0 || *s == 0)
    return *p == *s;

  switch(*p) {
  case '?':
    return *s && Fnmatch(p + 1, s + 1);
  case '*':
    if(p[1] == 0)
      return 1;
    else if(p[1] == '*')
      return Fnmatch(p + 1, s);
    else {
      size_t i;
      for(i = 0; s[i]; ++i)
        if(p[1] == s[i] && Fnmatch(p + 2, s + i + 1))
          return 1;
      return 0;
    }
  default:
    return *p == *s && Fnmatch(p + 1, s + 1);
  }

  abort();
}

static char **va_list_to_argv(va_list ap, const char *arg0) {
  void *buf = Malloc(PAGE_SIZE);
  const char **args = (const char **)buf;
  size_t max_args = PAGE_SIZE / sizeof(const char *);

  args[0] = arg0;
  ++args;
  --max_args;

  while(arg0) {
    assert(max_args > 0);
    arg0 = va_arg(ap, const char *);
    args[0] = arg0;
    ++args;
    --max_args;
  } while(arg0);

  return (char **)buf;
}

static char *get_prog_name() {
  FILE *p = fopen("/proc/self/cmdline", "rb");
  assert(p);

  static char s[128];
  memset(s, 0, sizeof(s));

  int nread = fread(s, 1, sizeof(s), p);
  if(nread < 0 || !memchr(s, 0, sizeof(s))) {
    dprintf(log_fd, PREFIX "fread() from /proc/self/exe failed: %s\n", sys_errlist[errno]);
    abort();
  }

  return Basename(s);
}

static void maybe_init() {
  assert(!is_initialized);

  const char *verbose = getenv("PREGRIND_VERBOSE");
  if(verbose) {
    v = atoi(verbose);
  }

  // TODO: PREGRIND_FLAGS (for --track-origins=yes, etc.)
  char *log_dir_rel = getenv("PREGRIND_LOG_PATH");
  if(log_dir_rel) {
    char *log_dir = log_dir_rel;
    if(log_dir[0] != '/') {
      log_dir = realpath(log_dir, 0);
      if(!log_dir) {
        fprintf(stderr, PREFIX "realpath() of %s failed: %s\n", log_dir_rel, sys_errlist[errno]);
        abort();
      }

      // Absolutize to protect against chdirs
      if(0 != setenv("PREGRIND_LOG_PATH", log_dir, 1)) {
        fprintf(stderr, PREFIX "setenv() failed: %s\n", sys_errlist[errno]);
        abort();
      }
    }

    const char *name = get_prog_name();
    size_t name_len = strlen(name);

    size_t log_dir_len = strlen(log_dir);
    vg_log_path_templ = malloc(log_dir_len + 20);
    sprintf(vg_log_path_templ, "%s/vg.%d.", log_dir, (int)getuid());  // FIXME: snprintf

    log_file = malloc(log_dir_len + name_len + 30);
    sprintf(log_file, "%s/%s.%d.%d", log_dir, name, (int)getuid(), (int)getpid()); // FIXME: snprintf
    log_fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
    if(-1 == log_fd) {
      fprintf(stderr, PREFIX "open() of %s failed: %s", log_file, sys_errlist[errno]);
//fprintf(stderr, PREFIX "uid=%d, gid=%d\n", getuid(), getgid());
//struct stat perm; stat(log_file, &perm); fprintf(stderr, PREFIX "owner=%d, group=%d\n", perm.st_uid, perm.st_gid);
      abort();
    }

    if(log_dir != log_dir_rel)
      free(log_dir);
  }

  const char *disable_ = getenv("PREGRIND_DISABLE");
  if(disable_) {
    disable = atoi(disable_);
  }

  const char *blacklist_name = getenv("PREGRIND_BLACKLIST");
  if(blacklist_name) {
    FILE *p = fopen(blacklist_name, "rb");
    if(!p) {
      dprintf(log_fd, PREFIX "failed to open blacklist file %s\n", blacklist_name);
      abort();
    }

    char s[128];
    size_t i = 0;
    while(fgets(s, sizeof(s), p)) {
      char *nl = strchr(s, '\n');
      if(nl)
        *nl = 0;

      if(i >= sizeof(blacklist) / sizeof(blacklist[0])) {
        dprintf(log_fd, PREFIX "failed to read patterns from %s: too many\n", blacklist_name);
        abort();
      }

//fprintf(stderr, "read from blacklist file: '%s'\n", s);
      blacklist[i++] = strdup(s);
    }

    fclose(p);
  }

#define INIT_REAL(f) do { \
    real ## _ ## f = (typeof(real ## _ ## f))dlsym(RTLD_NEXT, #f); \
    assert(real ## _ ## f); \
  } while(0)

  INIT_REAL(execl);
  INIT_REAL(execlp);
  INIT_REAL(execle);
  INIT_REAL(execv);
  INIT_REAL(execvp);
  INIT_REAL(execve);
  INIT_REAL(execvpe);

  i_am_root = getuid() == 0;

  if(v)
    dprintf(log_fd, PREFIX "initialized: v=%d, vg_log_path_templ=%s, log_file=%s, i_am_root=%d\n", v, vg_log_path_templ ? vg_log_path_templ : "(stderr)", log_file, i_am_root);

  // TODO: membar
  asm("");
  is_initialized = 1;
}

// Avoid issues with async-safety of dlsym by reading symbols at startup
__attribute__((constructor))
static void dummy() {
  maybe_init();
}

static char **init_valgrind_argv(char * const *argv) {
  void *buf = Malloc(PAGE_SIZE);
  char **new_args = buf;
  size_t max_args = PAGE_SIZE / sizeof(char *);

  new_args[0] = "/usr/bin/valgrind";
  new_args += 1;
  max_args -= 1;

  // Avoid "cannot create shared_mem file /tmp/vgdb-pipe-shared-mem-vgdb..." errors
  // when running under debign_pkg_test.
  // FIXME: this should be fixed in debign_pkg_test harness.
  new_args[0] = "--vgdb=no";
  new_args += 1;
  max_args -= 1;

  if(vg_log_path_templ) {
    char *name = Basename(argv[0]);
    size_t name_len = strlen(name);

    // TODO: --track-origins=yes --leak-check=full --showleak-kinds=definite ?
    size_t templ_len = strlen(vg_log_path_templ);
    char *out = Malloc(templ_len + name_len + 20);
    sprintf(out, "--log-file=%s%s.%%p", vg_log_path_templ, name);  // Valgrind understands %%p  // FIXME: snprintf

    new_args[0] = out;

    new_args += 1;
    max_args -= 1;
  }

  while(argv[0]) {
    assert(max_args > 0);
    new_args[0] = argv[0];
    ++new_args;
    ++argv;
    --max_args;
  }

  return (char **)buf;
}

static int can_instrument(const char *arg0, char *const *argv) {
  if(disable)
    return 0;

  // Do not try to instrument Valgrind itself
  if(strstr(arg0, "valgrind") || strstr(argv[0], "valgrind")) {
    if(v)
      Printf(PREFIX "not instrumenting %s: it's Valgrind!\n", arg0);
    return 0;
  }

  size_t i;
  for(i = 0; i < sizeof(blacklist) / sizeof(blacklist[0]) && blacklist[i]; ++i) {
    if(Fnmatch(blacklist[i], arg0)) {
//fprintf(stderr, "checking '%s' against '%s'\n", arg0, blacklist[i]);
      if(v)
        Printf(PREFIX "not instrumenting %s: blacklisted\n", arg0);
      return 0;
    }
  }

  struct stat perm;
  if(0 != stat(arg0, &perm)) {
    if(v)
      Printf(PREFIX "stat() failed on %s: %s\n", arg0, sys_errlist[errno]);
    // Do not abort() as some packages seem to check for presense of files by trying to run them
    return 0;
  }

  // Avoid calling setuids as VG can't instrument them
  if(!i_am_root
      && arg0[0] == '/') {  // TODO: otherwise manually find file in PATH
    if(perm.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) {
      if(v)
        Printf(PREFIX "not instrumenting %s: setuid\n", arg0);
      return 0;
    }
  }

  return 1;
}

static int exec_worker(const char *arg0, char *const *argv, int file_or_path, int has_envp, char *const *envp) {
  if(!is_initialized  // If initializer hasn't been called, we can't do much (we have to be async-safe)
      || !can_instrument(arg0, argv))
    return has_envp && file_or_path ? real_execvpe(arg0, argv, envp)
      : has_envp && !file_or_path ? real_execve(arg0, argv, envp)
      : !has_envp && file_or_path ? real_execvp(arg0, argv)
      : /* !has_envp && !file_or_path */ real_execv(arg0, argv);

  char **new_argv = init_valgrind_argv(argv);

  if(v) {
    write_string(PREFIX "executing: ");
    char **p = new_argv;
    for(p = new_argv; *p; ++p) {
      write_string(*p);
      write_string(" ");
    }
    write_string("\n");
  }

  return has_envp
    ? real_execve(new_argv[0], new_argv, envp)
    : real_execv(new_argv[0], new_argv);
}

EXPORT int execl(const char *path, const char *arg, ...) {
  if(v)
    Printf(PREFIX "intercepted execl: %s\n", path);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  return exec_worker(path, args, /*file_or_path*/0, /*has_envp*/ 0, 0);
}

EXPORT int execlp(const char *file, const char *arg, ...) {
  if(v)
    Printf(PREFIX "intercepted execlp: %s\n", file);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  return exec_worker(file, args, /*file_or_path*/ 1, /*has_envp*/ 0, 0);
}

EXPORT int execle(const char *path, const char *arg, ...) {
  if(v)
    Printf(PREFIX "intercepted execle: %s\n", path);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  char * const *e = va_arg(ap, char * const *);

  return exec_worker(path, args, /*file_or_path*/ 0, /*has_envp*/ 1, e);
}

// TODO: execlpe?

EXPORT int execv(const char *path, char *const argv[]) {
  if(v)
    Printf(PREFIX "intercepted execv: %s\n", path);
  return exec_worker(path, argv, /*file_or_path*/ 0, /*has_envp*/ 0, 0);
}

EXPORT int execve(const char *path, char *const argv[], char *const envp[]) {
  if(v)
    Printf(PREFIX "intercepted execve: %s\n", path);
  return exec_worker(path, argv, /*file_or_path*/ 0, /*has_envp*/ 1, envp);
}

EXPORT int execvp(const char *file, char *const argv[]) {
  if(v)
    Printf(PREFIX "intercepted execvp: %s\n", file);
  return exec_worker(file, argv, /*file_or_path*/ 1, /*has_envp*/ 0, 0);
}

EXPORT int execvpe(const char *file, char *const argv[], char *const envp[]) {
  if(v)
    Printf(PREFIX "intercepted execvpe: %s\n", file);
  return exec_worker(file, argv, /*file_or_path*/ 1, /*has_envp*/ 1, envp);
}
