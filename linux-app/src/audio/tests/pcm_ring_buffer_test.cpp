#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "Check.hpp"
#include "PcmRingBuffer.hpp"

using wiremic::audio::PcmRingBuffer;

namespace {

bool WrapAround() {
  PcmRingBuffer ring;
  ring.Reset(8);

  const std::vector<int16_t> first{1, 2, 3, 4, 5};
  WIREMIC_CHECK(ring.Write(first.data(), first.size()) == 5);
  WIREMIC_CHECK(ring.available() == 5);

  std::vector<int16_t> out(3, 0);
  WIREMIC_CHECK(ring.Read(out.data(), 3) == 3);
  WIREMIC_CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);

  // Writing past the end of the storage has to wrap into the space the reader
  // just freed rather than stopping at the physical end of the array.
  const std::vector<int16_t> second{6, 7, 8, 9, 10, 11};
  WIREMIC_CHECK(ring.Write(second.data(), second.size()) == 6);
  WIREMIC_CHECK(ring.available() == 8);

  std::vector<int16_t> drained(8, 0);
  WIREMIC_CHECK(ring.Read(drained.data(), 8) == 8);
  for (int16_t i = 0; i < 8; ++i) {
    WIREMIC_CHECK(drained[static_cast<size_t>(i)] == i + 4);
  }
  WIREMIC_CHECK(ring.available() == 0);
  return true;
}

// A full queue accepts what it can and reports how much. It never overwrites
// unread samples, because that would reorder the stream rather than merely
// shorten it.
bool ShortWriteWhenFull() {
  PcmRingBuffer ring;
  ring.Reset(4);

  const std::vector<int16_t> block{1, 2, 3, 4, 5, 6};
  WIREMIC_CHECK(ring.Write(block.data(), block.size()) == 4);
  WIREMIC_CHECK(ring.Write(block.data(), 1) == 0);
  WIREMIC_CHECK(ring.available() == 4);

  std::vector<int16_t> out(4, 0);
  ring.Read(out.data(), 4);
  WIREMIC_CHECK(out[0] == 1 && out[3] == 4);
  return true;
}

bool DiscardTrimsOldest() {
  PcmRingBuffer ring;
  ring.Reset(16);
  std::vector<int16_t> block(10);
  for (int16_t i = 0; i < 10; ++i) block[static_cast<size_t>(i)] = i;
  ring.Write(block.data(), block.size());

  WIREMIC_CHECK(ring.Discard(4) == 4);
  WIREMIC_CHECK(ring.available() == 6);

  std::vector<int16_t> out(6, 0);
  ring.Read(out.data(), 6);
  WIREMIC_CHECK(out[0] == 4);
  WIREMIC_CHECK(out[5] == 9);

  WIREMIC_CHECK(ring.Discard(100) == 0);
  return true;
}

// The property that matters for audio: whatever the consumer reads must be the
// producer's stream with nothing missing, nothing repeated and nothing out of
// order. A single lost or duplicated sample is a step in the waveform, and a
// step is the click the user hears as crackle. The producer here writes an
// ever-increasing ramp so any discontinuity is provable rather than a matter
// of listening.
bool ContiguousUnderConcurrency() {
  PcmRingBuffer ring;
  ring.Reset(4096);

  constexpr size_t kBlock = 240;
  constexpr int32_t kTotal = 8333 * static_cast<int32_t>(kBlock);

  std::atomic<bool> producerDone{false};

  std::thread producer([&] {
    std::vector<int16_t> block(kBlock);
    int32_t counter = 0;
    while (counter < kTotal) {
      for (size_t i = 0; i < kBlock; ++i) {
        block[i] = static_cast<int16_t>((counter + static_cast<int32_t>(i)) &
                                        0x7fff);
      }
      size_t offset = 0;
      while (offset < kBlock) {
        const size_t took = ring.Write(block.data() + offset, kBlock - offset);
        if (took == 0) {
          std::this_thread::yield();
          continue;
        }
        offset += took;
      }
      counter += static_cast<int32_t>(kBlock);
    }
    producerDone.store(true, std::memory_order_release);
  });

  int64_t seen = 0;
  int64_t breaks = 0;
  std::vector<int16_t> out(1024);

  while (true) {
    const size_t got = ring.Read(out.data(), out.size());
    if (got == 0) {
      if (producerDone.load(std::memory_order_acquire) &&
          ring.available() == 0) {
        break;
      }
      std::this_thread::yield();
      continue;
    }
    for (size_t i = 0; i < got; ++i) {
      const int16_t expected = static_cast<int16_t>(seen & 0x7fff);
      if (out[i] != expected) ++breaks;
      ++seen;
    }
  }

  producer.join();

  std::cout << "  concurrent: " << seen << " samples, " << breaks
            << " discontinuities\n";

  WIREMIC_CHECK(seen == kTotal);
  WIREMIC_CHECK(breaks == 0);
  return true;
}

// A slow consumer must lose samples in a counted, bounded way rather than
// corrupting the stream or letting the producer block.
bool SlowConsumerNeverCorrupts() {
  PcmRingBuffer ring;
  ring.Reset(512);

  constexpr int32_t kBlocks = 400;
  constexpr size_t kBlock = 240;

  std::atomic<bool> done{false};
  std::atomic<int64_t> refused{0};
  std::thread producer([&] {
    std::vector<int16_t> block(kBlock, 7);
    for (int32_t b = 0; b < kBlocks; ++b) {
      // Like the audio callback, this producer cannot wait for room.
      const size_t accepted = ring.Write(block.data(), kBlock);
      refused.fetch_add(static_cast<int64_t>(kBlock - accepted),
                        std::memory_order_relaxed);
    }
    done.store(true, std::memory_order_release);
  });

  int64_t seen = 0;
  int64_t wrong = 0;
  std::vector<int16_t> out(64);
  while (!done.load(std::memory_order_acquire) || ring.available() > 0) {
    const size_t got = ring.Read(out.data(), out.size());
    for (size_t i = 0; i < got; ++i) {
      if (out[i] != 7) ++wrong;
      ++seen;
    }
    if (got == 0) std::this_thread::yield();
  }
  producer.join();

  const int64_t expected = static_cast<int64_t>(kBlocks) * kBlock;
  std::cout << "  slow consumer: " << seen << " read, "
            << refused.load() << " refused, " << wrong << " corrupt\n";

  WIREMIC_CHECK(wrong == 0);
  WIREMIC_CHECK(seen + refused.load() == expected);
  return true;
}

}

int main() {
  std::cout << "PCM RING BUFFER (audio callback -> sender thread)\n";

  WIREMIC_CHECK(WrapAround());
  WIREMIC_CHECK(ShortWriteWhenFull());
  WIREMIC_CHECK(DiscardTrimsOldest());
  WIREMIC_CHECK(ContiguousUnderConcurrency());
  WIREMIC_CHECK(SlowConsumerNeverCorrupts());

  std::cout << "PCM_RING_BUFFER_TESTS_PASSED\n";
  return 0;
}
