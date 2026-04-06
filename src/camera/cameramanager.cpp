#include "cameramanager.h"
#include "camera_stream.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QFileDialog>
#include <QPushButton>
#include <QSettings>
#include <QCoreApplication>
#include <QtMath>
#include <QFileInfo>

bool CameraManager::revertRecordButtonIfPossible() const {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) {
        return false;
    }
    const bool oldState = btn->blockSignals(true);
    btn->setChecked(false);
    btn->blockSignals(oldState);
    return true;
}

bool CameraManager::openRecordingWriter(const QString &path,
                                        bool saveCompressed,
                                        const cv::Size &frameSize,
                                        bool isColor,
                                        const QString &sourceLabel) {
    if (frameSize.width <= 0 || frameSize.height <= 0) {
        emit statusMessage(sourceLabel + ": invalid frame size for recording.");
        return false;
    }

    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    const bool uncompressed = !saveCompressed;
    int codec = 0;

    if (uncompressed) {
        if (suffix != QStringLiteral("avi")) {
            emit statusMessage(sourceLabel + ": uncompressed recording requires an AVI file.");
            return false;
        }
        codec = cv::VideoWriter::fourcc('D', 'I', 'B', ' ');
    } else if (suffix == QStringLiteral("mp4")) {
        codec = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    } else {
        codec = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    }

    if (cvVideoWriter.isOpened()) {
        cvVideoWriter.release();
    }

    cvVideoWriter.open(path.toStdString(), codec, 30.0, frameSize, isColor);
    if (!cvVideoWriter.isOpened() && uncompressed) {
        cvVideoWriter.open(path.toStdString(), 0, 30.0, frameSize, isColor);
    }

    if (!cvVideoWriter.isOpened()) {
        const QString modeLabel = uncompressed ? QStringLiteral("uncompressed") : QStringLiteral("compressed");
        emit statusMessage(sourceLabel + ": failed to open " + modeLabel + " video writer.");
        return false;
    }

    return true;
}

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

void CameraManager::setZoomRegionNormalized(const QRectF &roiNormalized, bool enabled) {
    constexpr qreal kMinNormalizedExtent = 0.03;
    zoomEnabled = enabled;
    if (!zoomEnabled) {
        zoomRoiNormalized = QRectF(0.0, 0.0, 1.0, 1.0);
        return;
    }

    const qreal left = qBound(0.0, qMin(roiNormalized.left(), roiNormalized.right()), 1.0);
    const qreal right = qBound(0.0, qMax(roiNormalized.left(), roiNormalized.right()), 1.0);
    const qreal top = qBound(0.0, qMin(roiNormalized.top(), roiNormalized.bottom()), 1.0);
    const qreal bottom = qBound(0.0, qMax(roiNormalized.top(), roiNormalized.bottom()), 1.0);

    const qreal width = qBound<qreal>(kMinNormalizedExtent, right - left, 1.0);
    const qreal height = qBound<qreal>(kMinNormalizedExtent, bottom - top, 1.0);
    const qreal x = qBound(0.0, left, 1.0 - width);
    const qreal y = qBound(0.0, top, 1.0 - height);
    zoomRoiNormalized = QRectF(x, y, width, height);
}

