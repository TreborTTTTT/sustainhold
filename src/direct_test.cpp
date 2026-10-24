// direct_test.cpp — load the target plugin with NO proxy; open, render, close.
// Isolates proxy bugs from plugin/host-environment issues. Target path comes
// from SUSTAINHOLD_TARGET (absolute), default tal_real.dll/.dylib next to cwd.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "aeffectx.h"
#include "platform.h"

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
  const char* path = getenv("SUSTAINHOLD_TARGET");
  if (!path || !*path)
#ifdef _WIN32
    path = "tal_real.dll";
#else
    path = "tal_real.dylib";
#endif
  ModHandle h = modLoad(path);
  if (!h) { printf("modLoad fail: %s\n", path); return 2; }
  typedef AEffect* (*F)(audioMasterCallback);
  F f = (F)modSym(h, "VSTPluginMain");
  if (!f) f = (F)modSym(h, "main");
  AEffect* e = f ? f(mc) : nullptr;
  if (!e) { printf("factory null\n"); return 2; }
  printf("AEffect ok params=%d progs=%d ins=%d outs=%d flags=%08X\n",
         e->numParams, e->numPrograms, e->numInputs, e->numOutputs, (unsigned)e->flags);
  e->dispatcher(e, effOpen, 0, 0, 0, 0);
  printf("open ok\n"); fflush(stdout);
  e->dispatcher(e, effSetSampleRate, 0, 44100, 0, 44100.0f);
  e->dispatcher(e, effSetBlockSize, 0, 512, 0, 0);
  e->dispatcher(e, effMainsChanged, 0, 1, 0, 0);
  e->dispatcher(e, effStartProcess, 0, 0, 0, 0);
  printf("setup ok\n"); fflush(stdout);

  // dump ALL param names/values to see whether defaults are initialized
  for (int p = 0; p < e->numParams; ++p) {
    char nm[64] = {0}, disp[64] = {0};
    e->dispatcher(e, effGetParamName, p, 0, nm, 0);
    e->dispatcher(e, effGetParamDisplay, p, 0, disp, 0);
    printf("  param %d '%s' = '%s' (raw %.3f)\n", p, nm, disp, e->getParameter(e, p));
  }
  fflush(stdout);

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
