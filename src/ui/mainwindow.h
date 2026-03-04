#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QImage>
#include <cstdint> // For uint8_t   
#include <QLibrary> // NEW: For dynamic DLL loading

// Forward declarations
class CameraManager;
class QGridLayout;

// --- NEW: Define the DLL Function Pointers ---
typedef void (__stdcall *Window_Settings_Func)(int32_t MonitorNumber, int32_t WindowNumber015, int32_t XPixelShift, int32_t YPixelShift);
typedef void (__stdcall *Window_Array_to_Display_Func)(uint8_t* _1DArrayIn, int32_t X_pixel_in, int32_t Y_pixel_in, int32_t WindowNumber015, int32_t targetDataSize);
typedef void (__stdcall *Window_Term_Func)(int32_t WindowNumber015);

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
    void onFPSUpdated(const QString &fpsString); 
    void savePhaseMask();
    void updateCameraFeed(const QImage &img);
    void receiveHologram(const QImage &mask);
    
    // SLM Slots
    void loadPhasePattern();    
    void sendToSLM();           
    void clearSLM();            
private:
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
    int cameraBackend = 0; 
    int camWidth = 1920;
    int camHeight = 1080;
    double camPixelSize = 5.0; 
    double laserWavelength = 1064.0; 
    double fourierFocalLength = 100.0; 

    bool isDarkMode = true;

    // SLM Pointers
    QPushButton *loadPhaseBtn;
    QPushButton *sendSlmBtn;
    QPushButton *clearSlmBtn;

    // SLM State
    int slmWindowID = 0; 
    QImage currentMask;  
    QLibrary slmLibrary; // NEW: Qt's DLL Loader
};

#endif // MAINWINDOW_H