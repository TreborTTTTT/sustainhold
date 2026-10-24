// proxy.cpp — VST2 proxy that adds CC64 sustain semantics in front of any
// VST2 x64 instrument. The real plugin is loaded from a path given in
// sustainhold.ini (next to this module); everything else is forwarded untouched.
//
// Windows layout at install:
//   C:\Program Files\VSTPlugins\TAL-U-No-62.dll            <- this proxy
//   C:\Program Files\VSTPlugins\sustainhold.ini           <- config
//   C:\Program Files\VSTPlugins\TAL-U-No-62-original\
//        TAL-U-No-62-original.dll                         <- renamed real plugin
//
// macOS layout at install (target = TAL-U-NO-LX-V2, the 64-bit successor):
//   /Library/Audio/Plug-Ins/VST/TAL-U-NO-LX-V2.vst        <- this proxy bundle
//        Contents/MacOS/TAL-U-NO-LX-V2
//        Contents/MacOS/sustainhold.ini
//   /Library/Application Support/sustainhold/
//        TAL-U-NO-LX-V2.vst                              <- real plugin, hidden
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

#include "aeffectx.h"
#include "sustain_core.h"
#include "platform.h"

static const int kMaxEvents = 1024;

struct EvBuf {                       // VstEvents header + contiguous ptr array
  VstEvents head;                    // head.events[2] declared size
  VstEvent* extra[kMaxEvents];       // arr[i] beyond index 1 lands in extra
};

struct ProxyInstance {
  AEffect* self = nullptr;
  AEffect* target = nullptr;
  ModHandle hMod = nullptr;
  audioMasterCallback hostCb = nullptr;
  int targetNumParams = 0;
  int blockFrames = 512;
  SustainEngine engine;
  std::vector<VstMidiEvent> pool;
  EvBuf evbuf;
};

static std::vector<ProxyInstance*> g_instances;
static ProxyInstance* g_building = nullptr;   // instance under construction

// ---------------------------------------------------------------- config ---
static std::string proxyDir() { return modSelfDir(); }

// ini keys (section [sustainhold]): target=<path>, forward_cc64=0|1,
// max_voices=<int>, spacing_ms10=<int>, log=0|1
static bool readConfig(const std::string& dir, std::string& targetPath, SustainConfig& cfg) {
#ifdef _WIN32
  targetPath = dir + "\\TAL-U-No-62-original\\TAL-U-No-62-original.dll";
  std::string iniPath = dir + "\\sustainhold.ini";
#else
  targetPath = "/Library/Application Support/sustainhold/TAL-U-NO-LX-V2.vst/Contents/MacOS/TAL-U-NO-LX-V2";
  std::string iniPath = dir + "/sustainhold.ini";
#endif
  IniVals v;
  if (readIniPortable(iniPath, v)) {
    if (v.hasTarget && !v.target.empty()) {
#ifdef _WIN32
      bool abs = (v.target.size() > 1 && v.target[1] == ':');
      targetPath = abs ? v.target : (dir + "\\" + v.target);
#else
      bool abs = (!v.target.empty() && v.target[0] == '/');
      targetPath = abs ? v.target : (dir + "/" + v.target);
#endif
    }
    cfg.forwardCC64 = v.forward_cc64 != 0;
    cfg.maxVoices   = v.max_voices;
    if (cfg.maxVoices < 0) cfg.maxVoices = 0;
    cfg.spacingMs10 = v.spacing_ms10;
    if (cfg.spacingMs10 < 1) cfg.spacingMs10 = 1;
    return v.log != 0;
  }
  return false;
}

