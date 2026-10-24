// poly_probe.cpp — how many simultaneous voices does the real TAL-U-No-62 produce?
#include "aeffectx.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <windows.h>

static HMODULE g_target = nullptr;
typedef AEffect* (*mainFactory_t)(int, audioMasterCallback, VstInt32);

static VstIntPtr SD(AEffect* e, int op, int v, long val, void* ptr, float opt) {
  return e->dispatcher(e, op, v, val, ptr, opt);
}

static VstTimeInfo g_ti;
static VstIntPtr master(AEffect*, int op, int, long, void*, float) {
  if (op == audioMasterGetTime) return (VstIntPtr)&g_ti;
  if (op == audioMasterVersion) return 2400;
  return 0;
}

static void sendOns(AEffect* fx, const int* pitches, int n) {
  static VstMidiEvent evs[32];
  static VstEvent* ptrs[32];
  VstEvents ev; ev.numEvents = n; ev.flags = 0;
  for (int i = 0; i < n; ++i) {
    memset(&evs[i], 0, sizeof(evs[i]));
    evs[i].type = kVstMidiType; evs[i].byteSize = sizeof(VstMidiEvent);
    evs[i].deltaFrames = i; evs[i].noteLength = 0; evs[i].noteOffVelocity = 0;
    evs[i].midiData[0] = (char)0x90; evs[i].midiData[1] = (char)pitches[i]; evs[i].midiData[2] = 100;
    ptrs[i] = (VstEvent*)&evs[i];
  }
  ev.events = ptrs;
  SD(fx, effProcessEvents, 0, 0, &ev, 0);
}

int main() {
  memset(&g_ti, 0, sizeof(g_ti));
  g_ti.sampleRate = 44100; g_ti.flags = 1;

  g_target = LoadLibraryA("C:\\Program Files\\VSTPlugins\\TAL-U-No-62-original\\TAL-U-No-62-original.dll");
  if (!g_target) { printf("no target\n"); return 1; }
  mainFactory_t mf = (mainFactory_t)GetProcAddress(g_target, "VSTPluginMain");
  AEffect* fx = mf(0x56535470, master, 0);
  if (!fx) { printf("no effect\n"); return 1; }
  SD(fx, effSetSampleRate, 0, 0, nullptr, 44100.f);
  SD(fx, effSetBlockSize, 0, 512, nullptr, 0);
  SD(fx, effMainsChanged, 0, 1, nullptr, 0);

  float** outs = new float*[fx->numOutputs];
  for (int i = 0; i < fx->numOutputs; ++i) outs[i] = new float[512];

  int pitches[24];
  for (int i = 0; i < 24; ++i) pitches[i] = 48 + i; // spread apart, no unison overlap

  for (int n = 1; n <= 24; n += (n < 8 ? 1 : 4)) {
    // reset
    int offs[24]; for (int i = 0; i < 24; ++i) offs[i] = pitches[i];
    sendOns(fx, offs, 0); // no-op keep compiler happy
    // all notes off via dispatcher if supported, else explicit offs
    {
      static VstMidiEvent oe[24]; static VstEvent* op[24];
      VstEvents ev; ev.numEvents = n; ev.flags = 0;
      for (int i = 0; i < n; ++i) {
        memset(&oe[i], 0, sizeof(oe[i]));
        oe[i].type = kVstMidiType; oe[i].byteSize = sizeof(VstMidiEvent); oe[i].deltaFrames = i;
        oe[i].midiData[0] = (char)0x80; oe[i].midiData[1] = (char)pitches[i];
        op[i] = (VstEvent*)&oe[i];
      }
      ev.events = op; SD(fx, effProcessEvents, 0, 0, &ev, 0);
    }
    for (int b = 0; b < 64; ++b) fx->processReplacing(fx, nullptr, outs, 512);

    sendOns(fx, pitches, n);
    double rms = 0;
    for (int b = 0; b < 32; ++b) {
      fx->processReplacing(fx, nullptr, outs, 512);
      for (int i = 0; i < 512; ++i) rms += (double)outs[0][i] * outs[0][i];
    }
    rms = sqrt(rms / (32.0 * 512));
    printf("n=%2d  RMS=%.6f  (per-voice ~%.6f)\n", n, rms, rms / sqrt(n));
  }
  return 0;
}
