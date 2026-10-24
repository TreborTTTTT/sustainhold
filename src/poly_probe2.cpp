// poly_probe2.cpp — how many simultaneous voices does the real TAL-U-No-62 produce?
#include "aeffectx.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <windows.h>

static const int kBlock = 512;
static VstTimeInfo g_ti;
static VstIntPtr VSTCALLBACK masterCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) {
  switch (opcode) {
    case audioMasterVersion:       return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return kBlock;
    case audioMasterGetTime:       return (VstIntPtr)&g_ti;
    case 6: /*WantMidi*/           return 1;
    default: return 0;
  }
}

struct SendBuf { VstEvents head; VstEvent* extra[32]; };
static void sendNotes(AEffect* fx, int n, unsigned char st, const int* pitches) {
  static VstMidiEvent store[32];
  static SendBuf buf;
  VstEvent** p = &buf.head.events[0];
  for (int i = 0; i < n; ++i) {
    memset(&store[i], 0, sizeof(store[i]));
    store[i].type = kVstMidiType;
    store[i].byteSize = (VstInt32)sizeof(VstMidiEvent);
    store[i].deltaFrames = i;
    store[i].midiData[0] = (char)st;
    store[i].midiData[1] = (char)pitches[i];
    store[i].midiData[2] = (char)(st == 0x90 ? 100 : 0);
    p[i] = (VstEvent*)&store[i];
  }
  buf.head.numEvents = n;
  buf.head.reserved = 0;
  fx->dispatcher(fx, effProcessEvents, 0, 0, &buf.head, 0.f);
}

static double render(AEffect* fx, float** outs) {
  static float in0[kBlock], in1[kBlock];
  float* ins[2] = { in0, in1 };
  double rms = 0;
  fx->processReplacing(fx, ins, outs, kBlock);
  for (int i = 0; i < kBlock; ++i) rms += outs[0][i] * outs[0][i];
  return sqrt(rms / kBlock);
}

int main() {
  memset(&g_ti, 0, sizeof(g_ti));
  g_ti.samplePos = 0; g_ti.sampleRate = 44100; g_ti.ppqPos = 0;
  g_ti.tempo = 120; g_ti.barStartPos = 0;
  g_ti.timeSigNumerator = 4; g_ti.timeSigDenominator = 4;
  g_ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid | kVstTransportPlaying;

  HMODULE h = LoadLibraryA("C:\\Program Files\\VSTPlugins\\TAL-U-No-62-original\\TAL-U-No-62-original.dll");
  if (!h) { printf("no target\n"); return 1; }
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn mf = (MainFn)GetProcAddress(h, "VSTPluginMain");
  AEffect* fx = mf ? mf(masterCb) : nullptr;
  if (!fx) { printf("no effect\n"); return 1; }
  fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 44100.0f);
  fx->dispatcher(fx, effSetBlockSize, 0, kBlock, nullptr, 0.f);
  fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0.f);

  float* obuf[2] = { new float[kBlock], new float[kBlock] };
  float* outs[2] = { obuf[0], obuf[1] };

  int pitches[24];
  for (int i = 0; i < 24; ++i) pitches[i] = 48 + i * 2; // spread apart

  printf("n   RMS       RMS/sqrt(n)  RMS/n\n");
  for (int n = 1; n <= 24; n += (n < 9 ? 1 : 3)) {
    sendNotes(fx, n, 0x80, pitches);              // silence leftovers
    for (int b = 0; b < 64; ++b) render(fx, outs);
    sendNotes(fx, n, 0x90, pitches);              // n simultaneous notes
    double rms = 0;
    for (int b = 0; b < 32; ++b) rms += render(fx, outs);
    rms /= 32;
    printf("%2d  %.6f  %.6f  %.6f\n", n, rms, rms / sqrt((double)n), rms / n);
    sendNotes(fx, n, 0x80, pitches);
  }
  return 0;
}
