#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
//#include <QVideoWidget>
#include <QPushButton>

// Forward declarations
class CameraManager;
class QGridLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openSettingsDialog();
    void openHologramGenerator();
    void onTabChanged(int index);
    void toggleTheme();
    void onRecordingTimeUpdated(const QString &timeString);
    void onFPSUpdated(const QString &fpsString); // for fps showing camera
    void savePhaseMask();
    void updateCameraFeed(const QImage &img);
private:
    // Helper functions to keep code incredibly clean
    void setupUI();
    void createMenus();
    void createMonitors(QGridLayout *layout);
    void createControls(QGridLayout *layout);
    void setupConnections();
    void applyDarkTheme();
    void applyTheme(bool dark);

    // UI Pointers
    QGraphicsView *targetView;
    QGraphicsScene *targetScene;
    QTabWidget *targetModeTabs;
    QWidget *trapListContainer;
    QTableWidget *trapTable;
    QLabel *phaseMaskLabel;
    QLabel *resolutionLabel;
    QLabel *fpsLabel;
    QPushButton *saveMaskBtn;
    
    QLabel *cameraFeedLabel;
    QComboBox *camSelect;
    QPushButton *camStartBtn;
    QPushButton *camStopBtn;
    QPushButton *captureImageBtn;
    QPushButton *recordVideoBtn;
    QLabel *recordTimeLabel;

    // Backend Managers
    CameraManager *camManager;

    // Hardware Variables
    int slmWidth = 1920;
    int slmHeight = 1080;
    double slmPixelSize = 8.0;
    int cameraBackend = 0; // NEW: 0 = Qt, 1 = OpenCV
    int camWidth = 1920;
    int camHeight = 1080;
    double camPixelSize = 5.0; // microns
    double laserWavelength = 1064.0; // nm (Standard IR trapping laser)
    double fourierFocalLength = 100.0; // mm

    bool isDarkMode = true;
};

#endif // MAINWINDOW_H