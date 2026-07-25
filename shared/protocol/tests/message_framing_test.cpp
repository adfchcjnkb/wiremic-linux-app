#include "Check.hpp"
#include <iostream>

#include "MessageFraming.hpp"

using namespace wiremic::protocol;

int main() {
  {
    MessageFramer framer;
    const std::string payload = "hello control channel";
    const auto framed = FrameMessage(payload);
    framer.Feed(framed.data(), framed.size());
    auto message = framer.NextMessage();
    WIREMIC_CHECK(message.has_value());
    WIREMIC_CHECK(*message == payload);
    WIREMIC_CHECK(!framer.NextMessage().has_value());
    std::cout << "SINGLE_MESSAGE_OK\n";
  }

  {
    MessageFramer framer;
    const auto framedA = FrameMessage("first");
    const auto framedB = FrameMessage("second-message");
    const std::string combined = framedA + framedB;

    for (size_t i = 0; i < combined.size(); i += 3) {
      const size_t chunkLen = std::min<size_t>(3, combined.size() - i);
      framer.Feed(combined.data() + i, chunkLen);
    }

    auto first = framer.NextMessage();
    auto second = framer.NextMessage();
    WIREMIC_CHECK(first.has_value() && *first == "first");
    WIREMIC_CHECK(second.has_value() && *second == "second-message");
    WIREMIC_CHECK(!framer.NextMessage().has_value());
    std::cout << "FRAGMENTED_MULTI_MESSAGE_OK\n";
  }

  {
    MessageFramer framer;
    bool threw = false;
    std::string oversized(2 * 1024 * 1024, 'x');
    try {
      auto framed = FrameMessage(oversized);
      (void)framed;
    } catch (const std::length_error&) {
      threw = true;
    }
    WIREMIC_CHECK(threw);
    std::cout << "OVERSIZE_REJECTED_OK\n";
  }

  std::cout << "FRAMING_TESTS_PASSED\n";
  return 0;
}
