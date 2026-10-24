// macOS VST2 hosts look up the legacy 'main' symbol. C/C++ refuse a non-int
// main, so export it as an assembly tail-jump alias of VSTPluginMain.
#if defined(__x86_64__)
    .global _main
_main:
    jmp _VSTPluginMain
#elif defined(__arm64__) || defined(__aarch64__)
    .global _main
_main:
    b _VSTPluginMain
#endif
