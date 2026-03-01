#include "cameramanager.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDebug>

CameraManager::CameraManager(int engineBackend, QObject *parent) 
    : QObject(parent), backend(engineBackend) {
    
    // Setup Universal FPS Timer
    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &CameraManager::calculateFPS);

    if (backend == 0) {
        // --- Initialize Qt Native Engine ---
        availableQtCameras = QMediaDevices::videoInputs();
        qtCaptureSession = new QMediaCaptureSession(this);
        
        qtImageCapture = new QImageCapture(this);
        qtCaptureSession->setImageCapture(qtImageCapture);
        
        qtMediaRecorder = new QMediaRecorder(this);
        qtCaptureSession->setRecorder(qtMediaRecorder);
        connect(qtMediaRecorder, &QMediaRecorder::durationChanged, this, &CameraManager::onDurationChanged);

        qtVideoSink = new QVideoSink(this);
        qtCaptureSession->setVideoOutput(qtVideoSink);
        connect(qtVideoSink, &QVideoSink::videoFrameChanged, this, &CameraManager::onQtFrameReceived);

    } else {
        // --- Initialize OpenCV Engine ---
        cvTimer = new QTimer(this);
        connect(cvTimer, &QTimer::timeout, this, &CameraManager::processOpenCVFrame);
    }
}

CameraManager::~CameraManager() {
    stopCamera();
}

QStringList CameraManager::getCameraNames() const {
    QStringList names;
    if (backend == 0) {
        for (const auto &cam : availableQtCameras) {
            names << cam.description();
        }
    } else {
        // OpenCV doesn't give names automatically, so we provide generic slots.
        // OBS Virtual Camera usually grabs Device 0 or Device 1.
        names << "OpenCV Device 0" << "OpenCV Device 1" << "OpenCV Device 2" << "OpenCV Device 3";
    }
    return names;
}

void CameraManager::changeCamera(int index) {
    if (index < 0) return;
    currentCamIndex = index;

    if (backend == 0) {
        if (index >= availableQtCameras.size()) return;
        if (qtCamera) { qtCamera->stop(); delete qtCamera; }
        qtCamera = new QCamera(availableQtCameras[index], this);
        qtCaptureSession->setCamera(qtCamera);
    }
}

void CameraManager::startCamera() { 
    frameCount = 0;
    fpsTimer->start(1000);

    if (backend == 0) {
        if (qtCamera) qtCamera->start();
    } else {
        // CAP_DSHOW forces DirectShow, which guarantees OBS Virtual Cam detection
        if (cvCapture.open(currentCamIndex, cv::CAP_DSHOW)) {
            cvTimer->start(30); // ~33 FPS
        } else {
            emit statusMessage("OpenCV Error: Cannot open camera.");
        }
    }
}

void CameraManager::stopCamera() { 
    fpsTimer->stop();
    emit fpsUpdated("FPS: 0");

    if (backend == 0) {
        if (qtCamera) qtCamera->stop();
    } else {
        cvTimer->stop();
        if (cvCapture.isOpened()) cvCapture.release();
        if (cvVideoWriter.isOpened()) cvVideoWriter.release();
    }
}

void CameraManager::captureImage() {
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                       .filePath("HOT_Capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg");
    
    if (backend == 0 && qtCamera && qtCamera->isActive()) {
        qtImageCapture->captureToFile(path);
        emit statusMessage("Image saved: " + path);
    } else if (backend == 1 && cvCapture.isOpened()) {
        cv::Mat frame;
        cvCapture.read(frame);
        cv::imwrite(path.toStdString(), frame);
        emit statusMessage("OpenCV Image saved: " + path);
    }
}

void CameraManager::toggleRecording(bool checked) {
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                       .filePath("HOT_Video_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + (backend == 0 ? ".mp4" : ".avi"));
    
    if (checked) {
        if (backend == 0 && qtCamera && qtCamera->isActive()) {
            qtMediaRecorder->setOutputLocation(QUrl::fromLocalFile(path));
            qtMediaRecorder->record();
        } else if (backend == 1 && cvCapture.isOpened()) {
            int width = cvCapture.get(cv::CAP_PROP_FRAME_WIDTH);
            int height = cvCapture.get(cv::CAP_PROP_FRAME_HEIGHT);
            // Using MJPG codec for OpenCV AVI saving
            cvVideoWriter.open(path.toStdString(), cv::VideoWriter::fourcc('M','J','P','G'), 30.0, cv::Size(width, height));
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        }
        emit recordingTimeUpdated("00:00");
        emit statusMessage("Recording started...");
    } else {
        if (backend == 0) qtMediaRecorder->stop();
        else {
            isRecordingCV = false;
            if (cvVideoWriter.isOpened()) cvVideoWriter.release();
        }
        emit statusMessage("Recording saved.");
    }
}

// ==========================================
// FRAME PROCESSING & MATH
// ==========================================

void CameraManager::onQtFrameReceived(const QVideoFrame &frame) {
    frameCount++;
    // Convert modern Qt6 video frame to QImage so it matches OpenCV format
    QImage img = frame.toImage();
    if (!img.isNull()) {
        emit frameReady(img);
    }
}

void CameraManager::processOpenCVFrame() {
    if (!cvCapture.isOpened()) return;

    cv::Mat frame;
    cvCapture.read(frame);
    if (frame.empty()) return;

    frameCount++;

    if (isRecordingCV && cvVideoWriter.isOpened()) {
        cvVideoWriter.write(frame);
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - cvRecordStartTime;
        onDurationChanged(duration); // Update timer UI
    }

    // Convert BGR (OpenCV) to RGB (Qt)
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage img((const unsigned char*)(frame.data), frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    
    emit frameReady(img.copy()); // Use copy to prevent memory leaks from OpenCV pointers
}

void CameraManager::onDurationChanged(qint64 duration) {
    qint64 s = (duration / 1000) % 60;
    qint64 m = (duration / 60000) % 60;
    emit recordingTimeUpdated(QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')));
}

void CameraManager::calculateFPS() {
    emit fpsUpdated(QString("FPS: %1").arg(frameCount));
    frameCount = 0;
}   