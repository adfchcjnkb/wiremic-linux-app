#include "JitterBuffer.hpp"

#include <algorithm>
#include <cmath>

namespace wiremic::audio {

namespace {
constexpr std::chrono::milliseconds kStableGrowthCooldown{500};
}

JitterBuffer::JitterBuffer(uint8_t frameSizeMs, int minDepthFrames,
                            int maxDepthFrames, int initialDepthFrames)
    : frameSizeMs_(frameSizeMs),
      minDepthFrames_(minDepthFrames),
      maxDepthFrames_(maxDepthFrames),
      targetDepthFrames_(
          std::clamp(initialDepthFrames, minDepthFrames, maxDepthFrames)) {}

void JitterBuffer::UpdateJitterEstimate(
    std::chrono::steady_clock::time_point arrivalTime, uint64_t sequence) {
  if (!haveLastArrival_) {
    haveLastArrival_ = true;
    lastArrivalTime_ = arrivalTime;
    lastArrivalSequence_ = sequence;
    return;
  }

  if (sequence <= lastArrivalSequence_) {
    return;
  }

  const auto seqDelta = sequence - lastArrivalSequence_;
  const double expectedMs = static_cast<double>(seqDelta) * frameSizeMs_;
  const double actualMs =
      std::chrono::duration<double, std::milli>(arrivalTime - lastArrivalTime_)
          .count();
  const double deviation = std::abs(actualMs - expectedMs);

  jitterEstimateMs_ += (deviation - jitterEstimateMs_) / 8.0;

  lastArrivalTime_ = arrivalTime;
  lastArrivalSequence_ = sequence;

  const double growThreshold = targetDepthFrames_ * frameSizeMs_ * 0.5;

  if (jitterEstimateMs_ > growThreshold &&
      targetDepthFrames_ < maxDepthFrames_) {
    ++targetDepthFrames_;
    lastGrowthTime_ = arrivalTime;
    haveLastGrowthTime_ = true;
    return;
  }

  if (targetDepthFrames_ > minDepthFrames_) {
    if (!haveLastGrowthTime_) {
      lastGrowthTime_ = arrivalTime;
      haveLastGrowthTime_ = true;
    } else if (arrivalTime - lastGrowthTime_ >= kStableGrowthCooldown) {
      --targetDepthFrames_;
      lastGrowthTime_ = arrivalTime;
    }
  }
}

void JitterBuffer::Push(uint64_t sequence, std::vector<uint8_t> payload,
                         bool dtx,
                         std::chrono::steady_clock::time_point arrivalTime) {
  if (!initialized_) {
    initialized_ = true;
    nextPlayoutSeq_ = sequence;
  }

  if (sequence < nextPlayoutSeq_) {
    return;
  }

  UpdateJitterEstimate(arrivalTime, sequence);

  buffer_[sequence] = Entry{std::move(payload), dtx};
}

JitterPopOutcome JitterBuffer::Pop() {
  if (!initialized_ || buffer_.empty()) {
    return {JitterPopResult::NotReady, {}};
  }

  const uint64_t highestBuffered = buffer_.rbegin()->first;
  if (highestBuffered < nextPlayoutSeq_ +
                             static_cast<uint64_t>(targetDepthFrames_) - 1) {
    return {JitterPopResult::NotReady, {}};
  }

  auto it = buffer_.find(nextPlayoutSeq_);
  if (it == buffer_.end()) {
    ++nextPlayoutSeq_;
    return {JitterPopResult::Loss, {}};
  }

  Entry entry = std::move(it->second);
  buffer_.erase(it);
  ++nextPlayoutSeq_;

  if (entry.dtx) {
    return {JitterPopResult::Silence, {}};
  }
  return {JitterPopResult::Ready, std::move(entry.payload)};
}

}
