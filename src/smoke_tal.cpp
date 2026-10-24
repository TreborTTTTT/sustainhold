// smoke_tal.cpp — loads proxy.dll pointed at a COPY of the real TAL-U-No-62
// and verifies: instantiation, MIDI through the sustain engine, audible
// audio output, pedal behavior, clean close. No assertions on exact events.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "aeffectx.h"

static const int kBlock = 512;

static VstTimeInfo g_ti;
static VstIntPtr VSTCALLBACK masterCb(AEffect* e, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) {
  switch (opcode) {
    case audioMasterVersion:       return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return kBlock;
    case audioMasterGetTime:       return (VstIntPtr)&g_ti;
    case 6: /*WantMidi*/            return 1;
    default: return 0;
  }
}

struct SendBuf { VstEvents head; VstEvent* extra[32]; };
static void sendBlock(AEffect* fx, int n, const unsigned char evs[][4], const int* frames) {
  static VstMidiEvent store[32];
  static SendBuf buf;
  VstEvent** p = &buf.head.events[0];
  for (int i = 0; i < n; ++i) {
    memset(&store[i], 0, sizeof(store[i]));
    store[i].type = kVstMidiType;
    store[i].byteSize = (VstInt32)sizeof(VstMidiEvent);
    store[i].deltaFrames = frames[i];
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
  g_ti.samplePos = 0; g_ti.sampleRate = 44100; g_ti.ppqPos = 0;
  g_ti.tempo = 120; g_ti.barStartPos = 0;
  g_ti.timeSigNumerator = 4; g_ti.timeSigDenominator = 4;
  g_ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid |
               kVstTransportPlaying;

  FILE* ini = fopen("sustainhold.ini", "w");
  fputs("[sustainhold]\ntarget=tal_real.dll\n", ini);
  fclose(ini);

  HMODULE h = LoadLibraryA("proxy.dll");
  if (!h) { printf("FATAL: proxy.dll load failed (%lu)\n", GetLastError()); return 2; }
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn fn = (MainFn)GetProcAddress(h, "VSTPluginMain");
  AEffect* fx = fn ? fn(masterCb) : nullptr;
  if (!fx) { printf("FATAL: TAL instantiation through proxy failed\n"); return 2; }

  printf("TAL through proxy: magic=%c%c%c%c params=%d progs=%d outs=%d id=%08X\n",
         fx->magic & 0xFF, (fx->magic >> 8) & 0xFF, (fx->magic >> 16) & 0xFF, (fx->magic >> 24) & 0xFF,
         fx->numParams, fx->numPrograms, fx->numOutputs, (unsigned)fx->uniqueID);

  fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 44100.0f);
  fx->dispatcher(fx, effSetBlockSize, 0, kBlock, 0, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);

  static float in0[kBlock], in1[kBlock];
  static float out0[kBlock], out1[kBlock];
  float* ins[2]  = { in0, in1 };
  float* outs[2] = { out0, out1 };
  if (fx->numOutputs < 1) { printf("FATAL: no outputs\n"); return 2; }

  // hold a chord with pedal, then release pedal: sound must keep going after
  // note-offs while pedal down, and stop ~release+release-time after pedal up
  int fr[4] = {0, 0, 0, 0};
  const unsigned char down[][4] = {{0xB0,64,127,0},{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  sendBlock(fx, 4, down, fr);                       // pedal + chord
  for (int i = 0; i < 8; ++i) render(fx, outs);     // settle
  const unsigned char offs[][4] = {{0x80,60,0,0},{0x80,64,0,0},{0x80,67,0,0}};
  int fr3[3] = {0,0,0};
  sendBlock(fx, 3, offs, fr3);                      // keys up, pedal still down
  double rms_held = 0;
  for (int i = 0; i < 16; ++i) rms_held += render(fx, outs);
  const unsigned char rel[][4] = {{0xB0,64,0,0}};
  sendBlock(fx, 1, rel, fr);                        // pedal up -> notes off
  for (int i = 0; i < 32; ++i) render(fx, outs);    // 32*512/44100 = 0.37s tail
  double rms_after = 0;
  for (int i = 0; i < 64; ++i) rms_after += render(fx, outs);  // 0.74s later

  printf("RMS while pedal-held after keys up: %.6f\n", rms_held / 16);
  printf("RMS 0.74s after pedal release:     %.6f\n", rms_after / 64);

  // cycle 2: release near block end, then NO further MIDI at all —
  // trailing note-offs must be pumped by the audio path (stuck-note bug)
  const unsigned char down2[][4] = {{0xB0,64,127,0},{0x90,55,100,0},{0x90,59,100,0},{0x90,62,100,0}};
  sendBlock(fx, 4, down2, fr);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char offs2[][4] = {{0x80,55,0,0},{0x80,59,0,0},{0x80,62,0,0}};
  sendBlock(fx, 3, offs2, fr);
  const unsigned char rel2[][4] = {{0xB0,64,0,0}};
  int frel[1] = {500};
  sendBlock(fx, 1, rel2, frel);                        // release @500: 1 drains, 2 carry over
  for (int i = 0; i < 32; ++i) render(fx, outs);       // pump must deliver the rest
  double rms2_after = 0;
  for (int i = 0; i < 64; ++i) rms2_after += render(fx, outs);
  printf("RMS cycle2, 0.74s after block-end release: %.6f\n", rms2_after / 64);

  bool ok = rms_held / 16 > 1e-4 && rms_after / 64 < rms_held / 16 * 0.5
            && rms2_after / 64 < rms_held / 16 * 0.5;
  fx->dispatcher(fx, effMainsChanged, 0, 0, 0, 0);
  fx->dispatcher(fx, effClose, 0, 0, 0, 0);
  printf("%s\n", ok ? "SMOKE PASS" : "SMOKE FAIL");
  return ok ? 0 : 1;
}
