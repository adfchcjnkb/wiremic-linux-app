#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wiremic::platform {

struct AudioInputDevice {
  std::string id;
  std::string name;
  std::string description;
  bool isDefault{false};
};

class AudioDeviceLister {
 public:
  [[nodiscard]] static std::vector<AudioInputDevice> ListInputDevices(
      int timeoutMs = 1500);
};

}  // namespace wiremic::platform
