#include "DefaultMicControl.hpp"

#include "VirtualMicConfig.hpp"

#ifdef _WIN32
#include "WindowsVirtualMic.hpp"
#else
#include <array>
#include <cstdio>
#include <sstream>
#include <vector>
#endif

namespace wiremic::platform {

#ifdef _WIN32

bool DefaultMicControl::IsSupported() {
  return WindowsVirtualMic::IsCableInstalled();
}

std::string DefaultMicControl::CurrentDefault() {
  return WindowsVirtualMic::CurrentDefaultCaptureId();
}

bool DefaultMicControl::MakeWireMicDefault(std::string* previous,
                                            std::string* error) {
  return WindowsVirtualMic::MakeCableDefaultCaptureDevice(previous, error);
}

bool DefaultMicControl::RestoreDefault(const std::string& previous,
                                        std::string* error) {
  if (previous.empty()) return false;
  return WindowsVirtualMic::SetDefaultCaptureById(previous, error);
}

bool DefaultMicControl::WireMicIsDefault() {
  return WindowsVirtualMic::IsCableDefaultCaptureDevice();
}

#else

namespace {

bool RunCommand(const std::string& command, std::string& output) {
  output.clear();
  FILE* pipe = ::popen(command.c_str(), "r");
  if (!pipe) return false;

  std::array<char, 512> buffer{};
  while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output += buffer.data();
  }
  return ::pclose(pipe) == 0;
}

std::string Trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Every recording stream except the one the virtual microphone runs on its own
// behalf. Moving that one would disconnect the device from the sink it reads
// from, which is the opposite of helpful.
std::vector<std::string> MovableRecordingStreams(const std::string& exceptOn) {
  std::string listing;
  if (!RunCommand("pactl list source-outputs short 2>/dev/null", listing)) {
    return {};
  }

  std::vector<std::string> indices;
  std::istringstream stream(listing);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("module-remap-source") != std::string::npos) continue;

    std::istringstream fields(line);
    std::string index;
    std::string source;
    if (!(fields >> index >> source)) continue;
    if (!exceptOn.empty() && source == exceptOn) continue;
    indices.push_back(index);
  }
  return indices;
}

// Applications that were already recording when the default changed keep the
// device they opened; nothing tells them to look again. Moving their streams is
// what turns "the default changed" into "my voice comes out of this program
// now", without asking anyone to restart anything.
void MoveRecordingStreamsTo(const std::string& sourceName) {
  std::string output;
  for (const auto& index : MovableRecordingStreams(sourceName)) {
    RunCommand("pactl move-source-output " + index + " " + sourceName +
                   " 2>/dev/null",
               output);
  }
}

bool SourceExists(const std::string& name) {
  std::string listing;
  if (!RunCommand("pactl list sources short 2>/dev/null", listing)) return false;
  return listing.find(name) != std::string::npos;
}

}

bool DefaultMicControl::IsSupported() {
  std::string output;
  return RunCommand("pactl info >/dev/null 2>&1", output);
}

std::string DefaultMicControl::CurrentDefault() {
  // Read from "pactl info" rather than "pactl get-default-source": the latter
  // only exists from PulseAudio 15 onwards, and the former has reported the
  // same thing for far longer.
  std::string info;
  if (!RunCommand("pactl info 2>/dev/null", info)) return {};

  std::istringstream stream(info);
  std::string line;
  const std::string prefix = "Default Source:";
  while (std::getline(stream, line)) {
    if (line.rfind(prefix, 0) == 0) {
      return Trim(line.substr(prefix.size()));
    }
  }
  return {};
}

bool DefaultMicControl::MakeWireMicDefault(std::string* previous,
                                            std::string* error) {
  if (!SourceExists(kPulseSourceName)) {
    if (error) {
      *error =
          "The WireMic microphone is not published right now, so it cannot be "
          "made the default one.";
    }
    return false;
  }

  const std::string current = CurrentDefault();
  if (previous && current != kPulseSourceName) *previous = current;

  std::string output;
  if (!RunCommand(std::string("pactl set-default-source ") + kPulseSourceName +
                      " 2>&1",
                  output)) {
    if (error) {
      *error = output.empty() ? "The audio server refused the change." : output;
    }
    return false;
  }

  MoveRecordingStreamsTo(kPulseSourceName);
  return true;
}

bool DefaultMicControl::RestoreDefault(const std::string& previous,
                                        std::string* error) {
  if (previous.empty()) return false;

  if (!SourceExists(previous)) {
    if (error) *error = "That microphone is no longer connected.";
    return false;
  }

  std::string output;
  if (!RunCommand("pactl set-default-source " + previous + " 2>&1", output)) {
    if (error) {
      *error = output.empty() ? "The audio server refused the change." : output;
    }
    return false;
  }

  MoveRecordingStreamsTo(previous);
  return true;
}

bool DefaultMicControl::WireMicIsDefault() {
  return CurrentDefault() == kPulseSourceName;
}

#endif

}
