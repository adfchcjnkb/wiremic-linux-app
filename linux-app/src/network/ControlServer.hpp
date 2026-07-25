#pragma once

#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>

#include <unordered_map>

#include "CertificateManager.hpp"
#include "MessageFraming.hpp"
#include "Protocol.hpp"

namespace wiremic::network {

class ControlTcpServer : public QTcpServer {
  Q_OBJECT
 public:
  using QTcpServer::QTcpServer;

 signals:
  void newSslConnection(QSslSocket* socket);

 protected:
  void incomingConnection(qintptr handle) override;
};

class ControlServer : public QObject {
  Q_OBJECT

 public:
  ControlServer(security::CertificateManager& certificateManager,
                quint16 port, QObject* parent = nullptr);

  bool start();
  void stop();
  quint16 port() const;

  void accept(const std::string& requestId,
              const protocol::AudioSession& session);
  void reject(const std::string& requestId, protocol::RejectReason reason);
  void disconnectClient(const std::string& requestId,
                         protocol::DisconnectReason reason);

 signals:
  void connectRequestReceived(protocol::ConnectRequest request,
                               QString peerFingerprint);
  void clientDisconnected(QString requestId,
                           protocol::DisconnectReason reason);
  void errorOccurred(QString message);

 private slots:
  void onNewSslConnection(QSslSocket* socket);
  void onEncrypted();
  void onReadyRead();
  void onSslErrors(const QList<QSslError>& errors);
  void onDisconnected();

 private:
  struct ClientSession {
    QSslSocket* socket{nullptr};
    protocol::MessageFramer framer;
    std::string requestId;
    bool accepted{false};
  };

  void processMessage(QSslSocket* socket, const std::string& message);
  void sendFramed(QSslSocket* socket, const std::string& json);

  security::CertificateManager& certificateManager_;
  quint16 port_;
  ControlTcpServer server_;
  std::unordered_map<QSslSocket*, ClientSession> sessions_;
  std::unordered_map<std::string, QSslSocket*> requestToSocket_;
};

}  // namespace wiremic::network
