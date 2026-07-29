#include "Check.hpp"

#include <iostream>

#include "JitterBuffer.hpp"

using namespace wiremic::audio;

namespace {

std::vector<uint8_t> PayloadFor(uint64_t sequence) {
  return {static_cast<uint8_t>(sequence & 0xFF),
          static_cast<uint8_t>((sequence >> 8) & 0xFF)};
}

}

int main() {
  const auto base = std::chrono::steady_clock::now();
  const std::chrono::milliseconds frameInterval{10};

  {
    JitterBuffer buffer(10, 1, 8, 2);

    for (uint64_t seq = 0; seq < 3; ++seq) {
      buffer.Push(seq, PayloadFor(seq), false, base + frameInterval * seq);
    }

    auto outcome0 = buffer.Pop();
    WIREMIC_CHECK(outcome0.result == JitterPopResult::Ready);
    WIREMIC_CHECK(outcome0.payload == PayloadFor(0));

    auto outcome1 = buffer.Pop();
    WIREMIC_CHECK(outcome1.result == JitterPopResult::Ready);
    WIREMIC_CHECK(outcome1.payload == PayloadFor(1));

    auto outcomeNotReady = buffer.Pop();
    WIREMIC_CHECK(outcomeNotReady.result == JitterPopResult::NotReady);

    buffer.Push(3, PayloadFor(3), false, base + frameInterval * 3);
    auto outcome2 = buffer.Pop();
    WIREMIC_CHECK(outcome2.result == JitterPopResult::Ready);
    WIREMIC_CHECK(outcome2.payload == PayloadFor(2));

    std::cout << "IN_ORDER_PLAYOUT_OK\n";
  }

  {
    JitterBuffer buffer(10, 1, 8, 2);
    buffer.Push(0, PayloadFor(0), false, base);
    buffer.Push(1, PayloadFor(1), false, base + frameInterval);
    buffer.Push(3, PayloadFor(3), false, base + frameInterval * 3);
    buffer.Push(4, PayloadFor(4), false, base + frameInterval * 4);

    auto outcome0 = buffer.Pop();
    WIREMIC_CHECK(outcome0.result == JitterPopResult::Ready);
    auto outcome1 = buffer.Pop();
    WIREMIC_CHECK(outcome1.result == JitterPopResult::Ready);
    auto outcome2 = buffer.Pop();
    WIREMIC_CHECK(outcome2.result == JitterPopResult::Loss);
    auto outcome3 = buffer.Pop();
    WIREMIC_CHECK(outcome3.result == JitterPopResult::Ready);
    WIREMIC_CHECK(outcome3.payload == PayloadFor(3));

    std::cout << "PACKET_LOSS_DETECTED_OK\n";
  }

  {
    JitterBuffer buffer(10, 1, 8, 2);
    buffer.Push(0, PayloadFor(0), false, base);
    buffer.Push(1, {}, true, base + frameInterval);
    buffer.Push(2, PayloadFor(2), false, base + frameInterval * 2);

    auto outcome0 = buffer.Pop();
    WIREMIC_CHECK(outcome0.result == JitterPopResult::Ready);
    auto outcome1 = buffer.Pop();
    WIREMIC_CHECK(outcome1.result == JitterPopResult::Silence);
    WIREMIC_CHECK(outcome1.payload.empty());

    std::cout << "DTX_SILENCE_DETECTED_OK\n";
  }

  {
    JitterBuffer buffer(10, 1, 8, 2);
    const int initialDepth = buffer.currentTargetDepthFrames();
    WIREMIC_CHECK(initialDepth == 2);

    auto jitteryBase = base;
    std::vector<int> erraticGapsMs = {10, 80, 5, 95, 8, 90, 12, 100};
    uint64_t seq = 0;
    for (int gap : erraticGapsMs) {
      jitteryBase += std::chrono::milliseconds(gap);
      buffer.Push(seq, PayloadFor(seq), false, jitteryBase);
      ++seq;
    }

    WIREMIC_CHECK(buffer.currentTargetDepthFrames() > initialDepth);
    std::cout << "ADAPTIVE_DEPTH_GROWS_ON_JITTER_OK depth="
              << buffer.currentTargetDepthFrames() << "\n";
  }

  {
    JitterBuffer buffer(10, 1, 8, 2);
    for (uint64_t seq = 0; seq < 100; ++seq) {
      buffer.Push(seq, PayloadFor(seq), false, base + frameInterval * seq);
    }

    WIREMIC_CHECK(buffer.queuedLeadFrames() <= 16);

    auto outcome = buffer.Pop();
    WIREMIC_CHECK(outcome.result == JitterPopResult::Ready);
    WIREMIC_CHECK(outcome.payload != PayloadFor(0));

    int drained = 0;
    while (buffer.Pop().result != JitterPopResult::NotReady && drained < 64) {
      ++drained;
    }
    WIREMIC_CHECK(drained < 16);

    std::cout << "BACKLOG_SKIPS_TO_LIVE_EDGE_OK\n";
  }

  {
    JitterBuffer buffer(10, 1, 8, 2);
    auto now = base;
    uint64_t seq = 0;
    for (int i = 0; i < 500; ++i) {
      buffer.Push(seq, PayloadFor(seq), false, now);
      ++seq;
      now += frameInterval;
      buffer.Pop();
    }
    WIREMIC_CHECK(buffer.queuedLeadFrames() <= 8);
    std::cout << "STEADY_STATE_LEAD_BOUNDED_OK lead="
              << buffer.queuedLeadFrames() << "\n";
  }

  std::cout << "JITTER_BUFFER_TESTS_PASSED\n";
  return 0;
}
