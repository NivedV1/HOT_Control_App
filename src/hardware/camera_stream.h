#ifndef CAMERA_STREAM_H
#define CAMERA_STREAM_H

#include <QObject>
#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class CameraStream : public QObject {
    Q_OBJECT
public:
    explicit CameraStream(QObject *parent = nullptr);
    ~CameraStream() override;

    bool start(const QString &bindIp, quint16 port);
    void stop();
    bool isRunning() const;

    QImage latestFrame() const;
    QString latestSenderIp() const;

signals:
    void frameReady(const QImage &frame, quint32 frameId);
    void statusMessage(const QString &message);

private:
#pragma pack(push, 1)
    struct PacketHeader {
        quint16 width;
        quint16 height;
        quint8 codec;
        quint8 reserved0;
        quint16 reserved1;
        quint32 frameID;
        quint16 chunkIndex;
        quint16 totalChunks;
        quint32 chunkOffset;
        quint32 chunkSize;
    };
    struct LegacyPacketHeaderV2 {
        quint16 width;
        quint16 height;
        quint32 frameID;
        quint16 chunkIndex;
        quint16 totalChunks;
        quint32 chunkOffset;
        quint32 chunkSize;
    };
#pragma pack(pop)

    void receiveLoop();
    void queueStatusMessage(const QString &message);
    void queueFrameReady(const QImage &frame, quint32 frameId);
    bool handleFrameBoundary(const PacketHeader &header);
    bool decodeRleToRaw(const std::vector<quint8> &encoded, std::vector<quint8> &decoded, int expectedBytes) const;

    mutable std::mutex stateMutex;
    std::atomic<bool> running{false};
    std::thread receiveThread;
    std::mutex startupMutex;
    std::condition_variable startupCv;
    bool startupComplete = false;
    bool startupSucceeded = false;

    QString bindIp = "0.0.0.0";
    quint16 bindPort = 9000;
    int width = 640;
    int height = 480;
    int frameBytes = 640 * 480;
    static constexpr int kMaxWidth = 8192;
    static constexpr int kMaxHeight = 8192;
    static constexpr int kMaxCompressedBytes = 64 * 1024 * 1024;

    std::vector<quint8> frameBuffer;
    std::vector<quint8> decodedBuffer;
    std::vector<bool> chunkReceived;
    quint8 currentCodec = 0;
    int frameDataBytes = 0;
    quint32 currentFrameId = 0;
    quint16 expectedChunks = 0;
    quint16 chunksReceived = 0;
    bool hasActiveFrame = false;
    quint64 droppedFrameCount = 0;
    quint64 completedFrameCount = 0;
    qint64 lastDropStatusMs = 0;

    QImage lastFrame;
    QString lastSenderIpAddress;
};

#endif // CAMERA_STREAM_H
