// openlog.c — DYLD_INSERT_LIBRARIES shim: logs every open() with result fd.
// Lets us see which files the plugin tries to open and which fail (-1).
#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>

typedef int (*open_fn)(const char*, int, ...);
static open_fn real_open = NULL;

__attribute__((constructor)) static void init(void) {
  real_open = (open_fn)dlsym(RTLD_NEXT, "open");
}

int open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
  int fd = real_open ? real_open(path, flags, mode) : -1;
  if (path) fprintf(stderr, "[openlog] %s -> %d\n", path, fd);
  return fd;
}
