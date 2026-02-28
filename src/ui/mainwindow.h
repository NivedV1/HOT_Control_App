#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTableWidget>
#include <QAction>

// Camera & Media Includes
#include <QMediaDevices>
#include <QCameraDevice>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoWidget>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onTabChanged(int index);
    
    // Settings Slot
    void openSettingsDialog();

    // Camera Slots
    void startCamera();
    void stopCamera();
    void changeCamera(int index);

    // Recording Slots
    void captureImage();
    void toggleRecording(bool checked);
    void updateRecordTime(qint64 duration);

private:
    void setupUI();
    void applyDarkTheme();

    // UI Elements
    QGraphicsView *targetView;
    QGraphicsScene *targetScene;
    QTabWidget *targetModeTabs;
    QWidget *trapListContainer;
    QTableWidget *trapTable;
    QLabel *phaseMaskLabel;
    QLabel *resolutionLabel; // Dynamic label for SLM resolution

    // Camera Variables
    QVideoWidget *cameraFeedWidget;
    QComboBox *camSelect;
    QPushButton *camStartBtn;
    QPushButton *camStopBtn;
    
    // Capture Variables
    QPushButton *captureImageBtn;
    QPushButton *recordVideoBtn;
    QLabel *recordTimeLabel;

    QCamera *camera = nullptr;
    QMediaCaptureSession *captureSession = nullptr;
    QImageCapture *imageCapture = nullptr;
    QMediaRecorder *mediaRecorder = nullptr;
    QList<QCameraDevice> availableCameras;

    // Hardware Variables (Used later for your GS Algorithm math)
    int slmWidth = 1920;
    int slmHeight = 1080;
    double slmPixelSize = 8.0; // Stored in microns
};

#endif // MAINWINDOW_H