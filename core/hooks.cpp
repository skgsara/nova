// hooks.cpp
#include "hooks.hpp"
#include <cstdarg>
#include <cstdio>

namespace nova {

void dlog(const DecodeHooks& hooks, LogTopic topic, const char* fmt, ...) {
    if (!hooks.log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = '\0';
    hooks.log(topic, buf);
}

void report(const DecodeHooks& hooks, const char* stage, double fraction) {
    if (hooks.progress) hooks.progress(stage, fraction);
}

void throw_if_cancelled(const DecodeHooks& hooks, const char* where) {
    if (hooks.cancel && hooks.cancel())
        throw DecodeError(DecodeErrorKind::kCancelled,
                          std::string("decode_fax: cancelled during ") +
                              where);
}

}  // namespace nova
