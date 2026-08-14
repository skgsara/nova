// test_ring.cpp — the screamer for the audio ring [docs/05 §2.1], the
// only structure in the project two threads touch at once, and the only
// one written by a REALTIME thread that may not allocate, lock or throw.
//
// A ring that is merely "usually right" is the worst possible bug here:
// it corrupts the audio underneath a decoder whose whole job is detecting
// that the capture chain lost samples, so the failure would arrive
// disguised as a signal problem and be chased in the wrong file.
//
// Claims defended:
//   - single-threaded: what goes in comes out, in order, at every block
//     size, across many wrap-arounds of the buffer;
//   - a full ring drops the NEW samples and counts them EXACTLY — the
//     count is the claim, because the count is what the status line
//     shows and what tells an operator the loss was ours;
//   - written() + overruns() == everything offered, always: no sample is
//     silently unaccounted for;
//   - two real threads, a producer racing a consumer over millions of
//     samples in random block sizes, and every sample read is the one
//     the producer wrote at that position: no reordering, no duplication,
//     no drop that was not counted;
//   - the producer allocates NOTHING. Counted with a global operator new,
//     because "no allocation in the realtime callback" is a rule that
//     holds until someone adds a std::vector to the write path, and then
//     it fails as an audio glitch nobody traces back here.
#include "../live/ring.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// Allocation counter. Armed only around the producer calls, and
// THREAD-LOCAL: a process-wide counter measured the consumer thread's own
// startup allocations too, which is how the first version of this test
// reported one allocation the producer never made. The rule being pinned
// is about one thread, so the instrument has to be about one thread.
thread_local long long t_allocs = 0;
thread_local bool t_counting = false;

}  // namespace

