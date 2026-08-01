#pragma once

#include <string>

namespace wiremic::platform {

// Hands the virtual microphone to applications that never ask which microphone
// to use -- which is most of them, and all of the ones that only offer "the
// default". Publishing the device is not enough on its own: it has to be the
// one an application gets when it asks for a microphone and does not care
// which.
//
// Restoring is part of the same job, not an afterthought. Making someone's real
// microphone unreachable is only acceptable if putting it back is one button
// away, because WireMic will not always be running when they next need it.
struct DefaultMicControl {
  // False when there is no way to change the default on this system, in which
  // case the buttons that would call the rest of this are not worth showing.
  [[nodiscard]] static bool IsSupported();

  // An opaque handle for whatever is default right now -- an endpoint id on
  // Windows, a source name on Linux. Empty when nothing could be read.
  [[nodiscard]] static std::string CurrentDefault();

  // Makes the WireMic microphone the default and reports what was default
  // before, so it can be handed back later.
  static bool MakeWireMicDefault(std::string* previous, std::string* error);

  // Puts back whatever CurrentDefault() returned earlier.
  static bool RestoreDefault(const std::string& previous, std::string* error);

  // True when the WireMic microphone is the one applications currently get by
  // default.
  [[nodiscard]] static bool WireMicIsDefault();
};

}
