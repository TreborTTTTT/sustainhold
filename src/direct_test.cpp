// direct_test.cpp — load tal_real.dll with no proxy; open, render, close.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>
#include "aeffectx.h"

static VstTimeInfo g_ti;
static void initTime() {
  memset(&g_ti, 0, sizeof(g_ti));
  g_ti.samplePos = 0; g_ti.sampleRate = 44100; g_ti.ppqPos = 0;
  g_ti.tempo = 120; g_ti.barStartPos = 0;
  g_ti.timeSigNumerator = 4; g_ti.timeSigDenominator = 4;
  g_ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid |
               kVstTransportPlaying;
}

static VstIntPtr VSTCALLBACK mc(AEffect*, VstInt32 o, VstInt32 idx, VstIntPtr v, void* p, float opt) {
  printf("  masterCb op=%d idx=%d val=%lld ptr=%p opt=%g\n", o, idx, (long long)v, p, opt);
  fflush(stdout);
  switch (o) {
    case audioMasterGetTime:       return (VstIntPtr)&g_ti;
    case 6: /*WantMidi*/            return 1;
    case audioMasterVersion:       return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return 512;
    default: return 0;
  }
}

int main() {
  initTime();
  HMODULE h = LoadLibraryA("tal_real.dll");
  if (!h) { printf("LoadLibrary fail %lu\n", GetLastError()); return 2; }
  typedef AEffect* (*F)(audioMasterCallback);
  F f = (F)GetProcAddress(h, "VSTPluginMain");
  AEffect* e = f ? f(mc) : nullptr;
  if (!e) { printf("factory null\n"); return 2; }
  printf("AEffect ok params=%d progs=%d ins=%d outs=%d flags=%08X\n",
         e->numParams, e->numPrograms, e->numInputs, e->numOutputs, (unsigned)e->flags);
  e->dispatcher(e, effOpen, 0, 0, 0, 0);
  printf("open ok\n"); fflush(stdout);
  e->dispatcher(e, effSetSampleRate, 0, 0, 0, 44100.0f);
  e->dispatcher(e, effSetBlockSize, 0, 512, 0, 0);
  e->dispatcher(e, effMainsChanged, 0, 1, 0, 0);
  printf("setup ok\n"); fflush(stdout);

  static float in0[512], in1[512], out0[512], out1[512];
  float* ins[2] = { in0, in1 };
  float* outs[2] = { out0, out1 };

  // send one note-on directly (same struct layout the proxy builds)
  {
    static VstMidiEvent m;   // TAL may store the event pointer: keep it alive
    memset(&m, 0, sizeof(m));
    m.type = kVstMidiType;
    m.byteSize = (VstInt32)sizeof(VstMidiEvent);
    m.deltaFrames = 0;
    m.midiData[0] = (char)0x90; m.midiData[1] = 60; m.midiData[2] = 100;
    VstEvents ev; ev.numEvents = 1; ev.reserved = 0; ev.events[0] = (VstEvent*)&m;
    e->dispatcher(e, effProcessEvents, 0, 0, &ev, 0.f);
    printf("midi sent ok\n"); fflush(stdout);
  }

  double rms = 0;
  for (int b = 0; b < 10; ++b) {
    e->processReplacing(e, ins, outs, 512);
    for (int i = 0; i < 512; ++i) rms += out0[i] * out0[i];
  }
  printf("render ok rms=%.6f\n", sqrt(rms / 5120)); fflush(stdout);
  e->dispatcher(e, effClose, 0, 0, 0, 0);
  printf("close ok\n");
  return 0;
}
