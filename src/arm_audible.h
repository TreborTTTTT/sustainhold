// arm_audible.h — force a known-audible synth state through the proxy.
// TAL LX-V2's built-in default (no preset dir found, progs=1) has a near-zero
// master volume and a zero-sustain/zero-release envelope, which renders
// silence. Real hosts load factory presets; our headless harness sets the
// same kind of state explicitly via the proxy's parameter forwarding.
#pragma once
#include <cstring>
#include "aeffectx.h"

inline void armAudible(AEffect* fx) {
  for (int p = 0; p < fx->numParams; ++p) {
    char nm[64] = {0};
    fx->dispatcher(fx, effGetParamName, p, 0, nm, 0);
    if (!strcmp(nm, "master volume")) fx->setParameter(fx, p, 0.8f);
    else if (!strcmp(nm, "attack"))   fx->setParameter(fx, p, 0.05f);
    else if (!strcmp(nm, "decay"))    fx->setParameter(fx, p, 0.5f);
    else if (!strcmp(nm, "sustain"))  fx->setParameter(fx, p, 0.8f);
    else if (!strcmp(nm, "release"))  fx->setParameter(fx, p, 0.2f);
  }
}
