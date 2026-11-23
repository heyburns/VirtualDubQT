#ifndef VDQTAVISYNTH_H
#define VDQTAVISYNTH_H

#include <QString>
#include <avisynth/avisynth_c.h>

namespace VDQtAviSynth {

struct Api {
#define VDQT_AVS_FUNCTION(name) decltype(&::name) name = nullptr;
#include "VDQtAviSynthFunctions.inc"
#undef VDQT_AVS_FUNCTION
};

// Call only when opening an AviSynth script. The complete table and its shared
// library stay resident for the lifetime of the process, including audio and
// preview workers that borrow the clip. No script environment is shared here.
bool ensureLoaded(QString *error);
const Api& api(); // Requires a successful ensureLoaded() before using the API.

} // namespace VDQtAviSynth

#endif
