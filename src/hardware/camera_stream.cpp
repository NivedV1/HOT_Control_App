#include "camera_stream.h"

#include <QDateTime>
#include <QMetaObject>

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

CameraStream::CameraStream(QObject *parent)
    : QObject(parent) {
}

CameraStream::~CameraStream() {
    stop();
}

bool CameraStream::start(const QString &bindIpAddr, quint16 port) {
    stop();

    bindIp = bindIpAddr.isEmpty() ? QStringLiteral("0.0.0.0") : bindIpAddr;
    bindPort = port;
    width = 640;
    height = 480;
    frameBytes = width * height;

    frameBuffer.clear();
    decodedBuffer.clear();
    chunkReceived.clear();
    expectedChunks = 0;
    chunksReceived = 0;
    hasActiveFrame = false;
    droppedFrameCount = 0;
    completedFrameCount = 0;
    lastDropStatusMs = 0;
    currentCodec = 0;
    frameDataBytes = 0;

    running.store(true);
    receiveThread = std::thread(&CameraStream::receiveLoop, this);
    return true;
}

void CameraStream::stop() {
    running.store(false);

    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

bool CameraStream::isRunning() const {
    return running.load();
}

QImage CameraStream::latestFrame() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return lastFrame.copy();
}

void CameraStream::queueStatusMessage(const QString &message) {
    QMetaObject::invokeMethod(this, [this, message]() {
        emit statusMessage(message);
    }, Qt::QueuedConnection);
}

void CameraStream::queueFrameReady(const QImage &frame, quint32 frameId) {
    QMetaObject::invokeMethod(this, [this, frame, frameId]() {
        emit frameReady(frame, frameId);
    }, Qt::QueuedConnection);
}

bool CameraStream::handleFrameBoundary(const PacketHeader &header) {
    if (hasActiveFrame && expectedChunks > 0 && chunksReceived < expectedChunks) {
        droppedFrameCount++;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastDropStatusMs >= 1000) {
            lastDropStatusMs = now;
            queueStatusMessage(
                QString("UDP Stream: dropped incomplete frame (dropped=%1, complete=%2).")
                    .arg(droppedFrameCount)
                    .arg(completedFrameCount));
        }
    }

    if (header.width == 0 || header.height == 0 || header.width > kMaxWidth || header.height > kMaxHeight) {
        return false;
    }

    const int newWidth = static_cast<int>(header.width);
    const int newHeight = static_cast<int>(header.height);
    const quint64 newBytes = static_cast<quint64>(newWidth) * static_cast<quint64>(newHeight);
    if (newBytes == 0 || newBytes > static_cast<quint64>(kMaxWidth) * static_cast<quint64>(kMaxHeight)) {
        return false;
    }

    width = newWidth;
    height = newHeight;
    frameBytes = static_cast<int>(newBytes);
    currentCodec = header.codec;
    frameDataBytes = 0;

    if (currentCodec == 0) {
        if (frameBuffer.size() != static_cast<size_t>(frameBytes)) {
            frameBuffer.resize(static_cast<size_t>(frameBytes));
        }
    } else if (currentCodec == 1) {
        frameBuffer.clear();
    } else {
        return false;
    }

    currentFrameId = header.frameID;
    expectedChunks = 0;
    chunksReceived = 0;
    chunkReceived.clear();
    hasActiveFrame = true;
    std::fill(frameBuffer.begin(), frameBuffer.end(), 0);
    return true;
}

bool CameraStream::decodeRleToRaw(const std::vector<quint8> &encoded,
                                  std::vector<quint8> &decoded,
                                  int expectedBytes) const {
    decoded.clear();
    if (expectedBytes <= 0) {
        return false;
    }
    decoded.reserve(static_cast<size_t>(expectedBytes));

    size_t i = 0;
    while (i + 1 < encoded.size()) {
        const quint8 run = encoded[i];
        const quint8 value = encoded[i + 1];
        if (run == 0) {
            return false;
        }
        if (decoded.size() + run > static_cast<size_t>(expectedBytes)) {
            return false;
        }
        decoded.insert(decoded.end(), run, value);
        i += 2;
    }
    if (i != encoded.size()) {
        return false;
    }
    return decoded.size() == static_cast<size_t>(expectedBytes);
}

