// test_target.cpp — minimal VST2 synth used only by host_test.exe.
// Logs every effProcessEvents call ("B" marker) and its MIDI events
// ("EV <deltaFrames> <status-hex> <d1> <d2>") to test_target_events.log
// in the host's working directory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "aeffectx.h"

static FILE* g_log = nullptr;

static void copyStr(char* dst, const char* src, int maxLen) {
  if (!dst) return;
  strncpy(dst, src, maxLen - 1);
  dst[maxLen - 1] = 0;
}

static VstIntPtr VSTCALLBACK tdDispatcher(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                          VstIntPtr value, void* ptr, float opt) {
  switch (opcode) {
    case effProcessEvents: {
      if (g_log) {
        fputs("B\n", g_log);
        VstEvents* ev = (VstEvents*)ptr;
        if (ev) for (int i = 0; i < ev->numEvents; ++i) {
          VstEvent* e = ev->events[i];
          if (e && e->type == kVstMidiType) {
            VstMidiEvent* m = (VstMidiEvent*)e;
            fprintf(g_log, "EV %d %02X %d %d\n", e->deltaFrames,
                    (unsigned char)m->midiData[0],
                    (unsigned char)m->midiData[1],
                    (unsigned char)m->midiData[2]);
          }
        }
        fflush(g_log);
      }
      return 0;
    }
    case effGetVstVersion: return 2400;
    case effGetVendorString:  copyStr((char*)ptr, "TestTarget", kVstMaxVendorStrLen); return 0;
    case effGetProductString: copyStr((char*)ptr, "TestTarget", kVstMaxProductStrLen); return 0;
    case effGetEffectName:    copyStr((char*)ptr, "TestTarget", kVstMaxEffectNameLen); return 0;
    case effGetParamName:     copyStr((char*)ptr, "P", kVstMaxParamStrLen); return 0;
    case effClose:
      if (g_log) { fclose(g_log); g_log = nullptr; }
      delete effect;
      return 0;
    default: return 0;
  }
}

static void  VSTCALLBACK tdSetParam(AEffect*, VstInt32, float) {}
static float VSTCALLBACK tdGetParam(AEffect*, VstInt32) { return 0.f; }
static void  VSTCALLBACK tdProcessReplace(AEffect*, float**, float**, VstInt32) {}

extern "C" __declspec(dllexport) AEffect* VSTPluginMain(audioMasterCallback hostCb) {
  g_log = fopen("test_target_events.log", "w");
  AEffect* e = new AEffect();
  memset(e, 0, sizeof(AEffect));
  e->magic = kEffectMagic;
  e->dispatcher = tdDispatcher;
  e->setParameter = tdSetParam;
  e->getParameter = tdGetParam;
  e->numPrograms = 1;
  e->numParams = 3;
  e->numInputs = 0;
  e->numOutputs = 2;
  e->flags = effFlagsIsSynth | effFlagsCanReplacing;
  e->uniqueID = CCONST('T','T','s','t');
  e->version = 1;
  e->processReplacing = tdProcessReplace;
  hostCb(e, audioMasterVersion, 0, 0, 0, 0);
  return e;
}
// (no legacy 'main' export: C++ reserves it)
