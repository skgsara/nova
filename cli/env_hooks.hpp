// env_hooks.hpp — map the NOVA_DEBUG* environment variables onto a
// DecodeHooks log sink for the CLIs. The core no longer reads the
// environment; the shell debugging workflow keeps working because the
// CLIs install this sink when any of the variables is set.
//
// Mapping (one topic per variable, core/hooks.hpp):
//   NOVA_DEBUG         -> kInfo     (stage summaries)
//   NOVA_DEBUG_FULL    -> kDetail   (per-line detail)
//   NOVA_DEBUG_FOLD    -> kFold
//   NOVA_DEBUG_PROFILE -> kProfile
//   NOVA_DEBUG_SEAMS   -> kSeams
// One deliberate difference from the env-var era: kDetail used to print
// only when NOVA_DEBUG was also set (it was a modifier of the outer
// check); as a topic it stands alone, so NOVA_DEBUG_FULL=1 by itself now
// shows the per-line detail.
#pragma once
#include "../core/hooks.hpp"
#include <cstdio>
#include <cstdlib>

namespace nova {

inline DecodeHooks hooks_from_env() {
    DecodeHooks h;
    static const struct {
        const char* env;
        LogTopic topic;
    } kMap[] = {
        {"NOVA_DEBUG", LogTopic::kInfo},
        {"NOVA_DEBUG_FULL", LogTopic::kDetail},
        {"NOVA_DEBUG_FOLD", LogTopic::kFold},
        {"NOVA_DEBUG_PROFILE", LogTopic::kProfile},
        {"NOVA_DEBUG_SEAMS", LogTopic::kSeams},
    };
    bool any = false;
    for (const auto& m : kMap)
        if (std::getenv(m.env)) any = true;
    if (!any) return h;
    h.log = [](LogTopic topic, const std::string& line) {
        for (const auto& m : kMap)
            if (m.topic == topic && std::getenv(m.env)) {
                std::fprintf(stderr, "%s\n", line.c_str());
                return;
            }
    };
    return h;
}

}  // namespace nova
