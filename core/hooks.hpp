// hooks.hpp — the seams between the decode core and whatever drives it.
//
// The core never prints and never reads the environment. What used to be
// the NOVA_DEBUG* stderr stream is now a callback the caller installs (or
// does not): the CLIs map the same environment variables onto it, so a
// debugging session from the shell is unchanged, and the M4 GUI gets the
// same information as a status line rather than as text on a terminal it
// does not have. Progress and cooperative cancellation travel the same
// path: a caller that can be interrupted installs `cancel`, and the core
// asks it at stage boundaries and inside its long loops.
//
// Structured errors: decode_fax used to throw three different
// std::runtime_error strings that a caller could only tell apart by
// reading them. DecodeError carries the kind as a value, so a GUI can
// put "no signal in this recording" and "you cancelled" in different
// dialogs without parsing prose. The message string is unchanged and is
// still what a log should show.
#pragma once
#include <functional>
#include <stdexcept>
#include <string>

namespace nova {

// Which debug stream a message belongs to — one per NOVA_DEBUG* variable
// the core used to read. kInfo is NOVA_DEBUG, kDetail is NOVA_DEBUG_FULL
// (per-line detail), kFold / kProfile / kSeams are their namesakes.
enum class LogTopic {
    kInfo,
    kDetail,
    kFold,
    kProfile,
    kSeams,
};

struct DecodeHooks {
    // Debug/log output. Null (the default) means silent, and costs the
    // core nothing — see dlog().
    std::function<void(LogTopic topic, const std::string& line)> log;
    // Stage progress: `stage` names the stage just entered, `fraction` is
    // 0..1 within it. Stages are decode_fax's numbered sections, in order;
    // the caller weights them (their costs are wildly unequal).
    std::function<void(const char* stage, double fraction)> progress;
    // Cooperative cancellation: return true to stop the decode. Checked at
    // stage boundaries and every few iterations of the long loops; stopping
    // throws DecodeError with kind kCancelled.
    std::function<bool()> cancel;
};

// Why a decode ended without an image.
enum class DecodeErrorKind {
    kEmptyInput,   // no samples at all
    kTooShort,     // too little signal past start_sec to measure
    kNoSignal,     // no fax line comb found (fill, or no signal)
    kTooFewLines,  // a comb, but fewer lines than a picture needs
    kCancelled,    // DecodeHooks::cancel asked to stop
};

class DecodeError : public std::runtime_error {
  public:
    DecodeError(DecodeErrorKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}
    DecodeErrorKind kind() const { return kind_; }

  private:
    DecodeErrorKind kind_;
};

// printf-style log line. A null sink costs one branch: nothing is
// formatted. Filtering by topic is the sink's business, not the core's.
void dlog(const DecodeHooks& hooks, LogTopic topic, const char* fmt, ...);

// Progress report; a null callback costs one branch.
void report(const DecodeHooks& hooks, const char* stage, double fraction);

// Throws DecodeError{kCancelled} when the caller asked to stop. `where`
// names the stage for the error message.
void throw_if_cancelled(const DecodeHooks& hooks, const char* where);

}  // namespace nova
