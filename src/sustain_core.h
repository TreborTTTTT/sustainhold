// sustain_core.h — CC64 sustain-pedal state machine (VST-agnostic)
// Spec: piano-style sustain for synths that ignore CC64.
#pragma once
#include <vector>
#include <deque>
#include <cstdint>
#include <algorithm>

struct MidiEv {
  uint8_t status = 0; // full status byte incl. channel nibble
  uint8_t d1 = 0, d2 = 0;
  int frames = 0;     // timestamp within current audio block
};

struct SustainConfig {
  bool forwardCC64 = false; // forward CC64 to target? (default: swallow)
  int  maxVoices = 6;       // target voice pool cap (TAL-U-No-62 = 6). 0 = unlimited.
  int sampleRate = 44100;
  int spacingMs10 = 15;     // gap between flushed note-offs, tenths of ms (1.5ms)
};

class SustainEngine {
public:
  SustainConfig cfg;

  struct Held { uint8_t pitch; uint8_t ch; };
  struct Voice { uint8_t pitch; uint8_t ch; bool keyHeld; };
  bool held = false;
  std::deque<Held> queue;    // note-offs suppressed while pedal down
  std::deque<Held> flushing; // released, being emitted with spacing across blocks
  std::deque<Voice> pool;    // voices the target currently holds (ON sent, no OFF yet)
  uint8_t keyDown[16][128] = {}; // keys physically held by hand (counts overlapping presses)

  int spacingFrames() const {
    int f = (cfg.sampleRate * cfg.spacingMs10) / 10000;
    return f < 1 ? 1 : f;
  }

  static bool isNoteOn(const MidiEv& e)  { return (e.status & 0xF0) == 0x90 && e.d2 > 0; }
  static bool isNoteOff(const MidiEv& e) { return (e.status & 0xF0) == 0x80 ||
                                                  ((e.status & 0xF0) == 0x90 && e.d2 == 0); }

  static int find(const std::deque<Held>& q, uint8_t p, uint8_t c) {
    for (int i = 0; i < (int)q.size(); ++i)
      if (q[i].pitch == p && q[i].ch == c) return i;
    return -1;
  }
  int findPool(uint8_t p, uint8_t c) const {
    for (int i = 0; i < (int)pool.size(); ++i)
      if (pool[i].pitch == p && pool[i].ch == c) return i;
    return -1;
  }

  // Pop next queued note-off to emit. Pitches whose key is STILL held by hand
  // are dropped: a fast chord switch re-pressed that key, pedal release must
  // not kill it. Does not advance ff for dropped entries.
  bool popFlushed(std::vector<MidiEv>& out, int& ff) {
    while (!flushing.empty()) {
      Held h = flushing.front(); flushing.pop_front();
      int pi = findPool(h.pitch, h.ch);
      if (keyDown[h.ch][h.pitch] > 0) {
        if (pi >= 0) pool[pi].keyHeld = true; // re-grabbed by hand: voice lives on
        continue; // hand still holds: don't kill
      }
      if (pi >= 0) pool.erase(pool.begin() + pi); // voice now released
      out.push_back({(uint8_t)(0x80 | h.ch), h.pitch, 0, ff});
      ff += spacingFrames();
      return true;
    }
    return false;
  }

  // Drain pending note-offs for one block, starting at frame 0.
  void pump(std::vector<MidiEv>& out, int blockFrames) {
    int f = 0;
    while (f < blockFrames && popFlushed(out, f)) {}
  }

  // Process one audio block.
  // in:  events of this block (any order). out: events to forward, sorted by frames.
  void processBlock(const std::vector<MidiEv>& in, std::vector<MidiEv>& out, int blockFrames) {
    out.clear();

    // 1) carry-over flush from previous blocks, starting at frame 0
    pump(out, blockFrames);

    // 2) input events in arrival order
    for (const MidiEv& e : in) {
      uint8_t type = e.status & 0xF0, ch = e.status & 0x0F;

      if (type == 0xB0 && e.d1 == 64) {                 // CC64 sustain pedal
        if (e.d2 >= 64) {
          held = true;
          if (cfg.forwardCC64) out.push_back(e);
        } else {
          held = false;
          for (const Held& h : queue) flushing.push_back(h);
          queue.clear();
          int ff = e.frames;                            // drain from release moment
          while (ff < blockFrames && popFlushed(out, ff)) {}
          if (cfg.forwardCC64) out.push_back(e);
        }
      } else if (isNoteOn(e)) {
        if (keyDown[ch][e.d1] < 250) keyDown[ch][e.d1]++;
        int pi = findPool(e.d1, ch);
        if (pi >= 0) {
          // same pitch re-pressed: target retriggers its existing voice
          pool[pi].keyHeld = true;
          // retrigger of a held/flushing pitch: emit old note-off first (anti-doubling)
          int qi = find(queue, e.d1, ch);
          if (qi >= 0) {
            out.push_back({(uint8_t)(0x80 | ch), e.d1, 0, e.frames});
            queue.erase(queue.begin() + qi);
          } else {
            int fi = find(flushing, e.d1, ch);
            if (fi >= 0) {
              out.push_back({(uint8_t)(0x80 | ch), e.d1, 0, e.frames});
              flushing.erase(flushing.begin() + fi);
            }
          }
        } else {
          // NEW voice: the target only steals voices that already got a note-off,
          // so pedal-held voices occupy the pool forever unless we free one here.
          // Juno-60 HOLD semantics: evict oldest key-released voice, else oldest.
          if (cfg.maxVoices > 0) {
            while ((int)pool.size() >= cfg.maxVoices) {
              int ei = -1;
              for (int i = 0; i < (int)pool.size(); ++i)
                if (!pool[i].keyHeld) { ei = i; break; }
              if (ei < 0) ei = 0;
              out.push_back({(uint8_t)(0x80 | pool[ei].ch), pool[ei].pitch, 0, e.frames});
              int qi = find(queue, pool[ei].pitch, pool[ei].ch);
              if (qi >= 0) queue.erase(queue.begin() + qi); // its deferred off is now sent
              pool.erase(pool.begin() + ei);
            }
          }
          pool.push_back({e.d1, ch, true});
        }
        out.push_back(e);
      } else if (isNoteOff(e)) {
        if (keyDown[ch][e.d1] > 0) keyDown[ch][e.d1]--;
        if (held) {
          if (find(queue, e.d1, ch) < 0)               // dedupe
            queue.push_back({e.d1, ch});
          int pi = findPool(e.d1, ch);
          if (pi >= 0) pool[pi].keyHeld = false;       // hand up, voice sustained
          // note-off suppressed while pedal down
        } else {
          out.push_back(e);
          int pi = findPool(e.d1, ch);
          if (pi >= 0) pool.erase(pool.begin() + pi);
        }
      } else {
        out.push_back(e); // pitch bend, other CC, program change, aftertouch: passthrough
      }
    }

    std::stable_sort(out.begin(), out.end(),
      [](const MidiEv& a, const MidiEv& b){ return a.frames < b.frames; });
  }

  void reset() {
    held = false; queue.clear(); flushing.clear(); pool.clear();
    for (int c = 0; c < 16; ++c) for (int p = 0; p < 128; ++p) keyDown[c][p] = 0;
  }
};
