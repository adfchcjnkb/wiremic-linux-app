#include "ControlServer.hpp"

#include <QCryptographicHash>

namespace wiremic::network {

namespace {

QSslCertificate CertificateFromPem(const std::string& pem) {
  const QByteArray bytes = QByteArray::fromStdString(pem);
  return QSslCertificate(bytes, QSsl::Pem);
}

QSslKey KeyFromPem(const std::string& pem) {
  const QByteArray bytes = QByteArray::fromStdString(pem);
  return QSslKey(bytes, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
}

QString FingerprintOf(const QSslCertificate& certificate) {
  return QStringLiteral("sha256:") +
         certificate.digest(QCryptographicHash::Sha256).toHex();
}

}  // namespace

void ControlTcpServer::incomingConnection(qintptr handle) {
  auto* socket = new QSslSocket(this);
  if (!socket->setSocketDescriptor(handle)) {
    delete socket;
    return;
  }
  emit newSslConnection(socket);
}

ControlServer::ControlServer(security::CertificateManager& certificateManager,
                              quint16 port, QObject* parent)
    : QObject(parent),
      certificateManager_(certificateManager),
      port_(port),
      server_(this) {
  connect(&server_, &ControlTcpServer::newSslConnection, this,
          &ControlServer::onNewSslConnection);
}

bool ControlServer::start() {
  if (!server_.listen(QHostAddress::Any, port_)) {
    emit errorOccurred(QStringLiteral("Failed to listen on port %1: %2")
                            .arg(port_)
                            .arg(server_.errorString()));
    return false;
  }
  port_ = server_.serverPort();
  return true;
}

void ControlServer::stop() {
  for (auto& [socket, session] : sessions_) {
    socket->disconnectFromHost();
  }
  sessions_.clear();
  requestToSocket_.clear();
  server_.close();
}

quint16 ControlServer::port() const { return port_; }

void ControlServer::onNewSslConnection(QSslSocket* socket) {
  const auto& localCert = certificateManager_.localCertificate();
  socket->setLocalCertificate(CertificateFromPem(localCert.certificatePem));
  socket->setPrivateKey(KeyFromPem(localCert.privateKeyPem));
  socket->setPeerVerifyMode(QSslSocket::VerifyPeer);

  connect(socket, &QSslSocket::encrypted, this, &ControlServer::onEncrypted);
  connect(socket, &QSslSocket::readyRead, this, &ControlServer::onReadyRead);
  connect(socket,
          QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
          this, &ControlServer::onSslErrors);
  connect(socket, &QSslSocket::disconnected, this,
          &ControlServer::onDisconnected);

  ClientSession session;
  session.socket = socket;
  sessions_.emplace(socket, std::move(session));

  socket->startServerEncryption();
}

void ControlServer::onEncrypted() {}

void ControlServer::onSslErrors(const QList<QSslError>& errors) {
  auto* socket = qobject_cast<QSslSocket*>(sender());
  if (socket) socket->ignoreSslErrors(errors);
}

void ControlServer::onReadyRead() {
  auto* socket = qobject_cast<QSslSocket*>(sender());
  if (!socket) return;

  auto it = sessions_.find(socket);
  if (it == sessions_.end()) return;

  const QByteArray data = socket->readAll();
  it->second.framer.Feed(data.constData(), static_cast<size_t>(data.size()));

  while (auto message = it->second.framer.NextMessage()) {
    processMessage(socket, *message);
  }
}

void ControlServer::processMessage(QSslSocket* socket,
                                    const std::string& message) {
  auto& session = sessions_.at(socket);
  const auto type = protocol::PeekMessageType(message);

  if (type == protocol::ControlMessageType::KeepAlive) {
    auto keepAlive = protocol::ParseKeepAlive(message);
    if (!keepAlive) return;
    protocol::KeepAliveAck ack;
    ack.sequence = keepAlive->sequence;
    sendFramed(socket, protocol::ToJson(ack));
    return;
  }

  if (type == protocol::ControlMessageType::Disconnect) {
    auto disconnect = protocol::ParseDisconnect(message);
    const auto reason = disconnect ? disconnect->reason
                                    : protocol::DisconnectReason::UserRequested;
    if (!session.requestId.empty()) {
      requestToSocket_.erase(session.requestId);
      emit clientDisconnected(QString::fromStdString(session.requestId),
                               reason);
      session.requestId.clear();
    }
    socket->disconnectFromHost();
    return;
  }

  if (type != protocol::ControlMessageType::ConnectRequest) {
    emit errorOccurred(QStringLiteral("Received unexpected control message"));
    return;
  }

  auto request = protocol::ParseConnectRequest(message);
  if (!request) {
    emit errorOccurred(
        QStringLiteral("Received malformed control message"));
    return;
  }

  const QString peerFingerprint = FingerprintOf(socket->peerCertificate());
  const QString claimedFingerprint =
      QString::fromStdString(request->certFingerprint);

  if (peerFingerprint != claimedFingerprint) {
    protocol::ConnectResponse rejection;
    rejection.requestId = request->requestId;
    rejection.accepted = false;
    rejection.reason = protocol::RejectReason::None;
    sendFramed(socket, protocol::ToJson(rejection));
    socket->disconnectFromHost();
    emit errorOccurred(
        QStringLiteral("Peer certificate fingerprint mismatch"));
    return;
  }

  session.requestId = request->requestId;
  requestToSocket_[request->requestId] = socket;

  emit connectRequestReceived(*request, peerFingerprint);
}

void ControlServer::accept(const std::string& requestId,
                            const protocol::AudioSession& audioSession) {
  auto it = requestToSocket_.find(requestId);
  if (it == requestToSocket_.end()) return;

  protocol::ConnectResponse response;
  response.requestId = requestId;
  response.accepted = true;
  response.reason = protocol::RejectReason::None;
  response.session = audioSession;

  sessions_.at(it->second).accepted = true;
  sendFramed(it->second, protocol::ToJson(response));
}

void ControlServer::reject(const std::string& requestId,
                            protocol::RejectReason reason) {
  auto it = requestToSocket_.find(requestId);
  if (it == requestToSocket_.end()) return;

