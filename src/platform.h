// platform.h — thin shim so the proxy + test harness compile on both
// Windows (DLL) and macOS (.vst bundle / dylib). Module loading, exports,
// self-directory and config parsing are the only platform-dependent parts.
#pragma once
#include <string>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE ModHandle;
#define SH_EXPORT extern "C" __declspec(dllexport)
static inline ModHandle modLoad(const char* p) { return LoadLibraryA(p); }
static inline void*     modSym(ModHandle h, const char* s) { return (void*)GetProcAddress(h, s); }
static inline void      modFree(ModHandle h) { if (h) FreeLibrary(h); }
static inline const char* modDefaultName() { return "proxy.dll"; }
#else
#include <dlfcn.h>
typedef void* ModHandle;
#define SH_EXPORT extern "C" __attribute__((visibility("default")))
static inline ModHandle modLoad(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static inline void*     modSym(ModHandle h, const char* s) { return dlsym(h, s); }
static inline void      modFree(ModHandle h) { if (h) dlclose(h); }
static inline const char* modDefaultName() { return "./proxy.dylib"; }
#endif

// directory containing this code's own module (proxy.dll / proxy .vst Mach-O)
static inline std::string modSelfDir() {
#ifdef _WIN32
  HMODULE mod = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     (LPCSTR)&modSelfDir, &mod);
  char path[MAX_PATH];
  GetModuleFileNameA(mod, path, MAX_PATH);
  std::string s(path);
#else
  Dl_info info;
  if (!dladdr((const void*)&modSelfDir, &info) || !info.dli_fname) return ".";
  std::string s(info.dli_fname);
#endif
  size_t p = s.find_last_of("/\\");
  return p == std::string::npos ? std::string(".") : s.substr(0, p);
}

// portable ini: [sustainhold] key=value lines, '#'/';' comments
struct IniVals {
  bool  hasTarget = false; std::string target;
  int   forward_cc64 = 0;
  int   max_voices   = 6;
  int   spacing_ms10 = 15;
  int   log          = 0;
};
static inline bool readIniPortable(const std::string& path, IniVals& v) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  char line[1024];
  bool inSection = false;
  while (fgets(line, sizeof(line), f)) {
    char* s = line;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '#' || *s == ';' || *s == '\n' || *s == '\r' || !*s) continue;
    if (*s == '[') { inSection = (strncmp(s, "[sustainhold]", 13) == 0); continue; }
    if (!inSection) continue;
    char* eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char* key = s;  while (*key && (key[strlen(key)-1]==' '||key[strlen(key)-1]=='\t'||key[strlen(key)-1]=='\r'||key[strlen(key)-1]=='\n')) key[strlen(key)-1]=0;
    char* val = eq + 1;
    while (*val == ' ' || *val == '\t') ++val;
    char* e = val + strlen(val);
    while (e > val && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e = 0;
    if      (!strcmp(key, "target"))       { v.hasTarget = true; v.target = val; }
    else if (!strcmp(key, "forward_cc64")) v.forward_cc64 = atoi(val);
    else if (!strcmp(key, "max_voices"))   v.max_voices   = atoi(val);
    else if (!strcmp(key, "spacing_ms10")) v.spacing_ms10 = atoi(val);
    else if (!strcmp(key, "log"))          v.log          = atoi(val);
  }
  fclose(f);
  return true;
}
