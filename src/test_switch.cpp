// test_switch.cpp — reproduce "fast chord switch kills notes" on the real TAL.
// Pedal down: chord1 ON -> chord1 OFF (queued) -> chord2 ON (same pitches).
// The engine emits OFF+ON at the same frame for shared pitches. Measure RMS
// after chord2: silence = bug reproduced.
#include <cstdio>
#include <cstring>
#include <cmath>
#include "aeffectx.h"
#include "platform.h"

static const int kBlock = 512;
static VstTimeInfo g_ti;

static VstIntPtr VSTCALLBACK masterCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) {
  switch (opcode) {
    case audioMasterVersion:       return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return kBlock;
    case audioMasterGetTime:       return (VstIntPtr)&g_ti;
    case 6: return 1;
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

static float g_cap[32 * 512];
static int g_ncap = 0;
static double render(AEffect* fx, float** outs) {
  static float in0[kBlock], in1[kBlock];
  float* ins[2] = { in0, in1 };
  double rms = 0;
  for (int ch = 0; ch < fx->numOutputs; ++ch) memset(outs[ch], 0, kBlock * sizeof(float));
  fx->processReplacing(fx, ins, outs, kBlock);
  for (int i = 0; i < kBlock; ++i) {
    rms += outs[0][i] * outs[0][i];
    if (g_ncap < (int)(32 * 512)) g_cap[g_ncap++] = outs[0][i];
  }
  return sqrt(rms / kBlock);
}

// amplitude of a single frequency component over the captured window
static double goertzel(double freq) {
  double w = 2.0 * 3.14159265358979 * freq / 44100.0, re = 0, im = 0;
  for (int i = 0; i < g_ncap; ++i) { re += g_cap[i] * cos(w * i); im -= g_cap[i] * sin(w * i); }
  return 2.0 * sqrt(re * re + im * im) / g_ncap;
}

int main() {
  memset(&g_ti, 0, sizeof(g_ti));
  g_ti.sampleRate = 44100; g_ti.tempo = 120;
  g_ti.timeSigNumerator = 4; g_ti.timeSigDenominator = 4;
  g_ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid | kVstTransportPlaying;

  FILE* ini = fopen("sustainhold.ini", "w");
  const char* tgt = getenv("SUSTAINHOLD_TARGET");
  if (!tgt || !*tgt)
#ifdef _WIN32
    tgt = "tal_real.dll";
#else
    tgt = "tal_real.dylib";
#endif
  fprintf(ini, "[sustainhold]\ntarget=%s\n", tgt);
  fclose(ini);

  ModHandle h = modLoad(modDefaultName());
  if (!h) { printf("FATAL proxy\n"); return 2; }
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn fn = (MainFn)modSym(h, "VSTPluginMain");
  AEffect* fx = fn ? fn(masterCb) : nullptr;
  if (!fx) { printf("FATAL factory\n"); return 2; }
  fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 44100.0f);
  fx->dispatcher(fx, effSetBlockSize, 0, kBlock, 0, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);

  static float out0[kBlock], out1[kBlock];
  float* outs[2] = { out0, out1 };
  int fr0[1] = {0};

  // --- case 1: pedal DOWN, switch chord1(C E G) -> chord2(same C E G) ---
  const unsigned char d[][4] = {{0xB0,64,127,0}};
  sendBlock(fx, 1, d, fr0);
  const unsigned char c1[][4] = {{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  int fr3[3] = {0,0,0};
  sendBlock(fx, 3, c1, fr3);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char c1off[][4] = {{0x80,60,0,0},{0x80,64,0,0},{0x80,67,0,0}};
  sendBlock(fx, 3, c1off, fr3);                       // queued (pedal down)
  const unsigned char c2[][4] = {{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  sendBlock(fx, 3, c2, fr3);                          // retrigger: OFF+ON same frame
  double r1 = 0;
  for (int i = 0; i < 16; ++i) r1 += render(fx, outs);
  r1 /= 16;
  printf("case1 pedal-down chord switch, same pitches:   RMS=%.6f\n", r1);

  // release all, reset
  const unsigned char alloff[][4] = {{0x80,60,0,0},{0x80,64,0,0},{0x80,67,0,0},{0xB0,64,0,0}};
  sendBlock(fx, 4, alloff, fr3);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 2: pedal DOWN, switch to NON-overlapping chord (control) ---
  sendBlock(fx, 1, d, fr0);
  sendBlock(fx, 3, c1, fr3);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 3, c1off, fr3);
  const unsigned char c2b[][4] = {{0x90,69,100,0},{0x90,72,100,0},{0x90,76,100,0}};
  sendBlock(fx, 3, c2b, fr3);
  double r2 = 0;
  for (int i = 0; i < 16; ++i) r2 += render(fx, outs);
  r2 /= 16;
  printf("case2 pedal-down chord switch, new pitches:    RMS=%.6f\n", r2);

  sendBlock(fx, 4, alloff, fr3);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 3: pedal UP, fast switch same pitches (no retrigger path) ---
  const unsigned char c1b[][4] = {{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  sendBlock(fx, 3, c1b, fr3);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 3, c1off, fr3);                       // OFF pass through (pedal up)
  sendBlock(fx, 3, c2, fr3);                          // ON immediately, same block
  double r3 = 0;
  for (int i = 0; i < 16; ++i) r3 += render(fx, outs);
  r3 /= 16;
  printf("case3 pedal-up fast switch, same pitches:      RMS=%.6f\n", r3);

  sendBlock(fx, 4, alloff, fr3);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 4: pedal released @frame0 (drain OFFs @0,66,132), chord2 ONs
  //     @frame30 same pitches: drain OFFs for E,G land AFTER the new ONs ---
  sendBlock(fx, 1, d, fr0);                           // pedal down
  sendBlock(fx, 3, c1, fr3);                          // chord1
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 3, c1off, fr3);                       // queued
  const unsigned char mix[][4] = {{0xB0,64,0,0},{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0}};
  int fr4[4] = {0, 30, 30, 30};
  sendBlock(fx, 4, mix, fr4);                         // release@0 + chord2@30
  double r4 = 0;
  for (int i = 0; i < 16; ++i) r4 += render(fx, outs);
  r4 /= 16;
  printf("case4 release-drain overtakes new chord ONs:   RMS=%.6f\n", r4);

  bool bug1 = r4 < 0.1;   // 3 voices ~0.15; killed E,G leaves ~1 voice
  printf("case4 %s\n", bug1 ? "BUG (case4 notes killed by stale offs)" : "case4 sounds OK");

  sendBlock(fx, 4, alloff, fr3);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 5: voice exhaustion — pedal holds chord1 (4 notes), then
  //     chord2 (4 DIFFERENT notes): 8 held > TAL's 6 voices.
  //     If chord2 is silent -> voice pool exhausted, no stealing. ---
  sendBlock(fx, 1, d, fr0);                           // pedal down
  const unsigned char v1[][4] = {{0x90,60,100,0},{0x90,62,100,0},{0x90,64,100,0},{0x90,65,100,0}};
  int fr4a[4] = {0,0,0,0};
  sendBlock(fx, 4, v1, fr4a);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char v1off[][4] = {{0x80,60,0,0},{0x80,62,0,0},{0x80,64,0,0},{0x80,65,0,0}};
  sendBlock(fx, 4, v1off, fr4a);                      // queued: 4 voices held
  const unsigned char v2[][4] = {{0x90,72,100,0},{0x90,74,100,0},{0x90,76,100,0},{0x90,77,100,0}};
  sendBlock(fx, 4, v2, fr4a);                         // chord2: 4 more
  double r5 = 0;
  for (int i = 0; i < 16; ++i) r5 += render(fx, outs);
  r5 /= 16;
  printf("case5 pedal-held 4 + new 4 (8>6 voices):       RMS=%.6f\n", r5);
  const unsigned char allv[][4] = {{0x80,60,0,0},{0x80,62,0,0},{0x80,64,0,0},{0x80,65,0,0},
                                   {0x80,72,0,0},{0x80,74,0,0},{0x80,76,0,0},{0x80,77,0,0},{0xB0,64,0,0}};
  sendBlock(fx, 9, allv, fr4a);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 6 control: same but pedal UP (chord1 dies normally) ---
  sendBlock(fx, 4, v1, fr4a);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 4, v1off, fr4a);                      // pass through (pedal up)
  sendBlock(fx, 4, v2, fr4a);
  double r6 = 0;
  for (int i = 0; i < 16; ++i) r6 += render(fx, outs);
  r6 /= 16;
  printf("case6 control pedal-up 4 + new 4:              RMS=%.6f\n", r6);
  const unsigned char v2off[][4] = {{0x80,72,0,0},{0x80,74,0,0},{0x80,76,0,0},{0x80,77,0,0}};
  sendBlock(fx, 4, v2off, fr4a);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  bool bug5 = r5 < r6 * 0.5;
  printf("case5 %s\n", bug5 ? "VOICE EXHAUSTION (case5 << case6)" : "steals OK");

  // --- case 7: OVERLAP fast switch — chord2 ONs arrive BEFORE chord1 OFFs
  //     (fingers overlap, same keys). Pedal released after: keys still held
  //     by hand must NOT be killed by the flush. ---
  sendBlock(fx, 1, d, fr0);                            // pedal down
  sendBlock(fx, 3, c1, fr3);                           // chord1 ON C E G
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char ovl[][4] = {{0x90,60,100,0},{0x90,64,100,0},{0x90,67,100,0},
                                  {0x80,60,0,0},{0x80,64,0,0},{0x80,67,0,0}};
  int fr6[6] = {0,0,0,100,100,100};
  sendBlock(fx, 6, ovl, fr6);                          // chord2 ON, then chord1 OFF (queued)
  const unsigned char u[][4] = {{0xB0,64,0,0}};
  sendBlock(fx, 1, u, fr0);                            // pedal up: flush skips held keys
  double r7 = 0;
  for (int i = 0; i < 16; ++i) r7 += render(fx, outs);
  r7 /= 16;
  printf("case7 overlap switch, held keys survive:     RMS=%.6f\n", r7);
  sendBlock(fx, 3, c1off, fr3);                        // hand releases chord2
  for (int i = 0; i < 64; ++i) render(fx, outs);

  bool bug7 = r7 < 0.08;   // unfixed: flush kills all 3 held keys -> ~0
  printf("case7 %s\n", bug7 ? "BUG (held keys killed at pedal release)" : "held keys survive OK");

  // --- case 8: FULL 6+6 chord switch, pedal DOWN, different pitches.
  //     chord2 ONs steal all 6 voices held by chord1; pedal release then
  //     flushes chord1's queued offs for pitches that have NO voice left.
  //     Those stale offs must not touch chord2's fresh voices. ---
  const unsigned char s1[][4]  = {{0x90,60,100,0},{0x90,62,100,0},{0x90,64,100,0},
                                  {0x90,65,100,0},{0x90,67,100,0},{0x90,69,100,0}};
  const unsigned char s1o[][4] = {{0x80,60,0,0},{0x80,62,0,0},{0x80,64,0,0},
                                  {0x80,65,0,0},{0x80,67,0,0},{0x80,69,0,0}};
  const unsigned char s2[][4]  = {{0x90,72,100,0},{0x90,74,100,0},{0x90,76,100,0},
                                  {0x90,77,100,0},{0x90,79,100,0},{0x90,81,100,0}};
  const unsigned char s2o[][4] = {{0x80,72,0,0},{0x80,74,0,0},{0x80,76,0,0},
                                  {0x80,77,0,0},{0x80,79,0,0},{0x80,81,0,0}};
  int fr6b[6] = {0,0,0,0,0,0};
  sendBlock(fx, 1, d, fr0);
  sendBlock(fx, 6, s1, fr6b);                           // chord1: 6 voices
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 6, s1o, fr6b);                          // hand releases: 6 queued
  sendBlock(fx, 6, s2, fr6b);                           // chord2: steals all 6
  sendBlock(fx, 1, u, fr0);                            // release: stale offs for stolen pitches
  double r8 = 0;
  for (int i = 0; i < 16; ++i) r8 += render(fx, outs);
  r8 /= 16;
  printf("case8 6+6 sequential switch (voice steal):     RMS=%.6f\n", r8);
  sendBlock(fx, 6, s2o, fr6b);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 9: 6+6 OVERLAP switch (chord2 ONs before chord1 OFFs) ---
  sendBlock(fx, 1, d, fr0);
  sendBlock(fx, 6, s1, fr6b);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  const unsigned char ovl2[12][4] = {
    {0x90,72,100,0},{0x90,74,100,0},{0x90,76,100,0},{0x90,77,100,0},{0x90,79,100,0},{0x90,81,100,0},
    {0x80,60,0,0},{0x80,62,0,0},{0x80,64,0,0},{0x80,65,0,0},{0x80,67,0,0},{0x80,69,0,0}};
  int fr12[12] = {0,0,0,0,0,0,100,100,100,100,100,100};
  sendBlock(fx, 12, ovl2, fr12);                       // ONs steal, OFFs queued
  sendBlock(fx, 1, u, fr0);                            // release: queued offs stale
  double r9 = 0;
  for (int i = 0; i < 16; ++i) r9 += render(fx, outs);
  r9 /= 16;
  printf("case9 6+6 overlap switch (voice steal):        RMS=%.6f\n", r9);
  sendBlock(fx, 6, s2o, fr6b);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  bool bug8 = r8 < 0.1, bug9 = r9 < 0.1;
  printf("case8 %s  case9 %s\n",
         bug8 ? "BUG (stolen-pitch offs killed chord2)" : "OK",
         bug9 ? "BUG (overlap+steal killed chord2)" : "OK");

  // --- case 10: SPECTRAL proof of pool eviction. chord1 = low (C4 root),
  //     chord2 = high (C5 root). If eviction works: C5 rings, C4 dies.
  //     If TAL ignores new ONs (the user-reported bug): C4 rings, C5 silent. ---
  sendBlock(fx, 1, d, fr0);
  sendBlock(fx, 6, s1, fr6b);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 6, s1o, fr6b);                          // offs queued, 6 voices held
  sendBlock(fx, 6, s2, fr6b);                           // new chord must evict
  g_ncap = 0;
  for (int i = 0; i < 16; ++i) render(fx, outs);
  double aC4 = goertzel(261.63), aC5 = goertzel(523.25);
  printf("case10 spectral: chord1@262Hz=%.5f chord2@523Hz=%.5f\n", aC4, aC5);
  sendBlock(fx, 6, s2o, fr6b);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  // --- case 11: scale note over 6 pedal-held notes must sound (evict oldest) ---
  sendBlock(fx, 1, d, fr0);
  sendBlock(fx, 6, s1, fr6b);
  for (int i = 0; i < 8; ++i) render(fx, outs);
  sendBlock(fx, 6, s1o, fr6b);
  const unsigned char sc[][4]  = {{0x90,84,100,0}};     // C6
  const unsigned char sco[][4] = {{0x80,84,0,0}};
  sendBlock(fx, 1, sc, fr0);
  g_ncap = 0;
  for (int i = 0; i < 16; ++i) render(fx, outs);
  double aC6 = goertzel(1046.5);
  printf("case11 scale note C6 amp=%.5f\n", aC6);
  sendBlock(fx, 1, sco, fr0);
  sendBlock(fx, 1, u, fr0);
  for (int i = 0; i < 64; ++i) render(fx, outs);

  bool bug10 = aC5 < aC4, bug11 = aC6 < 0.01;
  printf("case10 %s  case11 %s\n",
         bug10 ? "BUG (new chord silent, old still ringing)" : "eviction audible OK",
         bug11 ? "BUG (scale note silent over held 6)" : "scale note sounds OK");
  return (bug1 || bug5 || bug7 || bug8 || bug9 || bug10 || bug11) ? 1 : 0;
}
