#ifdef _WIN32

#include "WindowsFirewall.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <windows.h>

#include <shellapi.h>

#include <string>
#include <utility>

namespace wiremic::platform {

namespace {

constexpr const char* kUdpRule = "WireMic (UDP-In)";
constexpr const char* kTcpRule = "WireMic (TCP-In)";

QString ExecutablePath() {
  return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// netsh reports a missing rule with a non-zero exit code, so the answer needs
// no output parsing. Its wording is localised; the exit code is not.
void RuleExistsAsync(QObject* context, const char* name,
                      std::function<void(bool)> done) {
  auto* netsh = new QProcess(context);
  netsh->setProgram(QStringLiteral("netsh"));
  netsh->setArguments({QStringLiteral("advfirewall"), QStringLiteral("firewall"),
                       QStringLiteral("show"), QStringLiteral("rule"),
                       QStringLiteral("name=%1").arg(QLatin1String(name))});
  netsh->setProcessChannelMode(QProcess::MergedChannels);

  // finished and errorOccurred can both arrive; the callback must run once.
  auto answered = std::make_shared<bool>(false);
  auto respond = [netsh, answered, done](bool exists) {
    if (*answered) return;
    *answered = true;
    netsh->deleteLater();
    done(exists);
  };

  QObject::connect(
      netsh, &QProcess::finished, context,
      [respond](int exitCode, QProcess::ExitStatus status) {
        respond(status == QProcess::NormalExit && exitCode == 0);
      });
  QObject::connect(netsh, &QProcess::errorOccurred, context,
                   [respond](QProcess::ProcessError) { respond(false); });

  netsh->start();
}

}

void WindowsFirewall::CheckAsync(QObject* context,
                                  std::function<void(Status)> done) {
  if (!context || !done) return;

  // netsh being absent is not the same as the rules being absent, and claiming
  // "blocked" would send the user chasing a problem they cannot fix.
  if (QStandardPaths::findExecutable(QStringLiteral("netsh")).isEmpty()) {
    done(Status::Unknown);
    return;
  }

  QPointer<QObject> guard(context);
  RuleExistsAsync(context, kUdpRule, [guard, done](bool udpExists) {
    if (!guard) return;
    if (!udpExists) {
      done(Status::Blocked);
      return;
    }
    RuleExistsAsync(guard, kTcpRule, [guard, done](bool tcpExists) {
      if (!guard) return;
      done(tcpExists ? Status::Allowed : Status::Blocked);
    });
  });
}

bool WindowsFirewall::Repair(QString* error) {
  const QString target = ExecutablePath();

  // The commands go through a batch file rather than a command line so that
  // the paths and rule names, both of which contain spaces, do not have to
  // survive two rounds of quoting through cmd.exe.
  const QString scriptPath =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath(QStringLiteral("wiremic-firewall.bat"));

  QFile script(scriptPath);
  if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate |
                   QIODevice::Text)) {
    if (error) {
      *error = QStringLiteral("Could not write %1.").arg(scriptPath);
    }
    return false;
  }

  {
    QTextStream out(&script);
    out << "@echo off\r\n";
    for (const char* name : {kUdpRule, kTcpRule}) {
      out << "netsh advfirewall firewall delete rule name=\""
          << QLatin1String(name) << "\" >nul 2>&1\r\n";
    }
    out << "netsh advfirewall firewall add rule name=\""
        << QLatin1String(kUdpRule) << "\" dir=in action=allow program=\""
        << target << "\" protocol=UDP profile=any enable=yes\r\n";
    out << "if errorlevel 1 exit /b 1\r\n";
    out << "netsh advfirewall firewall add rule name=\""
        << QLatin1String(kTcpRule) << "\" dir=in action=allow program=\""
        << target << "\" protocol=TCP profile=any enable=yes\r\n";
    out << "if errorlevel 1 exit /b 1\r\n";
    out << "exit /b 0\r\n";
  }
  script.close();

  const std::wstring script16 =
      QDir::toNativeSeparators(scriptPath).toStdWString();

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.lpVerb = L"runas";
  info.lpFile = script16.c_str();
  info.nShow = SW_HIDE;

  if (!::ShellExecuteExW(&info)) {
    const DWORD code = ::GetLastError();
    QFile::remove(scriptPath);
    if (error) {
      *error = code == ERROR_CANCELLED
                   ? QStringLiteral(
                         "Administrator permission is needed to change the "
                         "firewall, and the request was declined.")
                   : QStringLiteral(
                         "Windows refused to run the repair (error %1).")
                         .arg(static_cast<uint>(code));
    }
    return false;
  }

  DWORD exitCode = 1;
  if (info.hProcess) {
    ::WaitForSingleObject(info.hProcess, 30000);
    ::GetExitCodeProcess(info.hProcess, &exitCode);
    ::CloseHandle(info.hProcess);
  }
  QFile::remove(scriptPath);

  if (exitCode != 0) {
    if (error) {
      *error = QStringLiteral(
                   "The firewall rules could not be added. Allow %1 through "
                   "Windows Firewall for private and public networks.")
                   .arg(target);
    }
    return false;
  }

  return true;
}

}

#endif
