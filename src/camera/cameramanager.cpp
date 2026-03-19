#include "cameramanager.h"
#include "camera_stream.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDebug>

CameraManager::CameraManager(int engineBackend,
                             const QString &bindIp,
                             quint16 port,
                             QObject *parent)
    : QObject(parent),
      backend((engineBackend == static_cast<int>(CameraBackend::OpenCV))
                  ? CameraBackend::OpenCV
                  : ((engineBackend == static_cast<int>(CameraBackend::UdpStream))
                         ? CameraBackend::UdpStream
                         : CameraBackend::QtNative)),
      udpBindIp(bindIp),
      udpPort(port) {
    
    // Setup Universal FPS Timer
    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &CameraManager::calculateFPS);

    if (backend == CameraBackend::QtNative) {
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

    } else if (backend == CameraBackend::OpenCV) {
        // --- Initialize OpenCV Engine ---
        cvTimer = new QTimer(this);
        connect(cvTimer, &QTimer::timeout, this, &CameraManager::processOpenCVFrame);
    } else {
        udpStream = new CameraStream(this);
        connect(udpStream, &CameraStream::frameReady, this, &CameraManager::onUdpFrameReceived);
        connect(udpStream, &CameraStream::statusMessage, this, &CameraManager::onUdpStatusMessage);

        udpHealthTimer = new QTimer(this);
        udpHealthTimer->setInterval(500);
        connect(udpHealthTimer, &QTimer::timeout, this, &CameraManager::checkUdpHealth);
    }
}

CameraManager::~CameraManager() {
    stopCamera();
}

QStringList CameraManager::getCameraNames() const {
    QStringList names;
    if (backend == CameraBackend::QtNative) {
        for (const auto &cam : availableQtCameras) {
            names << cam.description();
        }
    } else if (backend == CameraBackend::OpenCV) {
        // OpenCV doesn't give names automatically, so we provide generic slots.
        // OBS Virtual Camera usually grabs Device 0 or Device 1.
        names << "OpenCV Device 0" << "OpenCV Device 1" << "OpenCV Device 2" << "OpenCV Device 3";
    } else {
        names << "UDP Stream Source";
    }
    return names;
}

void CameraManager::setUdpConfig(const QString &bindIp, quint16 port) {
    udpBindIp = bindIp.isEmpty() ? QStringLiteral("0.0.0.0") : bindIp;
    udpPort = (port == 0) ? 9000 : port;
}

void CameraManager::changeCamera(int index) {
    if (index < 0) return;
    currentCamIndex = index;

    if (backend == CameraBackend::QtNative) {
        if (index >= availableQtCameras.size()) return;
        if (qtCamera) { qtCamera->stop(); delete qtCamera; }
        qtCamera = new QCamera(availableQtCameras[index], this);
        qtCaptureSession->setCamera(qtCamera);
    } else if (backend == CameraBackend::UdpStream) {
        currentCamIndex = 0;
    }
}

void CameraManager::startCamera() { 
    frameCount = 0;
    fpsTimer->start(1000);

    if (backend == CameraBackend::QtNative) {
        if (qtCamera) qtCamera->start();
    } else if (backend == CameraBackend::OpenCV) {
        // CAP_DSHOW forces DirectShow, which guarantees OBS Virtual Cam detection
        if (cvCapture.open(currentCamIndex, cv::CAP_DSHOW)) {
            cvTimer->start(30); // ~33 FPS
        } else {
            emit statusMessage("OpenCV Error: Cannot open camera.");
        }
    } else {
        if (!udpStream) {
            emit statusMessage("UDP Stream error: receiver not initialized.");
            return;
        }

        lastUdpFrame = QImage();
        lastUdpFrameMs = QDateTime::currentMSecsSinceEpoch();
        udpTimeoutReported = false;

        if (udpStream->start(udpBindIp, udpPort)) {
            if (udpHealthTimer) {
                udpHealthTimer->start();
            }
            emit statusMessage(QString("Listening on %1:%2").arg(udpBindIp).arg(udpPort));
        } else {
            emit statusMessage("UDP Stream error: failed to start.");
            fpsTimer->stop();
            emit fpsUpdated("FPS: 0");
        }
    }
}

