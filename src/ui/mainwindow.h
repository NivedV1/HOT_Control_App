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
#include <QCheckBox>
#include <QImage>
#include <QMap>
#include <QVector>
#include <QString>
#include <cstdint>
#include <QLibrary> // For dynamic DLL loading

// Forward declarations
class CameraManager;
class QGridLayout;
class TargetGridWidget;

// --- DLL Function Pointers (Bypasses the need for LabVIEW's extcode.h) ---
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
    void openSourceIntensityDialog();
    void onSourceIntensityApplied(const QVector<float> &intensityMap, int width, int height, const QString &presetName, double beamWaistPx, const QImage &previewImage);
    void onTabChanged(int index);
    void toggleTheme();
    void onRecordingTimeUpdated(const QString &timeString);
    void onFPSUpdated(const QString &fpsString);
    void savePhaseMask();
    void updateCameraFeed(const QImage &img);

    // Image tab slots
    void loadTargetImage();
    void clearTargetImage();

    // SLM Slots
    void receiveHologram(const QImage &mask);
    void sendHologramToSLM(const QImage &mask); // NEW: Direct send to SLM from dialog
    void loadPhasePattern();
    void loadCorrectionFile();  // Connected to the File Menu
    void sendToSLM();
    void clearSLM();

    // Grid Widget Slots
    void onGridPointAdded(int pointId, QPointF physicalCoords);
    void onGridPointMoved(int pointId, QPointF newPhysicalCoords);
    void onGridPointRemoved(int pointId);
    void onGridPointSelected(int pointId);

private:
    // Grid enlargement/minimize
    bool gridEnlarged = false;
    QWidget *gridTitleBar = nullptr;
    QLabel *gridTitleLabel = nullptr;
    QPushButton *gridMaxMinBtn = nullptr;
    QWidget *controlsWidget = nullptr;
    QGridLayout *mainLayout = nullptr;

    // Control section wrappers for hiding/showing
    QWidget *toolsRow = nullptr;      // Row 2 with tools
    QWidget *controlsRow = nullptr;   // Row 3 with main controls

    void setupUI();
    void createMenus();
    void createMonitors(QGridLayout *layout);
    void createControls(QGridLayout *layout);
    void setupConnections();
    void applyTheme(bool dark);
    void toggleGridEnlarged();

    // Helper function to manage the preview screen
    void updatePhasePreview();

    // returns a full-resolution image that combines currentMask and correctionMask (modulo 256)
    // if both masks are empty the returned image will be null
    QImage composeFullResolutionMask() const;

    // UI Pointers
    TargetGridWidget *targetGridWidget;
    QTabWidget *targetModeTabs;
    QWidget *trapListContainer;
    QTableWidget *trapTable;
    QLabel *phaseMaskLabel;
    QLabel *resolutionLabel;
    QLabel *fpsLabel;
    QCheckBox *previewCorrectionCb;
    QPushButton *saveMaskBtn;
    QPushButton *addTrapBtn;
    QPushButton *removeTrapBtn;
    QPushButton *clearTrapsBtn;

    // Image tab controls/state
    QPushButton *loadTargetImageBtn;
    QPushButton *clearTargetImageBtn;
    QLabel *targetImageInfoLabel;
    QImage loadedTargetImageOriginal;
    QImage loadedTargetImageGray;

    // Source intensity state (SLM-sized array)
    QVector<float> sourceIntensityMap;
    QImage sourceIntensityPreview;
    QString sourcePresetName;
    double sourceBeamWaistPx = 0.0;

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

    // SLM Pointers & State
    QPushButton *loadPhaseBtn;
    QPushButton *sendSlmBtn;
    QPushButton *clearSlmBtn;

    int slmWindowID = 0;
    QImage currentMask;     // The target hologram
    QImage correctionMask;  // The physical flatness correction
    QLibrary slmLibrary;    // Dynamic DLL Loader

    // Grid point data
    QMap<int, QPointF> gridPointData;  // Maps point ID to physical coordinates
    int selectedPointId = -1;
};

#endif // MAINWINDOW_H