QRectF CameraManager::normalizedZoomRoiForSize(const QSize &size, const QRectF &roi) const {
    constexpr qreal kMinNormalizedExtent = 0.03;
    if (size.width() <= 0 || size.height() <= 0) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal left = qBound(0.0, qMin(roi.left(), roi.right()), 1.0);
    const qreal right = qBound(0.0, qMax(roi.left(), roi.right()), 1.0);
    const qreal top = qBound(0.0, qMin(roi.top(), roi.bottom()), 1.0);
    const qreal bottom = qBound(0.0, qMax(roi.top(), roi.bottom()), 1.0);

    qreal width = qMax<qreal>(kMinNormalizedExtent, right - left);
    qreal height = qMax<qreal>(kMinNormalizedExtent, bottom - top);
    const qreal aspect = static_cast<qreal>(size.width()) / static_cast<qreal>(size.height());

    if (width / height > aspect) {
        height = width / aspect;
    } else {
        width = height * aspect;
    }

    if (width > 1.0 || height > 1.0) {
        const qreal scale = qMin(1.0 / width, 1.0 / height);
        width *= scale;
        height *= scale;
    }

    width = qBound<qreal>(kMinNormalizedExtent, width, 1.0);
    height = qBound<qreal>(kMinNormalizedExtent, height, 1.0);

    const qreal cx = qBound(0.0, (left + right) * 0.5, 1.0);
    const qreal cy = qBound(0.0, (top + bottom) * 0.5, 1.0);
    const qreal x = qBound(0.0, cx - width * 0.5, 1.0 - width);
    const qreal y = qBound(0.0, cy - height * 0.5, 1.0 - height);
    return QRectF(x, y, width, height);
}

