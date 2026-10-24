// host_test.cpp — minimal VST2 host. Loads proxy.dll (which loads
// test_target.dll via sustainhold.ini), drives MIDI scenarios through the
// proxy, then asserts the exact event sequence the target received.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "aeffectx.h"
#include "platform.h"

static const int kBlock = 512;
static int g_fail = 0, g_pass = 0;

static VstIntPtr VSTCALLBACK masterCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) {
  switch (opcode) {
    case audioMasterVersion:      return 0x0240;
    case audioMasterGetSampleRate: return 44100;
    case audioMasterGetBlockSize:  return kBlock;
    default: return 0;
  }
}

struct Sent { int frames; unsigned char st, d1, d2; };

struct SendBuf {                 // head.events[2] + extra must be adjacent
  VstEvents head;
  VstEvent* extra[64];
};

static void sendBlock(AEffect* fx, const std::vector<Sent>& evs) {
  static VstMidiEvent store[64];
  static SendBuf buf;
  VstEvent** p = &buf.head.events[0];   // spills into extra by layout
  for (size_t i = 0; i < evs.size(); ++i) {
    memset(&store[i], 0, sizeof(store[i]));
    store[i].type = kVstMidiType;
    store[i].byteSize = (VstInt32)sizeof(VstMidiEvent);
    store[i].deltaFrames = evs[i].frames;
    store[i].midiData[0] = (char)evs[i].st;
    store[i].midiData[1] = (char)evs[i].d1;
    store[i].midiData[2] = (char)evs[i].d2;
    p[i] = (VstEvent*)&store[i];
  }
  buf.head.numEvents = (VstInt32)evs.size();
  buf.head.reserved = 0;
  fx->dispatcher(fx, effProcessEvents, 0, 0, &buf.head, 0.f);
}

static void renderBlocks(AEffect* fx, int n) {
  static float in0[kBlock], in1[kBlock], out0[kBlock], out1[kBlock];
  float* ins[2] = { in0, in1 };
  float* outs[2] = { out0, out1 };
  for (int i = 0; i < n; ++i) fx->processReplacing(fx, ins, outs, kBlock);
}

struct Rec { int block, frames, st, d1, d2; };
static std::vector<Rec> readLog() {
  std::vector<Rec> r; int block = -1;
  FILE* f = fopen("test_target_events.log", "r");
  if (!f) return r;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == 'B') { ++block; continue; }
    Rec x;
    if (sscanf(line, "EV %d %02X %d %d", &x.frames, &x.st, &x.d1, &x.d2) == 4) {
      x.block = block; r.push_back(x);
    }
  }
  fclose(f);
  return r;
}

static void check(bool ok, const char* what) {
  if (ok) ++g_pass;
  else { ++g_fail; printf("FAIL: %s\n", what); }
}