void* operator new(std::size_t n) {
    if (t_counting) t_allocs++;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// --- 1. round-trip at every block size, across many wraps ------------------
void test_roundtrip() {
    std::printf("round-trip and wrap-around\n");
    const std::size_t cap = 1000;
    const std::size_t kWriteBlocks[] = {1, 2, 7, 64, 333, 999, 1000};
    const std::size_t kReadBlocks[] = {1, 3, 64, 500, 1000};
    bool all_ok = true;
    std::size_t worst_wraps = 0;

    for (const std::size_t wb : kWriteBlocks) {
        for (const std::size_t rb : kReadBlocks) {
            nova::AudioRing ring(cap);
            // Twenty times the capacity, so the indices wrap many times and
            // an off-by-one at the seam cannot hide in a single pass.
            const std::size_t total = cap * 20;
            std::vector<float> in(total);
            for (std::size_t i = 0; i < total; i++)
                in[i] = static_cast<float>(i);
            std::vector<float> out;
            out.reserve(total);
            std::vector<float> scratch(rb);
            std::size_t written = 0;
            // The producer offers each block ONCE and never retries — that
            // is the realtime callback's only available behaviour — so the
            // consumer must drain to empty each time or this becomes an
            // overrun test wearing an ordering test's label.
            // The no-progress guard is not defensive coding: the first
            // mutation run of this test HUNG instead of failing, because a
            // ring that computes its free space wrong stores nothing and
            // this loop never ends. A screamer that hangs costs the whole
            // suite its timeout and says less than one that fails.
            bool stalled = false;
            while (written < total) {
                const std::size_t before = written;
                const std::size_t n = std::min(wb, total - written);
                written += ring.write(in.data() + written, n);
                std::size_t drained = 0;
                for (;;) {
                    const std::size_t got = ring.read(scratch.data(), rb);
                    if (got == 0) break;
                    drained += got;
                    out.insert(out.end(), scratch.begin(),
                               scratch.begin() + got);
                }
                if (written == before && drained == 0) {
                    std::printf("    write %zu read %zu: STALLED at %zu — the "
                                "ring accepted nothing into an empty buffer\n",
                                wb, rb, written);
                    stalled = true;
                    break;
                }
            }
            if (stalled) {
                all_ok = false;
                continue;
            }
            bool ok = out.size() == total && ring.overruns() == 0 &&
                      ring.written() == total;
            for (std::size_t i = 0; ok && i < total; i++)
                ok = out[i] == in[i];
            if (!ok) {
                std::printf("    write %zu read %zu: %zu of %zu samples, "
                            "%llu overrun\n",
                            wb, rb, out.size(), total, ring.overruns());
                all_ok = false;
            }
            worst_wraps = total / (cap + 1);
        }
    }
    char msg[160];
    std::snprintf(msg, sizeof msg,
                  "35 block-size pairs, 20000 samples each, ~%zu wraps: order "
                  "and values preserved, no overrun",
                  worst_wraps);
    check(all_ok, msg);
}

// --- 2. the full ring, and the count is exact ------------------------------
void test_overrun() {
    std::printf("overrun accounting\n");
    const std::size_t cap = 100;
    nova::AudioRing ring(cap);
    std::vector<float> in(250);
    for (std::size_t i = 0; i < in.size(); i++) in[i] = static_cast<float>(i);

    const std::size_t stored = ring.write(in.data(), in.size());
    check(stored == cap, "a full ring stores exactly its capacity");
    check(ring.overruns() == in.size() - cap,
          "the shortfall is counted in samples (150 of 250 dropped)");

    // The samples kept are the FIRST ones offered: the drop is at the new
    // end, not a rotation of the buffer.
    std::vector<float> out(cap);
    const std::size_t got = ring.read(out.data(), out.size());
    bool ok = got == cap;
    for (std::size_t i = 0; ok && i < cap; i++) ok = out[i] == in[i];
    check(ok, "the samples kept are the OLD ones; the new ones are what drop");

    // Offering more to a full ring counts every sample, and offering to a
    // ring with room counts none.
    nova::AudioRing r2(10);
    std::vector<float> pad(10, 1.0f);
    r2.write(pad.data(), pad.size());
    r2.write(pad.data(), pad.size());
    check(r2.overruns() == 10, "a wholly rejected write counts every sample");
    check(r2.written() == 10, "written() counts only what was stored");

    // Nothing vanishes: everything offered is either stored or counted.
    nova::AudioRing r3(64);
    std::mt19937 rng(20260814);
    unsigned long long offered = 0;
    std::vector<float> blk(200, 0.5f);
    std::vector<float> sink(40);
    for (int i = 0; i < 5000; i++) {
        const std::size_t n = 1 + rng() % 200;
        offered += n;
        r3.write(blk.data(), n);
        if ((i % 3) == 0) r3.read(sink.data(), sink.size());
    }
    check(r3.written() + r3.overruns() == offered,
          "written + overruns == everything ever offered");
}

// --- 3. two real threads --------------------------------------------------
void test_threaded() {
    std::printf("producer and consumer, two real threads\n");
    // 4 s at 48 kHz, the shipping capacity [§2.1].
    const std::size_t cap = 4 * 48000;
    nova::AudioRing ring(cap);
    const long long total = 4000000;  // ~83 s of 48 kHz audio

    std::atomic<bool> producer_done{false};
    std::atomic<long long> allocs_in_write{0};

    std::thread producer([&] {
        std::mt19937 rng(7);
        std::vector<float> blk(2048);
        long long sent = 0;
        t_allocs = 0;
        t_counting = true;
        while (sent < total) {
            const std::size_t n = static_cast<std::size_t>(
                std::min<long long>(1 + rng() % 2048, total - sent));
            // The value at stream position p is p itself, so a reordered,
            // duplicated or uncounted sample is visible as a value.
            for (std::size_t i = 0; i < n; i++)
                blk[i] = static_cast<float>(sent + static_cast<long long>(i));
            ring.write(blk.data(), n);
            sent += static_cast<long long>(n);
        }
        t_counting = false;
        allocs_in_write.store(t_allocs, std::memory_order_relaxed);
        producer_done.store(true, std::memory_order_release);
    });

    long long read_total = 0;
    long long order_errors = 0;
    long long expect = 0;  // next stream position we expect to see
    std::thread consumer([&] {
        std::mt19937 rng(11);
        std::vector<float> out(4096);
        for (;;) {
            const std::size_t want = 1 + rng() % 4096;
            const std::size_t got = ring.read(out.data(), want);
            for (std::size_t i = 0; i < got; i++) {
                const long long pos = static_cast<long long>(out[i]);
                // Samples may be MISSING (an overrun), but they may never
                // go backwards or repeat: the position must not decrease.
                if (pos < expect) order_errors++;
                expect = pos + 1;
            }
            read_total += static_cast<long long>(got);
            if (got == 0 && producer_done.load(std::memory_order_acquire) &&
                ring.size() == 0)
                break;
        }
    });

    producer.join();
    consumer.join();

    check(order_errors == 0,
          "every sample read is at or after the previous one: no reordering, "
          "no duplication");
    check(read_total + static_cast<long long>(ring.overruns()) == total,
          "samples read + samples counted as overrun == samples offered");
    char msg[160];
    std::snprintf(msg, sizeof msg,
                  "the realtime side allocated %lld times over %lld samples "
                  "(must be 0)",
                  allocs_in_write.load(), total);
    check(allocs_in_write.load() == 0, msg);
    std::printf("    %lld samples through a %zu-sample ring, %llu overrun\n",
                read_total, cap, ring.overruns());
}

// --- 3b. the publish, under a tight handoff -------------------------------
// The ordering test. §3's producer/consumer pass over a 4-second ring
// almost never has the consumer looking at the slot the producer is
// writing, so it did NOT notice when every release/acquire in ring.hpp was
// turned into relaxed — a mutation that survived the first version of this
// file and is the one bug on this machine's weak memory model that would
// reach an operator as "the picture has noise in it".
//
// What makes the difference: a ring small enough that the two threads are
// always on top of each other, blocks of a few samples so `head_` is
// republished constantly, and no sleeping on either side. A slot read
// before the producer's write to it is visible holds a value from a
// PREVIOUS wrap — a stream position `slots` behind — so it shows up in the
// same monotonic check §3 uses, and the small ring is what makes it show
// up at all.
void test_tight_handoff() {
    std::printf("the publish, under a tight handoff (the ordering test)\n");
    const std::size_t cap = 16;
    const long long total = 3000000;
    long long stale_reads = 0;
    long long seen = 0;

    for (int attempt = 0; attempt < 3 && stale_reads == 0; attempt++) {
        nova::AudioRing ring(cap);
        std::atomic<bool> done{false};
        std::thread producer([&] {
            std::vector<float> blk(4);
            long long sent = 0;
            while (sent < total) {
                const std::size_t n =
                    static_cast<std::size_t>(std::min<long long>(4, total - sent));
                for (std::size_t i = 0; i < n; i++)
                    blk[i] = static_cast<float>(sent + static_cast<long long>(i));
                sent += static_cast<long long>(ring.write(blk.data(), n));
            }
            done.store(true, std::memory_order_release);
        });
        std::thread consumer([&] {
            float out[4];
            long long expect = 0;
            for (;;) {
                const std::size_t got = ring.read(out, 4);
                for (std::size_t i = 0; i < got; i++) {
                    const long long pos = static_cast<long long>(out[i]);
                    if (pos < expect) stale_reads++;
                    expect = pos + 1;
                    seen++;
                }
                if (got == 0 && done.load(std::memory_order_acquire) &&
                    ring.size() == 0)
                    break;
            }
        });
        producer.join();
        consumer.join();
    }

    char msg[190];
    std::snprintf(msg, sizeof msg,
                  "%lld samples through a %zu-sample ring, both threads "
                  "spinning: %lld slots read before their write was published "
                  "(must be 0)",
                  seen, cap, stale_reads);
    check(stale_reads == 0, msg);
}

// --- 4. the consumer never outruns the producer ---------------------------
// A read on an empty ring returns nothing and moves nothing: the failure
// this guards is a consumer that reads stale slots when the ring runs dry,
// which on a live capture is the previous second of audio drawn twice.
void test_empty() {
    std::printf("an empty ring\n");
    nova::AudioRing ring(16);
    std::vector<float> out(16, -1.0f);
    check(ring.read(out.data(), out.size()) == 0,
          "reading an empty ring returns 0 samples");
    check(out[0] == -1.0f, "...and writes nothing into the caller's buffer");
    check(ring.size() == 0, "size() is 0");
    const float one = 1.0f;
    ring.write(&one, 1);
    check(ring.size() == 1, "size() follows the producer");
    check(ring.read(out.data(), 16) == 1 && out[0] == 1.0f,
          "a one-sample ring reads back one sample");
    check(ring.read(out.data(), 16) == 0, "and is empty again");
}

}  // namespace

int main() {
    std::printf("=== audio ring [docs/05 §2.1] ===\n");
    test_roundtrip();
    test_overrun();
    test_threaded();
    test_tight_handoff();
    test_empty();
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
