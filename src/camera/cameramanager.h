#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>
#include <QImage>
#include <QStringList>
#include <QRectF>
#include <QSize>

// --- NEW: OpenCV Header ---
#include <opencv2/opencv.hpp>

class CameraStream;

class CameraManager : public QObject {
    Q_OBJECT
public:
    enum class CameraBackend {
        QtNative = 0,
        OpenCV = 1,
        UdpStream = 2
    };

    explicit CameraManager(int engineBackend,
                           const QString &udpBindIp,
                           quint16 udpPort,
                           QObject *parent = nullptr);
    ~CameraManager();

    // Returns strings instead of hardware devices so UI doesn't care which engine is running
    QStringList getCameraNames() const;
    void setUdpConfig(const QString &bindIp, quint16 port);

public slots:
    void changeCamera(int index);
    void startCamera();
    void stopCamera();
    void captureImage();
    void toggleRecording(bool checked);
    void setZoomRegionNormalized(const QRectF &roiNormalized, bool enabled);

signals:
    void statusMessage(const QString &msg);
    void recordingTimeUpdated(const QString &timeString);
    void fpsUpdated(const QString &fpsString);
    void frameReady(const QImage &image); // NEW: Universal frame output

private slots:
    void onDurationChanged(qint64 duration);
    void onQtFrameReceived(const QVideoFrame &frame);
    void calculateFPS();
    void processOpenCVFrame(); // NEW: Grabs the OpenCV frame
    void onUdpFrameReceived(const QImage &frame, quint32 frameId);
    void onUdpStatusMessage(const QString &message);
    void checkUdpHealth();

private:
    QRectF normalizedZoomRoiForSize(const QSize &size, const QRectF &roi) const;
    QRect zoomCropRectForSize(const QSize &size) const;
    QImage applyZoomCrop(const QImage &image) const;
    cv::Mat applyZoomCropMat(const cv::Mat &frame) const;

    CameraBackend backend;
    int currentCamIndex = 0;

    // Qt Native Variables
    QCamera *qtCamera = nullptr;
    QMediaCaptureSession *qtCaptureSession = nullptr;
    QImageCapture *qtImageCapture = nullptr;
    QMediaRecorder *qtMediaRecorder = nullptr;
    QVideoSink *qtVideoSink = nullptr;
    QList<QCameraDevice> availableQtCameras;

    // OpenCV Variables
    cv::VideoCapture cvCapture;
    cv::VideoWriter cvVideoWriter;
    QTimer *cvTimer = nullptr;
    bool isRecordingCV = false;
    qint64 cvRecordStartTime = 0;
    bool cvSaveFollows = false;
    bool cvFlipX = false;
    bool cvFlipY = false;
    int cvRot = 0;

    // UDP stream variables
    CameraStream *udpStream = nullptr;
    QString udpBindIp;
    quint16 udpPort = 9000;
    QImage lastUdpFrame;
    qint64 lastUdpFrameMs = 0;
    bool udpTimeoutReported = false;
    QTimer *udpHealthTimer = nullptr;

    // FPS counting
    QTimer *fpsTimer;
    int frameCount = 0;
    QRectF zoomRoiNormalized = QRectF(0.0, 0.0, 1.0, 1.0);
    bool zoomEnabled = false;
};

#endif // CAMERAMANAGER_H
