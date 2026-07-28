#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

namespace wiremic::audio {

enum class JitterPopResult { NotReady, Loss, Silence, Ready };

struct JitterPopOutcome {
  JitterPopResult result{JitterPopResult::NotReady};
  std::vector<uint8_t> payload;
};

class JitterBuffer {
 public:
  explicit JitterBuffer(uint8_t frameSizeMs, int minDepthFrames = 1,
                         int maxDepthFrames = 8, int initialDepthFrames = 2);

  void Push(uint64_t sequence, std::vector<uint8_t> payload, bool dtx,
            std::chrono::steady_clock::time_point arrivalTime);
  JitterPopOutcome Pop();

  [[nodiscard]] int currentTargetDepthFrames() const {
    return targetDepthFrames_;
  }
  [[nodiscard]] size_t bufferedCount() const { return buffer_.size(); }

 private:
  struct Entry {
    std::vector<uint8_t> payload;
    bool dtx{false};
  };

  void UpdateJitterEstimate(std::chrono::steady_clock::time_point arrivalTime,
                             uint64_t sequence);

  std::map<uint64_t, Entry> buffer_;
  uint64_t nextPlayoutSeq_{0};
  bool initialized_{false};

  uint8_t frameSizeMs_;
  int minDepthFrames_;
  int maxDepthFrames_;
  int targetDepthFrames_;

  bool haveLastArrival_{false};
  std::chrono::steady_clock::time_point lastArrivalTime_;
  uint64_t lastArrivalSequence_{0};
  double jitterEstimateMs_{0.0};
  std::chrono::steady_clock::time_point lastGrowthTime_;
  bool haveLastGrowthTime_{false};
};

}
