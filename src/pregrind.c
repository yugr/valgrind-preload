/*
 * Copyright 2017-2022 Yury Gribov
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
#include <ctype.h>

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

const char *vg_flags[128];
const char *vg_log_path_templ;
const char *log_file;
int v;
int disable;
int i_am_root;
char *blacklist[64];
volatile int is_initialized;

static int get_log_fd() {
  static int log_fd = -1;

  if(!log_file)
    return STDERR_FILENO;

  if(log_fd > 0)
    return log_fd;

  // We delay opening the file until we have something to write.
  // This is racy but this isn't a big deal (worst case the output will be corrupted).
  log_fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
  if(-1 == log_fd) {
    fprintf(stderr, PREFIX "open() of %s failed: %s\n", log_file, sys_errlist[errno]);
    abort();
  }

  return log_fd;
}

// Async-safe print
static void safe_puts(const char *s) {
  ssize_t written, rem = strlen(s);
  while(rem) {
    written = write(get_log_fd(), s, rem);
    assert(written >= 0 && "write() failed");
    rem -= written;
    s += written;
  }
}

// Async-safe printf (snprintf isn't officially async-safe but should be if not using floats)
#define safe_printf(fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    safe_puts(_buf); \
  } while(0)

typedef struct {
  size_t size;
} MemControlBlock;

#define AS_BLOCK(ptr) ((MemControlBlock *)(ptr))
#define RAW2USER(ptr) ((void *)(AS_BLOCK(ptr) + 1))
#define USER2RAW(ptr) ((void *)(AS_BLOCK(ptr) - 1))

// Async-safe malloc (mmap(2) isn't officially async-safe but most probably is)
static void *safe_malloc(size_t n) {
  n = (n + sizeof(MemControlBlock) + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
  void *ret = mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!ret || ret == MAP_FAILED) {
    safe_printf(PREFIX "safe_malloc() failed to allocate %zd bytes: %s\n", n, sys_errlist[errno]);
    abort();
  }
  AS_BLOCK(ret)->size = n;
  return RAW2USER(ret);
}

static void safe_free(void *p) {
  void *buf = USER2RAW(p);
  size_t size = AS_BLOCK(buf)->size;
  if (0 != munmap(buf, size)) {
    safe_printf(PREFIX "safe_free() failed to unmap memory at 0x%p (size %zd)\n", p, size);
    abort();
  }
}

static char *safe_basename(char *f) {
  char *sep = strrchr(f, '/');
  return sep ? sep + 1 : f;
}

// Not very efficient but enough for our simple usecase
static int safe_fnmatch(const char *p, const char *s) {
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

// Reserve space for size and at least one trailing nullptr
#define GET_EFFECTIVE_SIZE(size) ((size) - sizeof(MemControlBlock) - sizeof (char *))

static char **va_list_to_argv(va_list ap, const char *arg0) {
  void *buf = safe_malloc(PAGE_SIZE >> 1);
  const char **args = (const char **)buf;
  size_t max_args = GET_EFFECTIVE_SIZE(PAGE_SIZE >> 1) / sizeof(const char *);

  args[0] = arg0;
  ++args;
  --max_args;

  while(arg0) {
    assert(max_args > 0 && "Too many arguments");
    arg0 = va_arg(ap, const char *);
    args[0] = arg0;
    ++args;
    --max_args;
  } while(arg0);

  return (char **)buf;
}

static char *get_prog_name() {
  FILE *p = fopen("/proc/self/cmdline", "rb");
  assert(p && "Failed to read /proc");

  static char s[128];
  memset(s, 0, sizeof(s));

  int nread = fread(s, 1, sizeof(s), p);
  if(nread < 0 || !memchr(s, 0, sizeof(s))) {
    dprintf(get_log_fd(), PREFIX "fread() from /proc/self/exe failed: %s\n", sys_errlist[errno]);
    abort();
  }

  return safe_basename(s);
}

static char *trim_whites(char *s) {
  for(; isspace(*s); ++s);

  char *end = 0, *p;
  for(p = s; *p; ++p) {
    int space = isspace(*p);
    if(end && !space)
      end = 0;
    else if(!end && space)
      end = p;
  }

  if(end)
    *end = 0;

  return s;
}

static void maybe_init() {
  assert(!is_initialized && "Init called twice");

  const char *verbose = getenv("PREGRIND_VERBOSE");
  if(verbose) {
    v = atoi(verbose);
  }

  const char *flags = getenv("PREGRIND_FLAGS");
  if(flags) {
    flags = strdup(flags);

    size_t i = 0;

    while(flags) {
      for(; *flags == ' '; ++flags)

      if(!*flags)
        break;

      char *next = strchr(flags, ' ');
      if(next) {
        *next = 0;
        ++next;
      }

      assert(i < sizeof(vg_flags) / sizeof(vg_flags[0]) - 1 && "Too many flags");
      vg_flags[i++] = flags;

      flags = next;
    }

    vg_flags[i] = 0;
  }

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
    sprintf((char *)vg_log_path_templ, "%s/vg.%d.", log_dir, (int)getuid());  // FIXME: snprintf

    log_file = malloc(log_dir_len + name_len + 30);
    sprintf((char *)log_file, "%s/%s.%d.%d", log_dir, name, (int)getuid(), (int)getpid()); // FIXME: snprintf

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
      dprintf(get_log_fd(), PREFIX "failed to open blacklist file %s\n", blacklist_name);
      abort();
    }

    char buf[128];
    size_t i = 0;
    while(fgets(buf, sizeof(buf), p)) {
      char *s = buf;

      char *nl = strchr(s, '\n');
      if(nl)
        *nl = 0;

      if(i >= sizeof(blacklist) / sizeof(blacklist[0])) {
        dprintf(get_log_fd(), PREFIX "failed to read patterns from %s: too many\n", blacklist_name);
        abort();
      }

      // Strip comments
      char *comment = strchr(s, '#');
      if(comment)
        *comment = 0;

      s = trim_whites(s);

      if(*s)
        blacklist[i++] = strdup(s);
    }

    fclose(p);
  }

#define INIT_REAL(f) do { \
    real_ ## f = (typeof(real_ ## f))dlsym(RTLD_NEXT, #f); \
    assert(real_ ## f && "Failed to locate true exec"); \
  } while(0)

  INIT_REAL(execl);
  INIT_REAL(execlp);
  INIT_REAL(execle);
  INIT_REAL(execv);
  INIT_REAL(execvp);
  INIT_REAL(execve);
  INIT_REAL(execvpe);

#undef INIT_REAL

  i_am_root = getuid() == 0;

  if(v)
    dprintf(get_log_fd(), PREFIX "initialized: v=%d, vg_log_path_templ=%s, log_file=%s, i_am_root=%d\n", v, vg_log_path_templ ? vg_log_path_templ : "(stderr)", log_file, i_am_root);

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
  void *buf = safe_malloc(PAGE_SIZE >> 1);
  const char **new_args = buf;
  size_t max_args = GET_EFFECTIVE_SIZE(PAGE_SIZE >> 1) / sizeof(char *);

  new_args[0] = "/usr/bin/valgrind";
  ++new_args;
  --max_args;

  if(vg_log_path_templ) {
    char *name = safe_basename(argv[0]);
    size_t name_len = strlen(name);

    size_t templ_len = strlen(vg_log_path_templ);
    char *out = safe_malloc(templ_len + name_len + 20);
    sprintf(out, "--log-file=%s%s.%%p", vg_log_path_templ, name);  // Valgrind understands %%p  // FIXME: snprintf

    new_args[0] = out;
    ++new_args;
    --max_args;

  }

  const char **vg_flag;
  for(vg_flag = vg_flags; *vg_flag; ++vg_flag, ++new_args, --max_args) {
    assert(max_args && "Too many flags");
    new_args[0] = *vg_flag;
  }

  for(; argv[0]; ++new_args, --max_args, ++argv) {
    assert(max_args && "Too many arguments");

    char *argv0 = safe_malloc(strlen(argv[0]) + 1);
    strcpy(argv0, argv[0]);
    new_args[0] = argv0;
  }

  return (char **)buf;
}

static const char *find_file_in_path(const char *file, char *buf, size_t buf_sz) {
  const char *path = getenv("PATH");
  if(!path)
    return 0;

  struct stat perm;
  do {
    char *next = strchr(path, ':');

    int is_empty = (next && next == path + 1) || (!next && !path[0]);
    int needed;
    if(is_empty) {
      needed = snprintf(buf, buf_sz, "%s", file);
    } else {
      int len = next ? next - path : (int)strlen(path);
      needed = snprintf(buf, buf_sz, "%.*s/%s", len, path, file);
    }

    if(needed < 0 || (size_t)needed >= buf_sz) {
      safe_printf(PREFIX "failed to find file %s in path: string too long\n", file);
      return 0;
    }

    if(0 == stat(buf, &perm))
      return buf;

    path = next ? next + 1 : 0;
  } while(path);

  return 0;
}

static int can_instrument(const char *arg0, char *const *argv) {
  if(!is_initialized)  // If initializer hasn't been called, we can't do much (we have to be async-safe)
    return 0;

  if(disable)
    return 0;

  // Do not try to instrument Valgrind itself
  if(strstr(arg0, "valgrind") || strstr(argv[0], "valgrind")) {
    if(v)
      safe_printf(PREFIX "not instrumenting %s: it's Valgrind!\n", arg0);
    return 0;
  }

  char buf[256];
  if(!strchr(arg0, '/')) {
    const char *path = find_file_in_path(arg0, buf, sizeof(buf));
    if(!path) {
      safe_printf(PREFIX "not instrumenting %s: failed to find file in path\n", arg0);
      return 0;
    }
    arg0 = path;
  }

  size_t i;
  for(i = 0; i < sizeof(blacklist) / sizeof(blacklist[0]) && blacklist[i]; ++i) {
    if(safe_fnmatch(blacklist[i], arg0)) {
      if(v)
        safe_printf(PREFIX "not instrumenting %s: blacklisted\n", arg0);
      return 0;
    }
  }

  struct stat perm;
  if(0 != stat(arg0, &perm)) {
    if(v)
      safe_printf(PREFIX "stat() failed on %s: %s\n", arg0, sys_errlist[errno]);
    // Do not abort() as some packages seem to check for presense of files by trying to run them
    return 0;
  }

  // Avoid calling setuids as VG can't instrument them
  if(!i_am_root && (perm.st_mode & (S_ISUID | S_ISGID | S_ISVTX))) {
    if(v)
      safe_printf(PREFIX "not instrumenting %s: setuid\n", arg0);
    return 0;
  }

  return 1;
}

static int exec_worker(const char *arg0, char *const *argv, int file_or_path, int has_envp, char *const *envp) {
  if(!can_instrument(arg0, argv))
    return has_envp && file_or_path ? real_execvpe(arg0, argv, envp)
      : has_envp && !file_or_path ? real_execve(arg0, argv, envp)
      : !has_envp && file_or_path ? real_execvp(arg0, argv)
      : /* !has_envp && !file_or_path */ real_execv(arg0, argv);

  char **new_argv = init_valgrind_argv(argv);

  if(v) {
    safe_puts(PREFIX "executing: ");
    char **p = new_argv;
    for(p = new_argv; *p; ++p) {
      safe_puts(*p);
      safe_puts(" ");
    }
    safe_puts("\n");
  }

  int retcode = has_envp
    ? real_execve(new_argv[0], new_argv, envp)
    : real_execv(new_argv[0], new_argv);

  if (0 != retcode) {
    for (size_t i = 0; new_argv[i]; ++i)
      safe_free(new_argv[i]);
    safe_free(new_argv);
  }

  return retcode;
}

