#pragma once

#include <QElapsedTimer>
#include <QLocalSocket>

namespace WheelCommands {
inline QByteArray readReply(QLocalSocket &socket, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    QByteArray reply;
    while (true) {
        // A blocking write/connection wait may already have buffered the reply.
        reply += socket.readAll();
        const auto newline = reply.indexOf('\n');
        if (newline >= 0) return reply.left(newline + 1);
        const int remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0 || socket.state() == QLocalSocket::UnconnectedState) return {};
        if (!socket.waitForReadyRead(remaining) && socket.bytesAvailable() == 0) return {};
    }
}
}
