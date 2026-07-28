#include "ControlServer.hpp"

#include <QCryptographicHash>

#include <unistd.h>

#include <vector>

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
    // Report instead of dropping the client on the floor, and close the
    // descriptor we were handed — QSslSocket never adopted it.
    emit connectionSetupFailed(socket->errorString());
    delete socket;
    ::close(static_cast<int>(handle));
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
  connect(&server_, &ControlTcpServer::connectionSetupFailed, this,
          [this](QString reason) {
            emit errorOccurred(
                QStringLiteral("Rejected incoming control connection: %1")
                    .arg(reason));
          });
}

bool ControlServer::start() {
  if (!QSslSocket::supportsSsl() ||
      QSslSocket::activeBackend() != QStringLiteral("openssl")) {
    emit errorOccurred(QStringLiteral(
        "No functional TLS backend available (active backend: %1). "
        "libssl/libcrypto may be missing or fail to load — refusing to "
        "start an insecure control server.")
                            .arg(QSslSocket::activeBackend()));
    return false;
  }

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
  // disconnectFromHost() can emit disconnected() synchronously, which runs
  // onDisconnected() and erases from sessions_. Snapshot the sockets first so
  // we never iterate a container that a slot is mutating underneath us.
  std::vector<QSslSocket*> sockets;
  sockets.reserve(sessions_.size());
  for (const auto& [socket, session] : sessions_) sockets.push_back(socket);

  for (auto* socket : sockets) socket->disconnectFromHost();

  sessions_.clear();
  requestToSocket_.clear();
  server_.close();
}

quint16 ControlServer::port() const { return port_; }

void ControlServer::onNewSslConnection(QSslSocket* socket) {
  const auto& localCert = certificateManager_.localCertificate();
  socket->setLocalCertificate(CertificateFromPem(localCert.certificatePem));
  socket->setPrivateKey(KeyFromPem(localCert.privateKeyPem));
  // VerifyPeer requires the client to present a certificate. Chain validation
  // can never succeed against self-signed device certs, so onSslErrors()
  // ignores those specific errors; the real authentication is the SHA-256
  // fingerprint pin applied in processMessage() (trust-on-first-use).
  socket->setPeerVerifyMode(QSslSocket::VerifyPeer);

  connect(socket, &QSslSocket::encrypted, this, &ControlServer::onEncrypted);
  connect(socket, &QSslSocket::readyRead, this, &ControlServer::onReadyRead);
  connect(socket,
          QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
          this, &ControlServer::onSslErrors);
  connect(socket, &QSslSocket::disconnected, this,
          &ControlServer::onDisconnected);
  // Without this a failed TLS handshake is completely silent on the server
  // side: the client just sees the connection drop.
  connect(socket, &QSslSocket::errorOccurred, this,
          [this, socket](QAbstractSocket::SocketError) {
            emit errorOccurred(
                QStringLiteral("Control connection error: %1")
                    .arg(socket->errorString()));
          });

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

  if (!socket->isEncrypted()) {
    emit errorOccurred(QStringLiteral(
        "Rejected data on a socket that never completed a TLS handshake "
        "(TLS backend may be unavailable)"));
    socket->abort();
    return;
  }

  const QByteArray data = socket->readAll();
  it->second.framer.Feed(data.constData(), static_cast<size_t>(data.size()));

  try {
    // processMessage() may disconnect the socket, which runs onDisconnected()
    // and erases the session — invalidating `it`. Re-look it up every pass and
    // stop as soon as the session is gone.
    while (true) {
      auto sessionIt = sessions_.find(socket);
      if (sessionIt == sessions_.end()) return;

      auto message = sessionIt->second.framer.NextMessage();
      if (!message) break;

      processMessage(socket, *message);
    }
  } catch (const std::exception& e) {
    emit errorOccurred(
        QStringLiteral("Malformed control message, closing connection: %1")
            .arg(e.what()));
    socket->abort();
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

  const QSslCertificate peerCertificate = socket->peerCertificate();
  const QString peerFingerprint = FingerprintOf(peerCertificate);
  const QString claimedFingerprint =
      QString::fromStdString(request->certFingerprint);

  // A peer that presented no certificate has nothing to pin, so there is no
  // way to recognise it on a later reconnect. Refuse it outright rather than
  // comparing two empty-ish fingerprints.
  if (peerCertificate.isNull() || claimedFingerprint.isEmpty() ||
      peerFingerprint != claimedFingerprint) {
    protocol::ConnectResponse rejection;
    rejection.requestId = request->requestId;
    rejection.accepted = false;
    rejection.reason = protocol::RejectReason::UnsupportedProtocol;
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
