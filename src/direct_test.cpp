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

static char g_dir[1024] = ".";  // absolute dir of the target plugin (audioMasterGetDirectory)

static void setDirFrom(const char* path) {
  char full[1024];
#ifdef _WIN32
  if (!_fullpath(full, path, sizeof(full))) return;
#else
  if (!realpath(path, full)) return;
#endif
  char* slash = strrchr(full, '/');
  if (!slash) return;
  *slash = 0;
  snprintf(g_dir, sizeof(g_dir), "%s", full);
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
    case audioMasterGetDirectory:  return (VstIntPtr)(const char*)g_dir;
    case audioMasterCanDo:         return 1;
    case audioMasterGetCurrentProcessLevel: return 1; /*realtime*/
    case audioMasterProcessEvents: return 1;
    default: return 0;
  }
}

int main() {
  initTime();
  static float in0[512], in1[512], out0[512], out1[512];
  float* ins[2] = { in0, in1 };
  float* outs[2] = { out0, out1 };
  const char* path = getenv("SUSTAINHOLD_TARGET");
  if (!path || !*path)
#ifdef _WIN32
    path = "tal_real.dll";
#else
    path = "tal_real.dylib";
#endif
  ModHandle h = modLoad(path);
  if (!h) { printf("modLoad fail: %s\n", path); return 2; }
  setDirFrom(path);
  printf("dir='%s'\n", g_dir); fflush(stdout);
  typedef AEffect* (*F)(audioMasterCallback);
  F f = (F)modSym(h, "VSTPluginMain");
  if (!f) f = (F)modSym(h, "main");
  AEffect* e = f ? f(mc) : nullptr;
  if (!e) { printf("factory null\n"); return 2; }
  printf("AEffect ok params=%d progs=%d ins=%d outs=%d flags=%08X\n",
         e->numParams, e->numPrograms, e->numInputs, e->numOutputs, (unsigned)e->flags);
  e->dispatcher(e, effOpen, 0, 0, 0, 0);
  printf("open ok\n"); fflush(stdout);
  e->dispatcher(e, effSetProgram, 0, 0, 0, 0);
  e->dispatcher(e, effSetSampleRate, 0, 44100, 0, 44100.0f);
  e->dispatcher(e, effSetBlockSize, 0, 512, 0, 0);
  e->dispatcher(e, 42 /*effConnectOutput*/, 0, 0, out0, 0);
  e->dispatcher(e, 42 /*effConnectOutput*/, 1, 0, out1, 0);
  e->dispatcher(e, 54 /*effSetBypass*/, 0, 0, 0, 0);
  e->dispatcher(e, 87 /*effSetProcessPrecision*/, 0, 0 /*float32*/, 0, 0);
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

  double rms = 0, rms1 = 0;
  for (int b = 0; b < 10; ++b) {
    e->processReplacing(e, ins, outs, 512);
    g_ti.samplePos += 512;
    for (int i = 0; i < 512; ++i) { rms += out0[i] * out0[i]; rms1 += out1[i] * out1[i]; }
  }
  printf("render ok rms=%.6f rmsR=%.6f\n", sqrt(rms / 5120), sqrt(rms1 / 5120)); fflush(stdout);

  // ---- diagnostic sweep: force key params to audible values, re-render ----
  for (int p = 0; p < e->numParams; ++p) {
    char nm[64] = {0};
    e->dispatcher(e, effGetParamName, p, 0, nm, 0);
    if (!strcmp(nm, "master volume"))  e->setParameter(e, p, 0.8f);
    if (!strcmp(nm, "attack"))         e->setParameter(e, p, 0.05f);
    if (!strcmp(nm, "decay"))          e->setParameter(e, p, 0.5f);
    if (!strcmp(nm, "sustain"))        e->setParameter(e, p, 0.8f);
    if (!strcmp(nm, "release"))        e->setParameter(e, p, 0.3f);
    if (!strcmp(nm, "filter cutoff"))  e->setParameter(e, p, 0.8f);
    if (!strcmp(nm, "dco saw enabled"))e->setParameter(e, p, 1.0f);
    if (!strcmp(nm, "dco pulse enabled")) e->setParameter(e, p, 1.0f);
    if (!strcmp(nm, "vca mode"))       e->setParameter(e, p, 0.0f);
  }
  {
    static VstMidiEvent m2;
    memset(&m2, 0, sizeof(m2));
    m2.type = kVstMidiType; m2.byteSize = (VstInt32)sizeof(VstMidiEvent);
    m2.midiData[0] = (char)0x80; m2.midiData[1] = 60; m2.midiData[2] = 0;  // off first
    VstEvents ev0; ev0.numEvents = 1; ev0.reserved = 0; ev0.events[0] = (VstEvent*)&m2;
    e->dispatcher(e, effProcessEvents, 0, 0, &ev0, 0.f);
    m2.midiData[0] = (char)0x90; m2.midiData[2] = 127;                      // then on
    e->dispatcher(e, effProcessEvents, 0, 0, &ev0, 0.f);
  }
  double rms2 = 0;
  for (int b = 0; b < 10; ++b) {
    e->processReplacing(e, ins, outs, 512);
    g_ti.samplePos += 512;
    for (int i = 0; i < 512; ++i) rms2 += out0[i] * out0[i];
  }
  printf("sweep rms=%.6f\n", sqrt(rms2 / 5120)); fflush(stdout);
  e->dispatcher(e, effClose, 0, 0, 0, 0);
  printf("close ok\n");
  return 0;
}