  protocol::ConnectResponse response;
  response.requestId = requestId;
  response.accepted = false;
  response.reason = reason;

  sendFramed(it->second, protocol::ToJson(response));
  it->second->disconnectFromHost();
}

void ControlServer::disconnectClient(const std::string& requestId,
                                      protocol::DisconnectReason reason) {
  auto it = requestToSocket_.find(requestId);
  if (it == requestToSocket_.end()) return;
  QSslSocket* socket = it->second;

  protocol::DisconnectMessage message;
  message.reason = reason;
  sendFramed(socket, protocol::ToJson(message));

  auto sessionIt = sessions_.find(socket);
  if (sessionIt != sessions_.end()) {
    sessionIt->second.requestId.clear();
  }
  requestToSocket_.erase(it);
  socket->disconnectFromHost();
}

void ControlServer::sendFramed(QSslSocket* socket, const std::string& json) {
  const auto framed = protocol::FrameMessage(json);
  socket->write(framed.data(), static_cast<qint64>(framed.size()));
}

void ControlServer::onDisconnected() {
  auto* socket = qobject_cast<QSslSocket*>(sender());
  if (!socket) return;

  auto it = sessions_.find(socket);
  if (it != sessions_.end()) {
    if (!it->second.requestId.empty()) {
      requestToSocket_.erase(it->second.requestId);
      emit clientDisconnected(QString::fromStdString(it->second.requestId),
                               protocol::DisconnectReason::RemoteShutdown);
    }
    sessions_.erase(it);
  }
  socket->deleteLater();
}

}  // namespace wiremic::network
