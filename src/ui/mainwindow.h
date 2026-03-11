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
class QMenu;
class QAction;
class QActionGroup;
class QScreen;
class QSpinBox;
class QDoubleSpinBox;
class QTimer;
class QWidget;
class TargetGridWidget;
class PatternPresetsWidget;

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

    // Algorithm slots
    void onAlgorithmSelectionChanged(int index);
    void onGenerateGsMaskClicked();
    void onGsAutoRunTimeout();
    void onSendToSlmRequested();

    // Image tab slots
    void loadTargetImage();
    void clearTargetImage();

    // SLM Slots
    void receiveHologram(const QImage &mask);
    void sendHologramToSLM(const QImage &mask);
    void loadPhasePattern();
    void loadCorrectionFile();
    void clearCorrectionMask();
    void sendToSLM();
    void clearSLM();
    void refreshMonitorSelectionMenu();
    void onMonitorActionTriggered(QAction *action);

    // Grid Widget Slots
    void onGridPointAdded(int pointId, QPointF physicalCoords);
    void onGridPointMoved(int pointId, QPointF newPhysicalCoords);
    void onGridPointRemoved(int pointId);
    void onGridPointSelected(int pointId);
    void onPatternGenerated(const QVector<QPointF> &points, const QString &summary, const QString &details);

private:
    enum SlmOutputMode {
        DllOutputMode = 0,
        DirectScreenOutputMode = 1
    };

    enum class GsRunTrigger {
        ManualButton = 0,
        AutoRunTimer = 1,
        SendToSlmPreRun = 2
    };

    // Grid enlargement/minimize
    bool gridEnlarged = false;
    QWidget *gridTitleBar = nullptr;
    QLabel *gridTitleLabel = nullptr;
    QPushButton *gridMaxMinBtn = nullptr;
    QWidget *controlsWidget = nullptr;
    QGridLayout *mainLayout = nullptr;

    // Control section wrappers for hiding/showing
    QWidget *toolsRow = nullptr;
    QWidget *controlsRow = nullptr;

    void setupUI();
    void createMenus();
    void createMonitors(QGridLayout *layout);
    void createControls(QGridLayout *layout);
    void setupConnections();
    void applyTheme(bool dark);
    void toggleGridEnlarged();
    void replaceGridWithPoints(const QVector<QPointF> &points);

    void updatePhasePreview();
    QImage composeFullResolutionMask(bool applyCorrection = true) const;
    QString configPath() const;
    bool isSelectedMonitorAvailable() const;
    QScreen *selectedScreen() const;
    void persistSelectedMonitor();
    void persistCorrectionPath(const QString &path);
    void ensureDirectOutputWindow();
    void displayDirectOutput(const QImage &finalMask);
    void clearDirectOutput();
    void tryAutoApplySavedCorrection();

    void updateAlgorithmSettingsUi();
    void scheduleGsAutoRun();
    void autoSendToSlmIfEnabled();
    bool generateAlgorithmMask(bool showWarnings, GsRunTrigger trigger);
    QVector<float> defaultGsSourceAmplitude() const;
    bool isGerchbergSaxtonSelected() const;

    // UI Pointers
    TargetGridWidget *targetGridWidget;
    QTabWidget *targetModeTabs;
    PatternPresetsWidget *patternPresetsWidget = nullptr;
    QTableWidget *trapTable;
    QLabel *phaseMaskLabel;
    QLabel *resolutionLabel;
    QLabel *fpsLabel;
    QCheckBox *previewCorrectionCb;
    QPushButton *saveMaskBtn;
    QPushButton *addPointsBtn;
    QPushButton *clearAllPointsBtn;

    // Algorithm controls
    QComboBox *algorithmCombo = nullptr;
    QSpinBox *iterationsSpin = nullptr;
    QLabel *relaxationLabel = nullptr;
    QDoubleSpinBox *relaxationSpin = nullptr;
    QPushButton *generateGsBtn = nullptr;

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
    bool autoRunGsEnabled = false;
    bool autoSendSlmEnabled = false;
    int gsStartingPhaseMaskMode = 0;

    // SLM Pointers & State
    QPushButton *loadPhaseBtn;
    QPushButton *sendSlmBtn;
    QPushButton *clearSlmBtn;

    int slmWindowID = 0;
    QImage currentMask;
    QImage correctionMask;
    QLibrary slmLibrary;

    // SLM Output Routing State
    int slmOutputMode = DllOutputMode;
    int selectedMonitorNumber = 2;
    int slmActiveWidth = 1272;
    int slmActiveHeight = 1024;
    int slmActiveOffsetX = 0;
    int slmActiveOffsetY = 0;
    QString correctionMaskPath;

    QMenu *monitorSelectionMenu = nullptr;
    QActionGroup *monitorActionGroup = nullptr;
    QWidget *directOutputWindow = nullptr;
    QLabel *directOutputLabel = nullptr;

    // Auto-run timer
    QTimer *gsAutoRunTimer = nullptr;

    // Grid point data
    QMap<int, QPointF> gridPointData;
    int selectedPointId = -1;
    bool suppressGridStatusMessages = false;
    QString lastGeneratedPatternSummary;
    QString lastGeneratedPatternDetails;
};

#endif // MAINWINDOW_H