EXPORT int execl(const char *path, const char *arg, ...) {
  if(v)
    safe_printf(PREFIX "intercepted execl: %s\n", path);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  return exec_worker(path, args, /*file_or_path*/0, /*has_envp*/ 0, 0);
}

EXPORT int execlp(const char *file, const char *arg, ...) {
  if(v)
    safe_printf(PREFIX "intercepted execlp: %s\n", file);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  return exec_worker(file, args, /*file_or_path*/ 1, /*has_envp*/ 0, 0);
}

EXPORT int execle(const char *path, const char *arg, ...) {
  if(v)
    safe_printf(PREFIX "intercepted execle: %s\n", path);

  va_list ap;
  va_start(ap, arg);
  char **args = va_list_to_argv(ap, arg);

  char * const *e = va_arg(ap, char * const *);

  return exec_worker(path, args, /*file_or_path*/ 0, /*has_envp*/ 1, e);
}

EXPORT int execv(const char *path, char *const argv[]) {
  if(v)
    safe_printf(PREFIX "intercepted execv: %s\n", path);
  return exec_worker(path, argv, /*file_or_path*/ 0, /*has_envp*/ 0, 0);
}

EXPORT int execve(const char *path, char *const argv[], char *const envp[]) {
  if(v)
    safe_printf(PREFIX "intercepted execve: %s\n", path);
  return exec_worker(path, argv, /*file_or_path*/ 0, /*has_envp*/ 1, envp);
}

EXPORT int execvp(const char *file, char *const argv[]) {
  if(v)
    safe_printf(PREFIX "intercepted execvp: %s\n", file);
  return exec_worker(file, argv, /*file_or_path*/ 1, /*has_envp*/ 0, 0);
}

EXPORT int execvpe(const char *file, char *const argv[], char *const envp[]) {
  if(v)
    safe_printf(PREFIX "intercepted execvpe: %s\n", file);
  return exec_worker(file, argv, /*file_or_path*/ 1, /*has_envp*/ 1, envp);
}

// TODO: execlpe
// TODO: posix_spawn, posix_spawnp
