// verify_installed.cpp — load the INSTALLED proxy at C:\Program Files\VSTPlugins
// (it resolves its target via sustainhold.ini next to itself) and run the
// pedal scenario end-to-end.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "aeffectx.h"

static const int kBlock = 512;
static VstTimeInfo g_ti;

static VstIntPtr VSTCALLBACK masterCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) {
  switch (opcode) {
    case audioMasterVersion:       return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return kBlock;
    case audioMasterGetTime:       return (VstIntPtr)&g_ti;
    case 6: return 1; // WantMidi
    default: return 0;
  }
}

struct SendBuf { VstEvents head; VstEvent* extra[32]; };
static void sendBlock(AEffect* fx, int n, const unsigned char evs[][4]) {
  static VstMidiEvent store[32];
  static SendBuf buf;
  VstEvent** p = &buf.head.events[0];
  for (int i = 0; i < n; ++i) {
    memset(&store[i], 0, sizeof(store[i]));
    store[i].type = kVstMidiType;
    store[i].byteSize = (VstInt32)sizeof(VstMidiEvent);
    store[i].deltaFrames = 0;
    memcpy(store[i].midiData, evs[i], 3);
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
  for (int ch = 0; ch < fx->numOutputs; ++ch) memset(outs[ch], 0, kBlock * sizeof(float));
  fx->processReplacing(fx, ins, outs, kBlock);
  for (int i = 0; i < kBlock; ++i) rms += outs[0][i] * outs[0][i];
  return sqrt(rms / kBlock);
}

int main() {
  memset(&g_ti, 0, sizeof(g_ti));
  g_ti.sampleRate = 44100; g_ti.tempo = 120;
  g_ti.timeSigNumerator = 4; g_ti.timeSigDenominator = 4;
  g_ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid | kVstTransportPlaying;

  HMODULE h = LoadLibraryA("C:\\Program Files\\VSTPlugins\\TAL-U-No-62.dll");
  if (!h) { printf("FATAL: LoadLibrary failed (%lu)\n", GetLastError()); return 2; }
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn fn = (MainFn)GetProcAddress(h, "VSTPluginMain");
  AEffect* fx = fn ? fn(masterCb) : nullptr;
  if (!fx) { printf("FATAL: instantiation failed\n"); return 2; }
  printf("installed proxy: params=%d progs=%d outs=%d id=%08X\n",
         fx->numParams, fx->numPrograms, fx->numOutputs, (unsigned)fx->uniqueID);

  fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 44100.0f);
  fx->dispatcher(fx, effSetBlockSize, 0, kBlock, 0, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);

  static float out0[kBlock], out1[kBlock];
  float* outs[2] = { out0, out1 };

  const unsigned char down[][4] = {{0xB0,64,127,0},{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  sendBlock(fx, 4, down);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char offs[][4] = {{0x80,60,0,0},{0x80,64,0,0},{0x80,67,0,0}};
  sendBlock(fx, 3, offs);
  double held = 0;
  for (int i = 0; i < 16; ++i) held += render(fx, outs);
  const unsigned char rel[][4] = {{0xB0,64,0,0}};
  sendBlock(fx, 1, rel);
  for (int i = 0; i < 32; ++i) render(fx, outs);
  double after = 0;
  for (int i = 0; i < 64; ++i) after += render(fx, outs);

  printf("RMS held (keys up, pedal down): %.6f\n", held / 16);
  printf("RMS 0.74s after pedal up:       %.6f\n", after / 64);
  bool ok = held / 16 > 1e-4 && after / 64 < held / 16 * 0.5;
  fx->dispatcher(fx, effMainsChanged, 0, 0, 0, 0);
  fx->dispatcher(fx, effClose, 0, 0, 0, 0);
  printf("%s\n", ok ? "INSTALLED VERIFICATION PASS" : "INSTALLED VERIFICATION FAIL");
  return ok ? 0 : 1;
}
