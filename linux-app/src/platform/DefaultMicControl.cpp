#include "DefaultMicControl.hpp"

#include "VirtualMicConfig.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <vector>

namespace wiremic::platform {


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

bool PulseGraphPresent() {
  std::string output;
  return RunCommand("pactl info >/dev/null 2>&1", output);
}

std::string PulseDefault() {
  // Read from "pactl info" rather than "pactl get-default-source": the latter
  // only exists from PulseAudio 15 onwards, and the former has reported the
  // same thing for far longer.
  std::string info;
  if (!RunCommand("pactl info 2>/dev/null", info)) return {};

  std::istringstream stream(info);
  std::string line;
  const std::string prefix = "Default Source:";
  while (std::getline(stream, line)) {
    if (line.rfind(prefix, 0) == 0) return Trim(line.substr(prefix.size()));
  }
  return {};
}

// A real PulseAudio daemon and a PipeWire daemon side by side are two graphs
// that cannot see each other, and "the default microphone" is a separate
// setting in each. Changing only one of them leaves half the programs on the
// machine still pointed at the built-in microphone.
bool PipeWireGraphPresent() {
  std::string output;
  return RunCommand("pw-metadata -n default >/dev/null 2>&1", output);
}

std::string ValueOfMetadataKey(const std::string& text, const std::string& key) {
  // Lines look like:
  //   update: id:0 key:'default.audio.source' value:'{"name":"foo"}' type:...
  std::istringstream stream(text);
  std::string line;
  std::string found;
  while (std::getline(stream, line)) {
    if (line.find("key:'" + key + "'") == std::string::npos) continue;
    const auto nameAt = line.find("\"name\":\"");
    if (nameAt == std::string::npos) continue;
    const auto start = nameAt + 8;
    const auto end = line.find('"', start);
    if (end == std::string::npos) continue;
    found = line.substr(start, end - start);
  }
  return found;
}

std::string PipeWireDefault() {
  std::string output;
  if (!RunCommand("pw-metadata -n default 2>/dev/null", output)) return {};

  // What the person chose comes first; what the session manager settled on is
  // the fallback, so that a machine nobody has ever configured still reports
  // something restorable.
  std::string configured =
      ValueOfMetadataKey(output, "default.configured.audio.source");
  if (!configured.empty()) return configured;
  return ValueOfMetadataKey(output, "default.audio.source");
}

bool SetPipeWireDefault(const std::string& nodeName) {
  std::string output;
  return RunCommand("pw-metadata -n default 0 default.configured.audio.source "
                    "'{\"name\":\"" +
                        nodeName + "\"}' >/dev/null 2>&1",
                    output);
}

bool PipeWireNodeExists(const std::string& nodeName) {
  std::string listing;
  if (!RunCommand("pw-cli ls Node 2>/dev/null", listing)) return false;
  return listing.find(nodeName) != std::string::npos;
}

// The two remembered names travel together in one string so that callers do not
// have to know there are two graphs to put back.
constexpr const char* kPulseTag = "pulse=";
constexpr const char* kPipeWireTag = "pipewire=";

std::string EncodePrevious(const std::string& pulse, const std::string& pipewire) {
  std::string encoded;
  if (!pulse.empty()) encoded += kPulseTag + pulse;
  if (!pipewire.empty()) {
    if (!encoded.empty()) encoded += "\n";
    encoded += kPipeWireTag + pipewire;
  }
  return encoded;
}

void DecodePrevious(const std::string& encoded, std::string* pulse,
                     std::string* pipewire) {
  std::istringstream stream(encoded);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind(kPulseTag, 0) == 0) {
      *pulse = trimmed.substr(std::string(kPulseTag).size());
    } else if (trimmed.rfind(kPipeWireTag, 0) == 0) {
      *pipewire = trimmed.substr(std::string(kPipeWireTag).size());
    } else if (pulse->empty() && !trimmed.empty()) {
      // Written by a version that only knew about PulseAudio.
      *pulse = trimmed;
    }
  }
}

}

bool DefaultMicControl::IsSupported() {
  return PulseGraphPresent() || PipeWireGraphPresent();
}

std::string DefaultMicControl::CurrentDefault() {
  return EncodePrevious(PulseDefault(), PipeWireDefault());
}

bool DefaultMicControl::MakeWireMicDefault(std::string* previous,
                                            std::string* error) {
  const bool onPulse = SourceExists(kPulseSourceName);
  const bool onPipeWire = PipeWireNodeExists(kPipeWireNodeName);

  if (!onPulse && !onPipeWire) {
    if (error) {
      *error =
          "The WireMic microphone is not published right now, so it cannot be "
          "made the default one.";
    }
    return false;
  }

  std::string previousPulse;
  std::string previousPipeWire;
  bool changedAny = false;
  std::string output;

  if (onPulse) {
    const std::string current = PulseDefault();
    if (current != kPulseSourceName) previousPulse = current;
    if (RunCommand(std::string("pactl set-default-source ") + kPulseSourceName +
                       " 2>&1",
                   output)) {
      MoveRecordingStreamsTo(kPulseSourceName);
      changedAny = true;
    } else if (error && error->empty()) {
      *error = output;
    }
  }

  if (onPipeWire) {
    const std::string current = PipeWireDefault();
    if (current != kPipeWireNodeName) previousPipeWire = current;
    if (SetPipeWireDefault(kPipeWireNodeName)) changedAny = true;
  }

  if (!changedAny) {
    if (error && error->empty()) *error = "The audio server refused the change.";
    return false;
  }

  if (previous) *previous = EncodePrevious(previousPulse, previousPipeWire);
  return true;
}

bool DefaultMicControl::RestoreDefault(const std::string& previous,
                                        std::string* error) {
  if (previous.empty()) return false;

  std::string previousPulse;
  std::string previousPipeWire;
  DecodePrevious(previous, &previousPulse, &previousPipeWire);

  bool restoredAny = false;
  std::string output;

  if (!previousPulse.empty() && SourceExists(previousPulse)) {
    if (RunCommand("pactl set-default-source " + previousPulse + " 2>&1",
                   output)) {
      MoveRecordingStreamsTo(previousPulse);
      restoredAny = true;
    }
  }

  if (!previousPipeWire.empty() && PipeWireNodeExists(previousPipeWire)) {
    if (SetPipeWireDefault(previousPipeWire)) restoredAny = true;
  }

  if (!restoredAny && error) {
    *error = "That microphone is no longer connected.";
  }
  return restoredAny;
}

bool DefaultMicControl::WireMicIsDefault() {
  // True only when nothing on the machine is still pointed somewhere else: a
  // half-applied change is exactly the state that makes one program work and
  // the next one look broken.
  const bool onPulse = SourceExists(kPulseSourceName);
  const bool onPipeWire = PipeWireNodeExists(kPipeWireNodeName);
  if (!onPulse && !onPipeWire) return false;

  if (onPulse && PulseDefault() != kPulseSourceName) return false;
  if (onPipeWire && PipeWireDefault() != kPipeWireNodeName) return false;
  return true;
}


}
