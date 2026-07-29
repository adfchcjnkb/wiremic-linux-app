#pragma once

#include <QUdpSocket>

namespace wiremic::audio {

bool BindUdpSocket(QUdpSocket& socket, quint16 port, quint16& boundPort,
                    QString& error);
}
