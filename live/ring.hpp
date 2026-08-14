// ring.hpp — the audio ring between the RtAudio callback and the live
// decode thread [docs/05 §2.1]. Single producer (thread 1, realtime),
// single consumer (thread 2), fixed capacity, no allocation after
// construction, no lock, no throw on the producer side.
//
// **Overrun is counted, never silently dropped.** A dropped block is
// exactly the kind of capture-chain sample loss the decoder spent
// sessions 9-12 learning to detect (the 1270-sample loss in
// himawari-jmh-warp is one), and manufacturing that loss inside our own
// buffer while reporting a clean timebase would be dishonest. The count
// is in SAMPLES rather than blocks, because a block is the sound card's
// unit and the picture's unit is the sample.
//
// Capacity is 4 s at the capture rate [§2.1] — orders of magnitude more
// than a callback period, so an overrun means thread 2 is wedged, not
// that the buffer was tight.
//
// The memory ordering, since this is the project's first lock-free
// structure and getting it wrong would be invisible on x86: the producer
// publishes with a release store on `head_` after writing the samples,
// the consumer acquires `head_` before reading them, and symmetrically
// for `tail_`. Each index is written by exactly one thread, so no
// read-modify-write and no CAS is needed anywhere.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace nova {

class AudioRing {
public:
    // `capacity` samples are storable; one extra slot is allocated so that
    // full and empty are distinguishable without a separate count.
    explicit AudioRing(std::size_t capacity)
        : buf_(capacity + 1, 0.0f), cap_(capacity) {}

    std::size_t capacity() const { return cap_; }

    // --- producer side: thread 1, realtime ---------------------------------
    // Writes what fits and counts the rest as overrun. Never allocates,
    // never blocks, never throws. Returns the number of samples stored.
    //
    // The samples DROPPED are the new ones, not the old ones: the buffered
    // audio is already part of a transmission being decoded, and discarding
    // it to make room would move the loss from the newest signal (where it
    // is a gap the tracker re-acquires through) to the middle of a picture
    // already being drawn.
    std::size_t write(const float* in, std::size_t n) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t slots = buf_.size();
        // Free slots, keeping one empty so head == tail means empty.
        const std::size_t free = (tail + slots - head - 1) % slots;
        const std::size_t take = n < free ? n : free;
        std::size_t h = head;
        for (std::size_t i = 0; i < take; i++) {
            buf_[h] = in[i];
            if (++h == slots) h = 0;
        }
        head_.store(h, std::memory_order_release);
        if (take < n)
            overrun_.fetch_add(static_cast<unsigned long long>(n - take),
                               std::memory_order_relaxed);
        written_.fetch_add(static_cast<unsigned long long>(take),
                           std::memory_order_relaxed);
        return take;
    }

    // --- consumer side: thread 2 -------------------------------------------
    // Reads up to `n` samples; returns how many were available.
    std::size_t read(float* out, std::size_t n) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t slots = buf_.size();
        const std::size_t avail = (head + slots - tail) % slots;
        const std::size_t take = n < avail ? n : avail;
        std::size_t t = tail;
        for (std::size_t i = 0; i < take; i++) {
            out[i] = buf_[t];
            if (++t == slots) t = 0;
        }
        tail_.store(t, std::memory_order_release);
        return take;
    }

    // Samples readable right now. A lower bound from the consumer's view:
    // the producer may add more between this call and the read.
    std::size_t size() const {
        const std::size_t slots = buf_.size();
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return (head + slots - tail) % slots;
    }

    // Samples the producer could not store, cumulative. Shown, not hidden
    // [§2.1]; the status line is where it surfaces.
    unsigned long long overruns() const {
        return overrun_.load(std::memory_order_relaxed);
    }
    // Samples actually stored, cumulative — the denominator that makes an
    // overrun count mean something.
    unsigned long long written() const {
        return written_.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    std::size_t cap_;
    std::atomic<std::size_t> head_{0};  // producer writes, consumer reads
    std::atomic<std::size_t> tail_{0};  // consumer writes, producer reads
    std::atomic<unsigned long long> overrun_{0};
    std::atomic<unsigned long long> written_{0};
};

}  // namespace nova
