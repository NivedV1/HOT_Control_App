#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>

class QVideoWidget;

class CameraManager : public QObject {
    Q_OBJECT
public:
    explicit CameraManager(QVideoWidget *videoOutput, QObject *parent = nullptr);
    ~CameraManager();

    QList<QCameraDevice> getAvailableCameras() const;

public slots:
    void changeCamera(int index);
    void startCamera();
    void stopCamera();
    void captureImage();
    void toggleRecording(bool checked);

signals:
    // Signals to tell the MainWindow UI to update
    void statusMessage(const QString &msg);
    void recordingTimeUpdated(const QString &timeString);

private slots:
    void onDurationChanged(qint64 duration);

private:
    QCamera *camera = nullptr;
    QMediaCaptureSession *captureSession = nullptr;
    QImageCapture *imageCapture = nullptr;
    QMediaRecorder *mediaRecorder = nullptr;
    QList<QCameraDevice> availableCameras;
};

#endif // CAMERAMANAGER_H