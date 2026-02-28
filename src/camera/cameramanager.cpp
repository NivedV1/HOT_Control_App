#include "cameramanager.h"
#include <QVideoWidget>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>

CameraManager::CameraManager(QVideoWidget *videoOutput, QObject *parent) : QObject(parent) {
    availableCameras = QMediaDevices::videoInputs();

    captureSession = new QMediaCaptureSession(this);
    captureSession->setVideoOutput(videoOutput);

    imageCapture = new QImageCapture(this);
    captureSession->setImageCapture(imageCapture);

    mediaRecorder = new QMediaRecorder(this);
    captureSession->setRecorder(mediaRecorder);

    connect(mediaRecorder, &QMediaRecorder::durationChanged, this, &CameraManager::onDurationChanged);
}

CameraManager::~CameraManager() {
    if (camera) {
        camera->stop();
        delete camera;
    }
}

QList<QCameraDevice> CameraManager::getAvailableCameras() const {
    return availableCameras;
}

void CameraManager::changeCamera(int index) {
    if (index < 0 || index >= availableCameras.size()) return;
    if (camera) {
        camera->stop();
        delete camera;
    }
    camera = new QCamera(availableCameras[index], this);
    captureSession->setCamera(camera);
}

void CameraManager::startCamera() { if (camera) camera->start(); }
void CameraManager::stopCamera() { if (camera) camera->stop(); }

void CameraManager::captureImage() {
    if (!camera || !camera->isActive()) return;
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                       .filePath("HOT_Capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg");
    imageCapture->captureToFile(path);
    emit statusMessage("Image saved to: " + path);
}

void CameraManager::toggleRecording(bool checked) {
    if (!camera || !camera->isActive()) return;

    if (checked) {
        QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                           .filePath("HOT_Video_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4");
        mediaRecorder->setOutputLocation(QUrl::fromLocalFile(path));
        mediaRecorder->record();
        emit recordingTimeUpdated("00:00");
        emit statusMessage("Recording started...");
    } else {
        mediaRecorder->stop();
        emit statusMessage("Recording saved successfully.");
    }
}

void CameraManager::onDurationChanged(qint64 duration) {
    qint64 s = (duration / 1000) % 60;
    qint64 m = (duration / 60000) % 60;
    emit recordingTimeUpdated(QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')));
}