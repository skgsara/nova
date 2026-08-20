// internal_rate.hpp — the internal processing rate shared by the CLI
// tools. 8000 Hz is the fixture rate: every library recording is 8 kHz
// mono 16-bit [docs/06 AUDIO_INPUT_RATE], and it is the rate all input
// is resampled to before demodulation.
#pragma once

namespace nova {
constexpr int kInternalRate = 8000;
}