QRect CameraManager::zoomCropRectForSize(const QSize &size) const {
    if (!zoomEnabled || size.width() <= 0 || size.height() <= 0) {
        return QRect(QPoint(0, 0), size);
    }

    const QRectF roi = normalizedZoomRoiForSize(size, zoomRoiNormalized);
    const int x0 = qBound(0, static_cast<int>(qFloor(roi.left() * size.width())), size.width() - 1);
    const int y0 = qBound(0, static_cast<int>(qFloor(roi.top() * size.height())), size.height() - 1);
    const int x1 = qBound(x0 + 1, static_cast<int>(qCeil(roi.right() * size.width())), size.width());
    const int y1 = qBound(y0 + 1, static_cast<int>(qCeil(roi.bottom() * size.height())), size.height());
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

QImage CameraManager::applyZoomCrop(const QImage &image) const {
    if (image.isNull() || !zoomEnabled) {
        return image;
    }

    const QRect cropRect = zoomCropRectForSize(image.size());
    if (cropRect.width() <= 0 || cropRect.height() <= 0 ||
        cropRect == QRect(QPoint(0, 0), image.size())) {
        return image;
    }
    return image.copy(cropRect);
}

cv::Mat CameraManager::applyZoomCropMat(const cv::Mat &frame) const {
    if (frame.empty() || !zoomEnabled) {
        return frame;
    }

    const QRect cropRect = zoomCropRectForSize(QSize(frame.cols, frame.rows));
    if (cropRect.width() <= 0 || cropRect.height() <= 0 ||
        cropRect == QRect(0, 0, frame.cols, frame.rows)) {
        return frame;
    }

    const cv::Rect roi(cropRect.x(), cropRect.y(), cropRect.width(), cropRect.height());
    return frame(roi).clone();
}

void CameraManager::captureImage() {
    QString settingsPath = QDir(QCoreApplication::applicationDirPath()).filePath("hardware_config.ini");
    QSettings settings(settingsPath, QSettings::IniFormat);
    bool save_compressed = settings.value("Hardware/save_compressed", false).toBool();
    bool uncompressed = !save_compressed;
    
    QString filter = uncompressed ? "BMP Image (*.bmp);;PNG Image (*.png);;JPEG Image (*.jpg)" 
                                  : "PNG Image (*.png);;JPEG Image (*.jpg);;BMP Image (*.bmp)";

    QString defaultName = "HOT_Capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(defaultName);
    
    QString path = QFileDialog::getSaveFileName(nullptr, "Save Image", defaultPath, filter);
    if (path.isEmpty()) return;
    
    bool save_follows = settings.value("Camera/save_follows_transforms", false).toBool();
    bool flipX = settings.value("Hardware/Camera_FlipX", false).toBool();
    bool flipY = settings.value("Hardware/Camera_FlipY", false).toBool();
    int rot = settings.value("Hardware/Camera_ViewRotation", 0).toInt();

    auto applyTransforms = [&](QImage &img) {
        if (!save_follows) return img;
        QTransform trans;
        if (flipX || flipY) trans.scale(flipX ? -1 : 1, flipY ? -1 : 1);
        if (rot != 0) trans.rotate(rot);
        return img.transformed(trans, Qt::SmoothTransformation);
    };

    if (backend == CameraBackend::QtNative && qtCamera && qtCamera->isActive()) {
        QImage toSave = applyTransforms(lastUdpFrame);
        toSave = applyZoomCrop(toSave);
        if (!toSave.isNull() && toSave.save(path)) {
            emit statusMessage("Image saved: " + path);
        } else {
            emit statusMessage("Webcam: failed to save image.");
        }
    } else if (backend == CameraBackend::OpenCV && cvCapture.isOpened()) {
        cv::Mat frame;
        cvCapture.read(frame);
        cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
        QImage img((const unsigned char*)(frame.data), frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
        QImage toSave = applyTransforms(img);
        toSave = applyZoomCrop(toSave);
        if (toSave.save(path)) {
            emit statusMessage("OpenCV Image saved: " + path);
        } else {
            emit statusMessage("OpenCV: failed to save image.");
        }
    } else if (backend == CameraBackend::UdpStream) {
        if (lastUdpFrame.isNull()) {
            emit statusMessage("UDP Stream: no frame available to save yet.");
            return;
        }
        QImage toSave = applyTransforms(lastUdpFrame);
        toSave = applyZoomCrop(toSave);
        if (toSave.save(path)) {
            emit statusMessage("UDP Stream image saved: " + path);
        } else {
            emit statusMessage("UDP Stream: failed to save image.");
        }
    }
}

void CameraManager::toggleRecording(bool checked) {
    if (checked) {
        QString settingsPath = QDir(QCoreApplication::applicationDirPath()).filePath("hardware_config.ini");
        QSettings settings(settingsPath, QSettings::IniFormat);
        bool save_compressed = settings.value("Hardware/save_compressed", false).toBool();
        bool uncompressed = !save_compressed;
        
        QString filter = uncompressed ? "AVI Video (*.avi)"
                                      : "MP4 Video (*.mp4);;AVI Video (*.avi)";
        
        QString defaultName = "HOT_Video_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                                  .filePath(defaultName + (uncompressed ? ".avi" : ".mp4"));
        
        QString path = QFileDialog::getSaveFileName(nullptr, "Save Video", defaultPath, filter);
        
        if (path.isEmpty()) {
            // Revert button state if cancelled
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (btn) {
                bool oldState = btn->blockSignals(true);
                btn->setChecked(false);
                btn->blockSignals(oldState);
            }
            emit statusMessage("Recording cancelled.");
            return;
        }

        cvSaveFollows = settings.value("Camera/save_follows_transforms", false).toBool();
        cvFlipX = settings.value("Hardware/Camera_FlipX", false).toBool();
        cvFlipY = settings.value("Hardware/Camera_FlipY", false).toBool();
        cvRot = settings.value("Hardware/Camera_ViewRotation", 0).toInt();

        auto getFinalSize = [&](int width, int height) {
            QSize finalSize(width, height);
            if (cvSaveFollows && (cvRot == 90 || cvRot == 270)) {
                finalSize = QSize(height, width);
            }
            const QRect cropRect = zoomCropRectForSize(finalSize);
            return cv::Size(cropRect.width(), cropRect.height());
        };

        if (backend == CameraBackend::QtNative && qtCamera && qtCamera->isActive()) {
            if (lastUdpFrame.isNull()) {
                revertRecordButtonIfPossible();
                emit statusMessage("Webcam: wait for first frame before recording.");
                return;
            }

            if (!openRecordingWriter(path,
                                     save_compressed,
                                     getFinalSize(lastUdpFrame.width(), lastUdpFrame.height()),
                                     true,
                                     QStringLiteral("Webcam"))) {
                revertRecordButtonIfPossible();
                return;
            }
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        } else if (backend == CameraBackend::OpenCV && cvCapture.isOpened()) {
            int width = cvCapture.get(cv::CAP_PROP_FRAME_WIDTH);
            int height = cvCapture.get(cv::CAP_PROP_FRAME_HEIGHT);

            if (!openRecordingWriter(path,
                                     save_compressed,
                                     getFinalSize(width, height),
                                     true,
                                     QStringLiteral("OpenCV"))) {
                revertRecordButtonIfPossible();
                return;
            }
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        } else if (backend == CameraBackend::UdpStream) {
            if (lastUdpFrame.isNull()) {
                revertRecordButtonIfPossible();
                emit statusMessage("UDP Stream: wait for first frame before recording.");
                return;
            }

            if (!openRecordingWriter(path,
                                     save_compressed,
                                     getFinalSize(lastUdpFrame.width(), lastUdpFrame.height()),
                                     false,
                                     QStringLiteral("UDP Stream"))) {
                revertRecordButtonIfPossible();
                return;
            }
            isRecordingCV = true;
            cvRecordStartTime = QDateTime::currentMSecsSinceEpoch();
        }
        emit recordingTimeUpdated("00:00");
        emit statusMessage("Recording started...");
    } else {
        isRecordingCV = false;
        if (cvVideoWriter.isOpened()) cvVideoWriter.release();
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
        lastUdpFrame = img.copy(); // We reuse lastUdpFrame to cache the raw image for saving
        
        if (isRecordingCV && cvVideoWriter.isOpened()) {
            QImage toWrite = img;
            if (cvSaveFollows) {
                QTransform trans;
                if (cvFlipX || cvFlipY) trans.scale(cvFlipX ? -1 : 1, cvFlipY ? -1 : 1);
                if (cvRot != 0) trans.rotate(cvRot);
                toWrite = img.transformed(trans, Qt::SmoothTransformation);
            }
            toWrite = applyZoomCrop(toWrite);
            QImage imgRGB = toWrite.convertToFormat(QImage::Format_RGB888);
            cv::Mat mat(imgRGB.height(), imgRGB.width(), CV_8UC3, const_cast<uchar*>(imgRGB.constBits()), imgRGB.bytesPerLine());
            cv::Mat bgrMat;
            cv::cvtColor(mat, bgrMat, cv::COLOR_RGB2BGR);
            cvVideoWriter.write(bgrMat);
            const qint64 duration = QDateTime::currentMSecsSinceEpoch() - cvRecordStartTime;
            onDurationChanged(duration);
        }
        
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
        cv::Mat frameToWrite = frame;
        if (cvSaveFollows) {
            if (cvFlipX && cvFlipY) cv::flip(frame, frameToWrite, -1);
            else if (cvFlipX) cv::flip(frame, frameToWrite, 1);
            else if (cvFlipY) cv::flip(frame, frameToWrite, 0);

            if (cvRot == 90) cv::rotate(frameToWrite, frameToWrite, cv::ROTATE_90_CLOCKWISE);
            else if (cvRot == 180) cv::rotate(frameToWrite, frameToWrite, cv::ROTATE_180);
            else if (cvRot == 270) cv::rotate(frameToWrite, frameToWrite, cv::ROTATE_90_COUNTERCLOCKWISE);
        }
        frameToWrite = applyZoomCropMat(frameToWrite);
        cvVideoWriter.write(frameToWrite);
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
        QImage toWrite = lastUdpFrame;
        if (cvSaveFollows) {
            QTransform trans;
            if (cvFlipX || cvFlipY) trans.scale(cvFlipX ? -1 : 1, cvFlipY ? -1 : 1);
            if (cvRot != 0) trans.rotate(cvRot);
            toWrite = lastUdpFrame.transformed(trans, Qt::SmoothTransformation);
        }
        toWrite = applyZoomCrop(toWrite);
        QImage grayImage = toWrite.convertToFormat(QImage::Format_Grayscale8);
        cv::Mat grayMat(grayImage.height(), grayImage.width(), CV_8UC1,
                        const_cast<uchar *>(grayImage.constBits()), grayImage.bytesPerLine());
        cvVideoWriter.write(grayMat);
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
