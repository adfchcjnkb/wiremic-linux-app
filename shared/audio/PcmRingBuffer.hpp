#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace wiremic::audio {

// A single-producer single-consumer queue of mono samples.
//
// The producer is the device audio callback, which runs on a thread with a
// hard deadline: if it takes longer than one buffer period the platform drops
// the next block of microphone input, and a dropped block is a step
// discontinuity in the waveform -- the click that is heard as crackle. So the
// callback is only allowed to memcpy into this queue, and every other stage of
// the pipeline runs on an ordinary thread that is free to be late.
//
// The two indices are the only shared state. The producer owns writeIndex_ and
// only reads readIndex_; the consumer owns readIndex_ and only reads
// writeIndex_. No locks, no allocation, no syscalls on either side.
class PcmRingBuffer {
 public:
  void Reset(size_t capacitySamples) {
    // One slot stays empty so that "write == read" always means empty and
    // never means completely full.
    storage_.assign(capacitySamples + 1, 0);
    writeIndex_.store(0, std::memory_order_relaxed);
    readIndex_.store(0, std::memory_order_relaxed);
  }

  [[nodiscard]] size_t capacity() const {
    return storage_.empty() ? 0 : storage_.size() - 1;
  }

  // Producer side. Returns the number of samples accepted, which is short of
  // count only when the consumer has stalled long enough to fill the queue.
  // Whether a short write is a loss is the caller's decision -- a caller that
  // can retry has lost nothing -- so the queue does not account for it.
  size_t Write(const int16_t* samples, size_t count) {
    if (storage_.empty() || count == 0) return 0;

    const size_t size = storage_.size();
    const size_t write = writeIndex_.load(std::memory_order_relaxed);
    const size_t read = readIndex_.load(std::memory_order_acquire);

    const size_t free = (read + size - write - 1) % size;
    const size_t take = count < free ? count : free;
    if (take == 0) return 0;

    const size_t firstRun = size - write < take ? size - write : take;
    std::memcpy(storage_.data() + write, samples, firstRun * sizeof(int16_t));
    if (take > firstRun) {
      std::memcpy(storage_.data(), samples + firstRun,
                  (take - firstRun) * sizeof(int16_t));
    }

    writeIndex_.store((write + take) % size, std::memory_order_release);
    return take;
  }

  // Consumer side.
  [[nodiscard]] size_t available() const {
    if (storage_.empty()) return 0;
    const size_t size = storage_.size();
    const size_t write = writeIndex_.load(std::memory_order_acquire);
    const size_t read = readIndex_.load(std::memory_order_relaxed);
    return (write + size - read) % size;
  }

  size_t Read(int16_t* out, size_t count) {
    if (storage_.empty() || count == 0) return 0;

    const size_t size = storage_.size();
    const size_t read = readIndex_.load(std::memory_order_relaxed);
    const size_t ready = available();
    const size_t take = count < ready ? count : ready;
    if (take == 0) return 0;

    const size_t firstRun = size - read < take ? size - read : take;
    std::memcpy(out, storage_.data() + read, firstRun * sizeof(int16_t));
    if (take > firstRun) {
      std::memcpy(out + firstRun, storage_.data(),
                  (take - firstRun) * sizeof(int16_t));
    }

    readIndex_.store((read + take) % size, std::memory_order_release);
    return take;
  }

  // Consumer side. Throws away the oldest samples; used to recover the latency
  // budget after a stall rather than letting a backlog play out late.
  size_t Discard(size_t count) {
    if (storage_.empty() || count == 0) return 0;
    const size_t size = storage_.size();
    const size_t ready = available();
    const size_t take = count < ready ? count : ready;
    if (take == 0) return 0;
    const size_t read = readIndex_.load(std::memory_order_relaxed);
    readIndex_.store((read + take) % size, std::memory_order_release);
    return take;
  }

 private:
  std::vector<int16_t> storage_;
  std::atomic<size_t> writeIndex_{0};
  std::atomic<size_t> readIndex_{0};
};

}