void CameraStream::receiveLoop() {
#ifndef _WIN32
    queueStatusMessage("UDP Stream error: this receiver is currently implemented for Windows builds.");
    running.store(false);
    return;
#else
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        queueStatusMessage("UDP Stream error: WSAStartup failed.");
        running.store(false);
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        queueStatusMessage("UDP Stream error: failed to create UDP socket.");
        WSACleanup();
        running.store(false);
        return;
    }

    int rcvBufSize = 5 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&rcvBufSize), sizeof(rcvBufSize));

    DWORD timeoutMs = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in recvAddr;
    std::memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(bindPort);

    if (bindIp == "0.0.0.0") {
        recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        const QByteArray ipBytes = bindIp.toUtf8();
        if (inet_pton(AF_INET, ipBytes.constData(), &recvAddr.sin_addr) != 1) {
            queueStatusMessage("UDP Stream error: invalid bind IP address.");
            closesocket(sock);
            WSACleanup();
            running.store(false);
            return;
        }
    }

    if (::bind(sock, reinterpret_cast<sockaddr *>(&recvAddr), sizeof(recvAddr)) == SOCKET_ERROR) {
        queueStatusMessage(
            QString("UDP Stream error: bind failed on %1:%2.")
                .arg(bindIp)
                .arg(bindPort));
        closesocket(sock);
        WSACleanup();
        running.store(false);
        return;
    }

    queueStatusMessage(QString("Listening for UDP stream on %1:%2.").arg(bindIp).arg(bindPort));

    constexpr int kMaxPacket = 4096;
    char recvBuffer[kMaxPacket];

    while (running.load()) {
        const int bytesReceived = recvfrom(sock, recvBuffer, sizeof(recvBuffer), 0, nullptr, nullptr);
        if (bytesReceived == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                continue;
            }
            if (running.load()) {
                queueStatusMessage(QString("UDP Stream socket error: %1.").arg(err));
            }
            break;
        }

        if (bytesReceived < static_cast<int>(sizeof(LegacyPacketHeaderV2))) {
            continue;
        }

        PacketHeader header{};
        int headerBytes = 0;
        bool parsed = false;

        if (bytesReceived >= static_cast<int>(sizeof(PacketHeader))) {
            PacketHeader candidate{};
            std::memcpy(&candidate, recvBuffer, sizeof(PacketHeader));

            const bool codecValid = (candidate.codec <= 1);
            const bool reservedValid = (candidate.reserved0 == 0 && candidate.reserved1 == 0);
            const bool chunkShapeValid = (candidate.totalChunks > 0 && candidate.chunkIndex < candidate.totalChunks);
            if (codecValid && reservedValid && chunkShapeValid) {
                header = candidate;
                headerBytes = static_cast<int>(sizeof(PacketHeader));
                parsed = true;
            }
        }

        if (!parsed) {
            LegacyPacketHeaderV2 legacy{};
            std::memcpy(&legacy, recvBuffer, sizeof(LegacyPacketHeaderV2));
            if (legacy.totalChunks == 0 || legacy.chunkIndex >= legacy.totalChunks) {
                continue;
            }
            header.width = legacy.width;
            header.height = legacy.height;
            header.codec = 0;
            header.reserved0 = 0;
            header.reserved1 = 0;
            header.frameID = legacy.frameID;
            header.chunkIndex = legacy.chunkIndex;
            header.totalChunks = legacy.totalChunks;
            header.chunkOffset = legacy.chunkOffset;
            header.chunkSize = legacy.chunkSize;
            headerBytes = static_cast<int>(sizeof(LegacyPacketHeaderV2));
        }

        const int payloadSize = bytesReceived - headerBytes;
        if (payloadSize <= 0) {
            continue;
        }

        if (header.frameID != currentFrameId || !hasActiveFrame) {
            if (!handleFrameBoundary(header)) {
                hasActiveFrame = false;
                continue;
            }
            expectedChunks = header.totalChunks;
            if (expectedChunks == 0) {
                hasActiveFrame = false;
                continue;
            }
            chunkReceived.assign(expectedChunks, false);
        }

        if (!hasActiveFrame || expectedChunks == 0) {
            continue;
        }

        if (header.codec != currentCodec) {
            continue;
        }

        if (header.totalChunks != expectedChunks) {
            continue;
        }

        if (header.chunkIndex >= expectedChunks) {
            continue;
        }

        if (header.chunkSize != static_cast<quint32>(payloadSize)) {
            continue;
        }

        const quint64 chunkEnd = static_cast<quint64>(header.chunkOffset) + static_cast<quint64>(header.chunkSize);
        if (currentCodec == 0) {
            if (header.chunkOffset >= static_cast<quint32>(frameBytes)) {
                continue;
            }
            if (chunkEnd > static_cast<quint64>(frameBytes)) {
                continue;
            }
        } else {
            if (chunkEnd > static_cast<quint64>(kMaxCompressedBytes)) {
                continue;
            }
            if (chunkEnd > frameBuffer.size()) {
                frameBuffer.resize(static_cast<size_t>(chunkEnd));
            }
        }

        if (!chunkReceived[header.chunkIndex]) {
            std::memcpy(frameBuffer.data() + header.chunkOffset,
                        recvBuffer + headerBytes,
                        static_cast<size_t>(payloadSize));
            chunkReceived[header.chunkIndex] = true;
            chunksReceived++;
            if (static_cast<int>(chunkEnd) > frameDataBytes) {
                frameDataBytes = static_cast<int>(chunkEnd);
            }
        }

        if (chunksReceived == expectedChunks) {
            QImage frameCopy;
            if (currentCodec == 0) {
                QImage frame(frameBuffer.data(), width, height, width, QImage::Format_Grayscale8);
                frameCopy = frame.copy();
            } else {
                if (frameDataBytes <= 0 || frameDataBytes > static_cast<int>(frameBuffer.size())) {
                    hasActiveFrame = false;
                    continue;
                }
                std::vector<quint8> encoded(frameBuffer.begin(), frameBuffer.begin() + frameDataBytes);
                if (!decodeRleToRaw(encoded, decodedBuffer, frameBytes)) {
                    queueStatusMessage("UDP Stream: failed to decode compressed frame.");
                    hasActiveFrame = false;
                    continue;
                }
                QImage frame(decodedBuffer.data(), width, height, width, QImage::Format_Grayscale8);
                frameCopy = frame.copy();
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                lastFrame = frameCopy;
            }
            completedFrameCount++;
            queueFrameReady(frameCopy, header.frameID);
            hasActiveFrame = false;
        }
    }

    closesocket(sock);
    WSACleanup();
    running.store(false);
#endif
}
