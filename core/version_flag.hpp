// version_flag.hpp — `--version`, answered the same way by every binary.
//
// Lives in core/ rather than cli/ because the GUI needs it too, and the
// whole point of the flag is that all five tools answer identically
// [E-GAP-001, Pass E]. `cli/internal_rate.hpp` is the precedent for the
// shape; the difference is that the GUI is a consumer of this one.
#pragma once

#include <nova/version.hpp>

#include <cstdio>
#include <cstring>

namespace nova {

// True when the command line asks for the version, in which case it has
// already been printed on stdout and the caller should exit 0.
//
// Every tool calls this as the FIRST thing in main, ahead of its own
// argument-count check, because all five refuse a short command line and
// `nova-decode --version` would otherwise be answered with a complaint
// about missing positional arguments. Scanning from argv[1] rather than
// from each tool's first optional argument is deliberate for the same
// reason: the flag is about the program, not about a decode.
inline bool handled_version_flag(int argc, char** argv, const char* tool) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--version") != 0) continue;
        // stdout, not stderr: this is an answer, not a complaint, and a
        // packager pipes it.
        std::printf("%s %s\n", tool, nova::kVersion);
        return true;
    }
    return false;
}

}  // namespace nova