// -------------------------------------------------------------- event log --
static FILE* g_log = nullptr;
static unsigned long long nowMs() {
  return (unsigned long long)
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void logEnable(bool on) {
  if (!on || g_log) return;
  char path[600];
#ifdef _WIN32
  char base[512] = {0};
  if (!GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) || !base[0])
    strcpy(base, ".");
  snprintf(path, sizeof(path), "%s\\sustainhold_log.txt", base);
#else
  const char* t = getenv("TMPDIR");
  if (!t || !*t) t = "/tmp";
  snprintf(path, sizeof(path), "%s/sustainhold_log.txt", t);
#endif
  g_log = fopen(path, "w");
}
static void logEv(const char* tag, VstEvents* ev) {
  if (!g_log) return;
  fprintf(g_log, "%s %llu\n", tag, nowMs());
  if (ev) for (int i = 0; i < ev->numEvents; ++i) {
    VstEvent* e = ev->events[i];
    if (e && e->type == kVstMidiType) {
      VstMidiEvent* m = (VstMidiEvent*)e;
      fprintf(g_log, "  f%d %02X %d %d\n", e->deltaFrames,
              (unsigned char)m->midiData[0], (unsigned char)m->midiData[1], (unsigned char)m->midiData[2]);
    } else if (e) fprintf(g_log, "  f%d type=%d\n", e->deltaFrames, e->type);
  }
  fflush(g_log);
}

// ------------------------------------------------------- master callback ---
// The target plugin calls this with ITS AEffect*; swap to our AEffect* so the
// host only ever sees the proxy instance.
static VstIntPtr VSTCALLBACK masterProxy(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                         VstIntPtr value, void* ptr, float opt) {
  for (ProxyInstance* inst : g_instances) {
    if (inst->target == effect)
      return inst->hostCb(inst->self, opcode, index, value, ptr, opt);
  }
  // during construction the target AEffect isn't registered yet: forward anyway
  if (g_building)
    return g_building->hostCb(nullptr, opcode, index, value, ptr, opt);
  return 0;
}

static ProxyInstance* findInstance(AEffect* effect) {
  if (effect && effect->user) return (ProxyInstance*)effect->user;
  for (ProxyInstance* inst : g_instances)
    if (inst->self == effect || inst->target == effect) return inst;
  return nullptr;
}

// ------------------------------------------------------------- MIDI path ---
static void buildAndSend(ProxyInstance* inst, std::vector<MidiEv>& out,
                         std::vector<VstEvent*>* passthrough) {
  VstEvent** arr = &inst->evbuf.head.events[0];
  int n = 0;
  inst->pool.resize(out.size());
  for (size_t i = 0; i < out.size() && n < kMaxEvents; ++i) {
    VstMidiEvent& m = inst->pool[i];
    memset(&m, 0, sizeof(m));
    m.type = kVstMidiType;
    m.byteSize = (VstInt32)(sizeof(VstMidiEvent) - 2 * sizeof(VstInt32));
    m.deltaFrames = out[i].frames;
    m.midiData[0] = (char)out[i].status;
    m.midiData[1] = (char)out[i].d1;
    m.midiData[2] = (char)out[i].d2;
    m.midiData[3] = 0;
    arr[n++] = (VstEvent*)&m;
  }
  if (passthrough) for (VstEvent* p : *passthrough) if (n < kMaxEvents) arr[n++] = p;
  inst->evbuf.head.numEvents = n;
  inst->evbuf.head.reserved = 0;
  inst->target->dispatcher(inst->target, effProcessEvents, 0, 0, &inst->evbuf.head, 0.f);
}

// Drive the cross-block note-off flush from the AUDIO path: after a pedal
// release with no further MIDI input, processReplacing keeps pumping until
// the flush queue is empty. Without this, trailing note-offs starve and
// notes stick (the bug users see with big chords).
static void pumpFlush(ProxyInstance* inst) {
  if (inst->engine.flushing.empty()) return;
  std::vector<MidiEv> out;
  inst->engine.pump(out, inst->blockFrames);
  if (!out.empty()) {
    buildAndSend(inst, out, nullptr);
    logEv("PUMP", &inst->evbuf.head);
  }
}

