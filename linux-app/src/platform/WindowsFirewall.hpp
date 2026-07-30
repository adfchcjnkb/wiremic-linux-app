#pragma once

#ifdef _WIN32

#include <QObject>
#include <QString>

#include <functional>

namespace wiremic::platform {

// Windows Firewall silently drops unsolicited inbound datagrams for any program
// that has no rule, and on a network it has classified as Public it does so
// without ever prompting. Discovery is unsolicited inbound UDP by definition, so
// with no rule the phone's broadcasts never reach this machine: the phone can
// hear the desktop, the desktop never hears the phone, and neither can open the
// control channel. The installer adds the rules while it is already elevated;
// this is the recovery path for a portable copy, a moved installation, or a
// rule someone removed.
class WindowsFirewall {
 public:
  enum class Status {
    Allowed,
    Blocked,
    Unknown,
  };

  // Asks whether an enabled inbound rule exists for this executable, without
  // blocking. Querying the firewall means running netsh, which can take a
  // noticeable moment on a machine with a large rule set, and doing that
  // synchronously would freeze the window while it ran. The callback lands on
  // context's thread and is dropped if context dies first.
  static void CheckAsync(QObject* context, std::function<void(Status)> done);

  // Prompts for administrator rights and installs the rules. Returns false if
  // the prompt was declined or the rules could not be written; error is filled
  // with something worth showing the user. This one is synchronous because the
  // user has just pressed a button and is waiting for the UAC dialog anyway.
  static bool Repair(QString* error);
};

}

#endif
