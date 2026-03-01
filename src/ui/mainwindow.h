#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVideoWidget>
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
    void onRecordingTimeUpdated(const QString &timeString);
    void savePhaseMask();
private:
    // Helper functions to keep code incredibly clean
    void setupUI();
    void createMenus();
    void createMonitors(QGridLayout *layout);
    void createControls(QGridLayout *layout);
    void setupConnections();
    void applyDarkTheme();


    // UI Pointers
    QGraphicsView *targetView;
    QGraphicsScene *targetScene;
    QTabWidget *targetModeTabs;
    QWidget *trapListContainer;
    QTableWidget *trapTable;
    QLabel *phaseMaskLabel;
    QLabel *resolutionLabel;
    QPushButton *saveMaskBtn;
    
    QVideoWidget *cameraFeedWidget;
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
};

#endif // MAINWINDOW_H