static void handleProcessEvents(ProxyInstance* inst, VstEvents* events) {
  std::vector<MidiEv> in;
  std::vector<VstEvent*> passthrough; // sysex & unknown types: untouched
  if (events) {
    for (int i = 0; i < events->numEvents; ++i) {
      VstEvent* ev = events->events[i];
      if (!ev) continue;
      if (ev->type == kVstMidiType) {
        VstMidiEvent* m = (VstMidiEvent*)ev;
        if ((unsigned char)m->midiData[0] >= 0x80) {
          MidiEv e;
          e.status = (unsigned char)m->midiData[0];
          e.d1 = (unsigned char)m->midiData[1];
          e.d2 = (unsigned char)m->midiData[2];
          e.frames = ev->deltaFrames;
          in.push_back(e);
        } else passthrough.push_back(ev);
      } else passthrough.push_back(ev);
    }
  }

  std::vector<MidiEv> out;
  inst->engine.processBlock(in, out, inst->blockFrames);
  logEv("IN ", events);
  buildAndSend(inst, out, &passthrough);
  logEv("OUT", &inst->evbuf.head);
}

// ------------------------------------------------------------- dispatcher --
static void copyStr(char* dst, const char* src, int maxLen) {
  if (!dst) return;
  strncpy(dst, src, maxLen - 1);
  dst[maxLen - 1] = 0;
}

static VstIntPtr VSTCALLBACK proxyDispatcher(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                             VstIntPtr value, void* ptr, float opt) {
  ProxyInstance* inst = findInstance(effect);
  if (!inst) return 0;
  AEffect* t = inst->target;
  int tnp = inst->targetNumParams;

  // our two extra params live at indices [tnp, tnp+1]: display strings only here,
  // value get/set goes through AEffect::setParameter/getParameter pointers
  if (index >= tnp) {
    int ours = index - tnp; // 0 = forward CC64, 1 = 6-voice cap
    switch (opcode) {
      case effGetParamLabel:   copyStr((char*)ptr, "", kVstMaxParamStrLen); return 0;
      case effGetParamDisplay: copyStr((char*)ptr,
                                 (ours == 0 ? inst->engine.cfg.forwardCC64
                                            : inst->engine.cfg.maxVoices > 0) ? "on" : "off",
                                 kVstMaxParamStrLen);
                               return 0;
      case effGetParamName:    copyStr((char*)ptr, ours == 0 ? "FwdCC64" : "PolyCap6", kVstMaxParamStrLen);
                               return 0;
      case effGetParameterProperties: return 0;
      case effCanBeAutomated: return 1;
      default: break; // everything else falls through to target
    }
  }

  switch (opcode) {
    case effProcessEvents:
      handleProcessEvents(inst, (VstEvents*)ptr);
      return 0;
    case effSetSampleRate:
      inst->engine.cfg.sampleRate = (int)(opt > 0.f ? opt : (float)value);
      if (g_log) fprintf(g_log, "SETRATE %d\n", inst->engine.cfg.sampleRate);
      break; // also forward
    case effSetBlockSize:
      inst->blockFrames = (int)value > 0 ? (int)value : 512;
      if (g_log) fprintf(g_log, "SETBLOCK %d\n", inst->blockFrames);
      break; // also forward
    case effClose: {
      VstIntPtr r = t->dispatcher(t, effClose, index, value, ptr, opt);
      auto it = std::find(g_instances.begin(), g_instances.end(), inst);
      if (it != g_instances.end()) g_instances.erase(it);
      modFree(inst->hMod);
      delete inst->self;
      delete inst;
      return r;
    }
    default: break;
  }
  return t->dispatcher(t, opcode, index, value, ptr, opt);
}

// ---------------------------------------------------------- param pointers --
static void VSTCALLBACK proxySetParameter(AEffect* effect, VstInt32 index, float value) {
  ProxyInstance* inst = findInstance(effect);
  if (!inst) return;
  if (index >= inst->targetNumParams) {
    int ours = index - inst->targetNumParams;
    if (ours == 0) inst->engine.cfg.forwardCC64 = (value >= 0.5f);
    else           inst->engine.cfg.maxVoices   = (value >= 0.5f) ? 6 : 0;
    return;
  }
  if (inst->target->setParameter) inst->target->setParameter(inst->target, index, value);
}