int main() {
  // self-contained config: proxy sits next to test_target in build/
  FILE* ini = fopen("sustainhold.ini", "w");
#ifdef _WIN32
  fputs("[sustainhold]\ntarget=test_target.dll\n", ini);
#else
  fputs("[sustainhold]\ntarget=test_target.dylib\n", ini);
#endif
  fclose(ini);

  ModHandle h = modLoad(modDefaultName());
  if (!h) { printf("FATAL: cannot load proxy module\n"); return 2; }
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn fn = (MainFn)modSym(h, "VSTPluginMain");
  if (!fn) { printf("FATAL: no VSTPluginMain export\n"); return 2; }
  AEffect* fx = fn(masterCb);
  if (!fx) { printf("FATAL: proxy returned null AEffect\n"); return 2; }

  check(fx->magic == kEffectMagic, "magic");
  check(fx->numParams == 5, "numParams == 3+2");
  check(fx->uniqueID == CCONST('T','T','s','t'), "uniqueID forwarded");
  check(fx->dispatcher(fx, effGetVstVersion, 0, 0, 0, 0) == 2400, "effGetVstVersion forwarded");
  char vstr[64] = {0};
  fx->dispatcher(fx, effGetVendorString, 0, 0, vstr, 0);
  check(strcmp(vstr, "TestTarget") == 0, "vendor string forwarded");
  char pname[8] = {0};
  fx->dispatcher(fx, effGetParamName, 3, 0, pname, 0);
  check(strcmp(pname, "FwdCC64") == 0, "our param name at index 3");

  fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
  fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 44100.0f);
  fx->dispatcher(fx, effSetBlockSize, 0, kBlock, 0, 0);
  fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);

  // A: basic sustain — CC64 down, note on/off while held, pedal up flushes
  sendBlock(fx, {{0, 0xB0, 64, 127}});                       // b0: pedal down (swallowed)
  sendBlock(fx, {{10, 0x90, 60, 100}, {100, 0x80, 60, 0}});  // b1: off suppressed
  sendBlock(fx, {{5, 0xB0, 64, 0}});                         // b2: pedal up -> flush @5

  // B: retrigger while held — old note-off emitted before new note-on
  sendBlock(fx, {{0, 0xB0, 64, 127},                 // pedal down again
                 {0, 0x90, 62, 90}, {50, 0x80, 62, 0},
                 {80, 0x90, 62, 90}, {100, 0x80, 62, 0}});   // b3
  sendBlock(fx, {{0, 0xB0, 64, 0}});                         // b4: pedal up -> flush

  // C: passthrough — pitch bend and other CC untouched
  sendBlock(fx, {{0, 0xE0, 0, 64}, {10, 0xB0, 1, 100}});     // b5

  // D: 7 notes held (pool cap 6 default) — 7th ON evicts oldest, flush 66 apart
  sendBlock(fx, {{0, 0xB0, 64, 127},
                 {0, 0x90, 60, 100}, {10, 0x90, 61, 100}, {20, 0x90, 62, 100},
                 {30, 0x90, 63, 100}, {40, 0x90, 64, 100}, {50, 0x90, 65, 100},
                 {60, 0x90, 66, 100},
                 {100, 0x80, 60, 0}, {110, 0x80, 61, 0}, {120, 0x80, 62, 0},
                 {130, 0x80, 63, 0}, {140, 0x80, 64, 0}, {150, 0x80, 65, 0},
                 {160, 0x80, 66, 0}});                       // b6: 7th ON evicts 60
  sendBlock(fx, {{0, 0xB0, 64, 0}});                         // b7: flush 7 @66 apart

  // E: forward_cc64 param turns CC64 forwarding on
  fx->setParameter(fx, 3, 1.0f);
  check(fx->getParameter(fx, 3) == 1.0f, "param 3 set/get");
  fx->setParameter(fx, 4, 1.0f);
  check(fx->getParameter(fx, 4) == 1.0f, "param 4 set/get");
  fx->setParameter(fx, 4, 0.0f);
  check(fx->getParameter(fx, 4) == 0.0f, "param 4 off");
  sendBlock(fx, {{0, 0xB0, 64, 127}});                       // b8: now forwarded
  sendBlock(fx, {{0, 0xB0, 64, 0}});                         // b9: forwarded

  // F: PolyCap6 off (param4=0) -> unlimited pool: 7 ONs pass, no eviction
  fx->setParameter(fx, 4, 0.0f);
  sendBlock(fx, {{0, 0xB0, 64, 127},
                 {0, 0x90, 60, 100}, {10, 0x90, 61, 100}, {20, 0x90, 62, 100},
                 {30, 0x90, 63, 100}, {40, 0x90, 64, 100}, {50, 0x90, 65, 100},
                 {60, 0x90, 66, 100},
                 {100, 0x80, 60, 0}, {110, 0x80, 61, 0}, {120, 0x80, 62, 0},
                 {130, 0x80, 63, 0}, {140, 0x80, 64, 0}, {150, 0x80, 65, 0},
                 {160, 0x80, 66, 0}});                       // b10: no eviction
  sendBlock(fx, {{0, 0xB0, 64, 0}});                         // b11: flush 60..66

  // G: starvation regression — release near block end, then NO further MIDI:
  // trailing note-offs must be pumped by the audio path
  fx->setParameter(fx, 4, 1.0f);                             // pool cap back on
  sendBlock(fx, {{0, 0xB0, 64, 127},
                 {0, 0x90, 70, 100}, {0, 0x90, 71, 100}, {0, 0x90, 72, 100},
                 {0, 0x90, 73, 100}, {0, 0x90, 74, 100},
                 {50, 0x80, 70, 0}, {50, 0x80, 71, 0}, {50, 0x80, 72, 0},
                 {50, 0x80, 73, 0}, {50, 0x80, 74, 0}});     // b12: 5 queued
  sendBlock(fx, {{500, 0xB0, 64, 0}});                       // b13: release @500 -> only 70 drains
  renderBlocks(fx, 1);                                       // b14: pump drains 71..74

  // H: OVERLAP fast switch — chord2 ONs arrive BEFORE chord1 OFFs (fingers
  //    overlap on the same keys). Pedal release must NOT kill keys the hand
  //    still holds: queued offs for re-pressed pitches are dropped.
  sendBlock(fx, {{0, 0xB0, 64, 127},
                 {0, 0x90, 60, 100}, {10, 0x90, 62, 100}, {20, 0x90, 64, 100}});   // b15
  sendBlock(fx, {{0, 0x90, 60, 100}, {10, 0x90, 62, 100}, {20, 0x90, 64, 100},     // b16: chord2 ON
                 {100, 0x80, 60, 0}, {110, 0x80, 62, 0}, {120, 0x80, 64, 0}});     //      chord1 OFF queued
  sendBlock(fx, {{0, 0xB0, 64, 0}});                                               // b17: release -> offs skipped
  sendBlock(fx, {{0, 0x80, 60, 0}, {10, 0x80, 62, 0}, {20, 0x80, 64, 0}});         // b18: hand releases

  // I: voice-pool eviction — pedal holds 6 notes (target pool full, its offs
  //    queued). Target NEVER steals key-down voices, so the new chord's ONs
  //    would be ignored unless the engine evicts oldest sustained voices.
  sendBlock(fx, {{0, 0xB0, 64, 127},
                 {0, 0x90, 60, 100}, {10, 0x90, 62, 100}, {20, 0x90, 64, 100},
                 {30, 0x90, 65, 100}, {40, 0x90, 67, 100}, {50, 0x90, 69, 100}});  // b19: pool=6
  sendBlock(fx, {{0, 0x80, 60, 0}, {10, 0x80, 62, 0}, {20, 0x80, 64, 0},
                 {30, 0x80, 65, 0}, {40, 0x80, 67, 0}, {50, 0x80, 69, 0}});        // b20: all queued
  sendBlock(fx, {{0, 0x90, 72, 100}, {10, 0x90, 74, 100}, {20, 0x90, 76, 100},
                 {30, 0x90, 77, 100}, {40, 0x90, 79, 100}, {50, 0x90, 81, 100}});  // b21: evict 60..69
  sendBlock(fx, {{0, 0xB0, 64, 0}});                                               // b22: queue already empty
  sendBlock(fx, {{0, 0x80, 72, 0}, {10, 0x80, 74, 0}, {20, 0x80, 76, 0},
                 {30, 0x80, 77, 0}, {40, 0x80, 79, 0}, {50, 0x80, 81, 0}});        // b23: pass through

  fx->dispatcher(fx, effMainsChanged, 0, 0, 0, 0);
  fx->dispatcher(fx, effClose, 0, 0, 0, 0);

  // ---- assert exact sequence seen by the target ----
  std::vector<Rec> r = readLog();
  struct Exp { int block, frames, st, d1; };
  std::vector<Exp> exp = {
    // b1
    {1, 10, 0x90, 60},
    // b2
    {2, 5, 0x80, 60},
    // b3
    {3, 0, 0x90, 62}, {3, 80, 0x80, 62}, {3, 80, 0x90, 62},
    // b4
    {4, 0, 0x80, 62},
    // b5
    {5, 0, 0xE0, 0}, {5, 10, 0xB0, 1},
    // b6: 7 note-ons; 7th ON (66) evicts oldest voice (60) first
    {6, 0, 0x90, 60}, {6, 10, 0x90, 61}, {6, 20, 0x90, 62}, {6, 30, 0x90, 63},
    {6, 40, 0x90, 64}, {6, 50, 0x90, 65},
    {6, 60, 0x80, 60}, {6, 60, 0x90, 66},
    // b7: 7 note-offs, 66 frames apart
    {7, 0, 0x80, 60}, {7, 66, 0x80, 61}, {7, 132, 0x80, 62}, {7, 198, 0x80, 63},
    {7, 264, 0x80, 64}, {7, 330, 0x80, 65}, {7, 396, 0x80, 66},
    // b8/b9: CC64 forwarded
    {8, 0, 0xB0, 64}, {9, 0, 0xB0, 64},
    // b10: PolyCap6 off -> unlimited pool: CC64 fwd + 7 note-ons, NO eviction
    {10, 0, 0xB0, 64},
    {10, 0, 0x90, 60}, {10, 10, 0x90, 61}, {10, 20, 0x90, 62}, {10, 30, 0x90, 63},
    {10, 40, 0x90, 64}, {10, 50, 0x90, 65}, {10, 60, 0x90, 66},
    // b11: flush all 7 (stable sort), CC64 fwd at same frame
    {11, 0, 0x80, 60}, {11, 0, 0xB0, 64},
    {11, 66, 0x80, 61}, {11, 132, 0x80, 62},
    {11, 198, 0x80, 63}, {11, 264, 0x80, 64}, {11, 330, 0x80, 65}, {11, 396, 0x80, 66},
    // b12: CC64 fwd + 5 note-ons (offs queued)
    {12, 0, 0xB0, 64},
    {12, 0, 0x90, 70}, {12, 0, 0x90, 71}, {12, 0, 0x90, 72}, {12, 0, 0x90, 73}, {12, 0, 0x90, 74},
    // b13: release @500: only 70 drains in-block, CC64 fwd same frame
    {13, 500, 0x80, 70}, {13, 500, 0xB0, 64},
    // b14: audio-path pump delivers the rest (no MIDI was sent)
    {14, 0, 0x80, 71}, {14, 66, 0x80, 72}, {14, 132, 0x80, 73}, {14, 198, 0x80, 74},
    // b15: CC64 fwd + chord1 ONs
    {15, 0, 0xB0, 64},
    {15, 0, 0x90, 60}, {15, 10, 0x90, 62}, {15, 20, 0x90, 64},
    // b16: chord2 ONs pass through (overlap); chord1 OFFs swallowed (queued)
    {16, 0, 0x90, 60}, {16, 10, 0x90, 62}, {16, 20, 0x90, 64},
    // b17: pedal release: queued offs DROPPED (hand still holds) — only CC64 fwd
    {17, 0, 0xB0, 64},
    // b18: hand finally releases -> offs pass through
    {18, 0, 0x80, 60}, {18, 10, 0x80, 62}, {18, 20, 0x80, 64},
    // b19: CC64 fwd + 6 note-ons (pool full)
    {19, 0, 0xB0, 64},
    {19, 0, 0x90, 60}, {19, 10, 0x90, 62}, {19, 20, 0x90, 64},
    {19, 30, 0x90, 65}, {19, 40, 0x90, 67}, {19, 50, 0x90, 69},
    // b20: all 6 offs queued (swallowed)
    // b21: each new ON evicts oldest sustained voice, same frame
    {21, 0, 0x80, 60},  {21, 0, 0x90, 72},
    {21, 10, 0x80, 62}, {21, 10, 0x90, 74},
    {21, 20, 0x80, 64}, {21, 20, 0x90, 76},
    {21, 30, 0x80, 65}, {21, 30, 0x90, 77},
    {21, 40, 0x80, 67}, {21, 40, 0x90, 79},
    {21, 50, 0x80, 69}, {21, 50, 0x90, 81},
    // b22: release: queue was emptied by evictions -> only CC64 fwd
    {22, 0, 0xB0, 64},
    // b23: hand releases -> offs pass through
    {23, 0, 0x80, 72}, {23, 10, 0x80, 74}, {23, 20, 0x80, 76},
    {23, 30, 0x80, 77}, {23, 40, 0x80, 79}, {23, 50, 0x80, 81},
  };
  check(r.size() == exp.size(), "event count");
  if (r.size() != exp.size()) {
    printf("got %zu events:\n", r.size());
    for (const Rec& x : r) printf("  b%d f%d %02X %d %d\n", x.block, x.frames, x.st, x.d1, x.d2);
  }
  for (size_t i = 0; i < exp.size() && i < r.size(); ++i) {
    char msg[96];
    snprintf(msg, sizeof(msg), "event %zu: b%d f%d %02X %d", i,
             exp[i].block, exp[i].frames, exp[i].st, exp[i].d1);
    check(r[i].block == exp[i].block && r[i].frames == exp[i].frames &&
          r[i].st == exp[i].st && r[i].d1 == exp[i].d1, msg);
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
