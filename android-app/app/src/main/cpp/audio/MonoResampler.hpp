#pragma once

#include <cstdint>
#include <vector>

namespace wiremic::audio {

class MonoResampler {
 public:
  void Reset(int32_t inputRate, int32_t outputRate) {
    inputRate_ = inputRate;
    outputRate_ = outputRate;
    cursor_ = 0.0;
    haveHistory_ = false;
    history_ = 0;
  }

  [[nodiscard]] bool passthrough() const { return inputRate_ == outputRate_; }

  void Process(const int16_t* input, size_t count, std::vector<int16_t>& out) {
    if (count == 0) return;

    if (passthrough()) {
      out.insert(out.end(), input, input + count);
      return;
    }

    work_.clear();
    work_.reserve(count + 1);
    if (haveHistory_) work_.push_back(history_);
    work_.insert(work_.end(), input, input + count);

    const double ratio =
        static_cast<double>(inputRate_) / static_cast<double>(outputRate_);
    const double limit = static_cast<double>(work_.size()) - 1.0;

    while (cursor_ < limit) {
      const size_t index = static_cast<size_t>(cursor_);
      const double fraction = cursor_ - static_cast<double>(index);
      const double a = work_[index];
      const double b = work_[index + 1];
      out.push_back(static_cast<int16_t>(a + (b - a) * fraction));
      cursor_ += ratio;
    }

    cursor_ -= limit;
    if (cursor_ < 0.0) cursor_ = 0.0;
    history_ = work_.back();
    haveHistory_ = true;
  }

 private:
  int32_t inputRate_{48000};
  int32_t outputRate_{48000};
  double cursor_{0.0};
  bool haveHistory_{false};
  int16_t history_{0};
  std::vector<int16_t> work_;
};

}