static float VSTCALLBACK proxyGetParameter(AEffect* effect, VstInt32 index) {
  ProxyInstance* inst = findInstance(effect);
  if (!inst) return 0.f;
  if (index >= inst->targetNumParams) {
    int ours = index - inst->targetNumParams;
    bool on = (ours == 0) ? inst->engine.cfg.forwardCC64 : inst->engine.cfg.maxVoices > 0;
    return on ? 1.f : 0.f;
  }
  return inst->target->getParameter ? inst->target->getParameter(inst->target, index) : 0.f;
}

// ------------------------------------------------------------- audio path --
static void VSTCALLBACK proxyProcessReplace(AEffect* effect, float** inputs, float** outputs,
                                            VstInt32 numSamples) {
  ProxyInstance* inst = findInstance(effect);
  if (inst) {
    pumpFlush(inst);
    if (inst->target->processReplacing)
      inst->target->processReplacing(inst->target, inputs, outputs, numSamples);
  }
}

static void VSTCALLBACK proxyProcessDoubleReplace(AEffect* effect, double** inputs, double** outputs,
                                                  VstInt32 numSamples) {
  ProxyInstance* inst = findInstance(effect);
  if (inst) {
    pumpFlush(inst);
    if (inst->target->processDoubleReplacing)
      inst->target->processDoubleReplacing(inst->target, inputs, outputs, numSamples);
  }
}

// ------------------------------------------------------------------ main ---
SH_EXPORT AEffect* VSTPluginMain(audioMasterCallback hostCb) {
  std::string dir = proxyDir();
  std::string targetPath;
  SustainConfig cfg;
  bool wantLog = readConfig(dir, targetPath, cfg);
  logEnable(wantLog);

  ModHandle h = modLoad(targetPath.c_str());
  if (!h) return nullptr;
  typedef AEffect* (*MainFn)(audioMasterCallback);
  MainFn fn = (MainFn)modSym(h, "VSTPluginMain");
  if (!fn) fn = (MainFn)modSym(h, "main");
  if (!fn) { modFree(h); return nullptr; }

  ProxyInstance* inst = new ProxyInstance();
  inst->hMod = h;
  inst->pool.reserve(kMaxEvents);   // event addresses must stay valid: some
                                    // plugins store the pointers past the call
  inst->hostCb = hostCb;
  inst->engine.cfg = cfg;
  inst->engine.cfg.sampleRate = 44100;

  // register as under-construction BEFORE calling the target: many plugins
  // query the host (version etc.) inside their factory and bail on 0 answers
  g_building = inst;
  AEffect* t = fn(masterProxy);
  g_building = nullptr;
  if (!t) { modFree(h); delete inst; return nullptr; }

  inst->target = t;
  inst->targetNumParams = t->numParams;

  AEffect* self = new AEffect();
  memset(self, 0, sizeof(AEffect));
  self->magic = kEffectMagic;
  self->dispatcher = proxyDispatcher;
  self->setParameter = proxySetParameter;
  self->getParameter = proxyGetParameter;
  self->numPrograms = t->numPrograms;
  self->numParams = t->numParams + 2;
  self->numInputs = t->numInputs;
  self->numOutputs = t->numOutputs;
  self->flags = t->flags;
  self->initialDelay = t->initialDelay;
  self->object = inst;
  self->user = inst;
  self->uniqueID = t->uniqueID;   // keep identity for host preset/project matching
  self->version = t->version;
  self->processReplacing = t->processReplacing ? proxyProcessReplace : nullptr;
  self->processDoubleReplacing = t->processDoubleReplacing ? proxyProcessDoubleReplace : nullptr;

  inst->self = self;
  g_instances.push_back(inst);
  return self;
}

// macOS VST2 hosts look for the legacy 'main' symbol (Studio One accepts both).
#ifndef _WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmain"
SH_EXPORT AEffect* main(audioMasterCallback hostCb) { return VSTPluginMain(hostCb); }
#pragma GCC diagnostic pop
#endif
