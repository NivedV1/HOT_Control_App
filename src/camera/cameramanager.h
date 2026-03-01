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

// --- NEW: OpenCV Header ---
#include <opencv2/opencv.hpp>

class CameraManager : public QObject {
    Q_OBJECT
public:
    explicit CameraManager(int engineBackend, QObject *parent = nullptr);
    ~CameraManager();

    // Returns strings instead of hardware devices so UI doesn't care which engine is running
    QStringList getCameraNames() const; 

public slots:
    void changeCamera(int index);
    void startCamera();
    void stopCamera();
    void captureImage();
    void toggleRecording(bool checked);

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

private:
    int backend; // 0 = Qt, 1 = OpenCV
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
    QTimer *cvTimer;
    bool isRecordingCV = false;
    qint64 cvRecordStartTime = 0;

    // FPS counting
    QTimer *fpsTimer;
    int frameCount = 0;
};

#endif // CAMERAMANAGER_H