void CameraManager::stopCamera() { 
    fpsTimer->stop();
    emit fpsUpdated("FPS: 0");

    if (backend == CameraBackend::QtNative) {
        if (qtCamera) qtCamera->stop();
    } else if (backend == CameraBackend::OpenCV) {
        cvTimer->stop();
        if (cvCapture.isOpened()) cvCapture.release();
        if (cvVideoWriter.isOpened()) cvVideoWriter.release();
        isRecordingCV = false;
    } else {
        if (udpHealthTimer) {
            udpHealthTimer->stop();
        }
        if (udpStream && udpStream->isRunning()) {
            udpStream->stop();
        }
        if (cvVideoWriter.isOpened()) {
            cvVideoWriter.release();
        }
        isRecordingCV = false;
        udpTimeoutReported = false;
    }
}

void CameraManager::captureImage() {
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                       .filePath("HOT_Capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg");
    
    if (backend == CameraBackend::QtNative && qtCamera && qtCamera->isActive()) {
        qtImageCapture->captureToFile(path);
        emit statusMessage("Image saved: " + path);
    } else if (backend == CameraBackend::OpenCV && cvCapture.isOpened()) {
        cv::Mat frame;
        cvCapture.read(frame);
        cv::imwrite(path.toStdString(), frame);
        emit statusMessage("OpenCV Image saved: " + path);
    } else if (backend == CameraBackend::UdpStream) {
        if (lastUdpFrame.isNull()) {
            emit statusMessage("UDP Stream: no frame available to save yet.");
            return;
        }
        if (lastUdpFrame.save(path)) {
            emit statusMessage("UDP Stream image saved: " + path);
        } else {
            emit statusMessage("UDP Stream: failed to save image.");
        }
    }
}

void CameraManager::toggleRecording(bool checked) {
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                       .filePath("HOT_Video_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")
                                 + (backend == CameraBackend::QtNative ? ".mp4" : ".avi"));
    
    if (checked) {
        if (backend == CameraBackend::QtNative && qtCamera && qtCamera->isActive()) {
            qtMediaRecorder->setOutputLocation(QUrl::fromLocalFile(path));
            qtMediaRecorder->record();
        } else if (backend == CameraBackend::OpenCV && cvCapture.isOpened()) {
            int width = cvCapture.get(cv::CAP_PROP_FRAME_WIDTH);
            int height = cvCapture.get(cv::CAP_PROP_FRAME_HEIGHT);
            // Using MJPG codec for OpenCV AVI saving
            cvVideoWriter.open(path.toStdString(), cv::VideoWriter::fourcc('M','J','P','G'), 30.0, cv::Size(width, height));
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        } else if (backend == CameraBackend::UdpStream) {
            if (lastUdpFrame.isNull()) {
                emit statusMessage("UDP Stream: wait for first frame before recording.");
                return;
            }
            const int w = lastUdpFrame.width();
            const int h = lastUdpFrame.height();
            cvVideoWriter.open(path.toStdString(), cv::VideoWriter::fourcc('M','J','P','G'), 30.0, cv::Size(w, h));
            if (!cvVideoWriter.isOpened()) {
                emit statusMessage("UDP Stream: failed to open video writer.");
                return;
            }
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        }
        emit recordingTimeUpdated("00:00");
        emit statusMessage("Recording started...");
    } else {
        if (backend == CameraBackend::QtNative) qtMediaRecorder->stop();
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

void CameraManager::onUdpFrameReceived(const QImage &frame, quint32 frameId) {
    Q_UNUSED(frameId);
    if (frame.isNull()) {
        return;
    }

    frameCount++;
    lastUdpFrame = frame.copy();
    lastUdpFrameMs = QDateTime::currentMSecsSinceEpoch();
    udpTimeoutReported = false;

    if (isRecordingCV && cvVideoWriter.isOpened()) {
        cv::Mat grayMat(lastUdpFrame.height(), lastUdpFrame.width(), CV_8UC1,
                        const_cast<uchar *>(lastUdpFrame.constBits()), lastUdpFrame.bytesPerLine());
        cv::Mat bgrMat;
        cv::cvtColor(grayMat, bgrMat, cv::COLOR_GRAY2BGR);
        cvVideoWriter.write(bgrMat);
        const qint64 duration = QDateTime::currentMSecsSinceEpoch() - cvRecordStartTime;
        onDurationChanged(duration);
    }

    emit frameReady(lastUdpFrame);
}

void CameraManager::onUdpStatusMessage(const QString &message) {
    emit statusMessage(message);
}

void CameraManager::checkUdpHealth() {
    if (!udpStream || !udpStream->isRunning()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!udpTimeoutReported && (now - lastUdpFrameMs) > 2000) {
        udpTimeoutReported = true;
        emit statusMessage("UDP Stream timeout: waiting for packets...");
    }
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
