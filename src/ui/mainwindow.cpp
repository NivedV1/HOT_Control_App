#include "mainwindow.h"
#include "settingsdialog.h"
#include "hologramdialog.h"
#include "sourceintensitydialog.h"
#include "computebenchmarkdialog.h"
#include "components/targetgridwidget.h"
#include "components/patternpresetswidget.h"
#include "components/arrowspinbox.h"
#include "components/pythonsyntaxhighlighter.h"
#include "../core/patterngenerator.h"
#include "../core/python_trap_script_engine.h"
#include "../core/algorithms/gs_algorithm.h"
#include "../core/algorithms/rme_algorithm.h"
#include "../camera/cameramanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHeaderView>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSlider>
#include <QCheckBox>
#include <QPlainTextEdit>
#include "components/pythoncodeeditor.h"
#include <QFile>
#include <QTextStream>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QSettings>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QDebug>
#include <QActionGroup>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QStyle>
#include <QFontDatabase>
#include <QSignalBlocker>
#include <QShortcut>
#include <QCursor>
#include <QMouseEvent>
#include <QEvent>
#include <QHostAddress>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QtMath>
#include <cstring>

namespace {
constexpr int kImageTabIndex = 2;
constexpr int kCameraTabIndex = 3;
constexpr int kPythonTabIndex = 5;
constexpr int kDefaultMonitorNumber = 2;
constexpr int kDefaultCameraPreviewMonitorNumber = 1;
constexpr int kDefaultActiveWidth = 1272;
constexpr int kDefaultActiveHeight = 1024;
constexpr int kDefaultActiveOffsetX = 0;
constexpr int kDefaultActiveOffsetY = 0;
constexpr int kGsAutoRunDebounceMs = 180;
constexpr double kFramePointEpsilon = 1e-6;
constexpr int kAlgorithmGsIndex = 0;
constexpr int kAlgorithmWeightedGsIndex = 1;
constexpr int kAlgorithmRmeIndex = 2;
constexpr qreal kMinZoomRoiNormalized = 0.03;
constexpr quint16 kXpSenderOverlayPort = 9001;
constexpr quint16 kXpSenderOverlayProtocolVersion = 1;
constexpr char kXpSenderOverlayMagic[4] = {'X', 'P', 'O', 'L'};

#pragma pack(push, 1)
struct XpSenderOverlayPacketHeader {
    char magic[4];
    quint16 version;
    quint16 pointCount;
    quint16 imageWidth;
    quint16 imageHeight;
    quint32 frameId;
    quint16 rotationDegrees;
    quint8 flags;
    quint8 reserved[3];
};

struct XpSenderOverlayPoint {
    quint16 x;
    quint16 y;
    quint8 flags;
    quint8 reserved[3];
};
#pragma pack(pop)

enum : quint8 {
    kXpSenderOverlayFlagEnabled = 0x01,
    kXpSenderOverlayFlagFlipX = 0x02,
    kXpSenderOverlayFlagFlipY = 0x04,
    kXpSenderOverlayPointFlagSelected = 0x01
};

QString hardwareConfigPath() {
    return QCoreApplication::applicationDirPath() + "/hardware_config.ini";
}

int normalizeCameraRotation(int degrees) {
    int normalized = degrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    if (normalized == 0 || normalized == 90 || normalized == 180 || normalized == 270) {
        return normalized;
    }
    return 0;
}

#if HOT_ENABLE_TEMP_GS_PROFILING
QString gsStartingPhaseMaskToString(GSAlgorithm::GSStartingPhaseMask mask) {
    switch (mask) {
    case GSAlgorithm::GSStartingPhaseMask::BinaryGrating:
        return "BinaryGrating";
    case GSAlgorithm::GSStartingPhaseMask::RandomPhase:
        return "RandomPhase";
    case GSAlgorithm::GSStartingPhaseMask::Checkerboard:
    default:
        return "Checkerboard";
    }
}

QString gsComputeBackendToString(GSAlgorithm::GSComputeBackend backend) {
    switch (backend) {
    case GSAlgorithm::GSComputeBackend::CPU:
        return "CPU";
    case GSAlgorithm::GSComputeBackend::CUDA:
        return "CUDA";
    case GSAlgorithm::GSComputeBackend::OpenCL:
        return "OpenCL";
    case GSAlgorithm::GSComputeBackend::Auto:
    default:
        return "Auto";
    }
}

QString gsComputeBackendUsedToString(GSAlgorithm::GSComputeBackendUsed backend) {
    switch (backend) {
    case GSAlgorithm::GSComputeBackendUsed::CUDA:
        return "CUDA";
    case GSAlgorithm::GSComputeBackendUsed::OpenCL:
        return "OpenCL";
    case GSAlgorithm::GSComputeBackendUsed::CPU:
    default:
        return "CPU";
    }
}

QString targetModeLabelFromIndex(int index) {
    switch (index) {
    case 0:
        return "Manual";
    case 1:
        return "Pattern";
    case 2:
        return "Image";
    case 3:
        return "Camera";
    case 4:
        return "Animation";
    case 5:
        return "Python";
    default:
        return "Unknown";
    }
}

bool areFramesEquivalent(const QVector<QPointF> &a, const QVector<QPointF> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        if (qAbs(a.at(i).x() - b.at(i).x()) > kFramePointEpsilon ||
            qAbs(a.at(i).y() - b.at(i).y()) > kFramePointEpsilon) {
            return false;
        }
    }
    return true;
}

bool isStaticSequence(const QVector<QVector<QPointF>> &frames) {
    if (frames.size() <= 1) {
        return true;
    }
    const QVector<QPointF> &first = frames.first();
    for (int i = 1; i < frames.size(); ++i) {
        if (!areFramesEquivalent(first, frames.at(i))) {
            return false;
        }
    }
    return true;
}

QPointF clampPointToCameraBounds(const QPointF &point, int camWidth, int camHeight) {
    const double halfWidth = camWidth / 2.0;
    const double halfHeight = camHeight / 2.0;

    return QPointF(qBound(-halfWidth, point.x(), halfWidth),
                   qBound(-halfHeight, point.y(), halfHeight));
}

bool isEditableInputWidget(const QWidget *widget) {
    const QWidget *current = widget;
    while (current) {
        if (qobject_cast<const QLineEdit *>(current) ||
            qobject_cast<const QAbstractSpinBox *>(current) ||
            qobject_cast<const QPlainTextEdit *>(current) ||
            qobject_cast<const QTextEdit *>(current)) {
            return true;
        }
        current = current->parentWidget();
    }
    return false;
}

void appendGsRuntimeLogEntry(const QString &triggerLabel,
                             const QString &targetModeLabel,
                             const QString &patternSummary,
                             const QString &patternDetails,
                             bool success,
                             const QString &error,
                             qint64 elapsedMs,
                             double msPerIteration,
                             const GSAlgorithm::GSConfig &config,
                             const GSAlgorithm::GSResult &result,
                             bool usingDefaultSource,
                             const QString &sourcePresetName,
                             double sourceBeamWaistPx,
                             int targetPointCount) {
    QFile logFile(QCoreApplication::applicationDirPath() + "/gs_runtime_debug.log");
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to append GS runtime log:" << logFile.fileName();
        return;
    }

    QTextStream out(&logFile);
    out << "==== GS_RUN " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " ====\n";
    out << "trigger=" << triggerLabel << "\n";
    out << "success=" << (success ? "true" : "false") << "\n";
    out << "error=" << (error.isEmpty() ? "<none>" : error) << "\n";
    out << "elapsed_ms=" << elapsedMs << "\n";
    out << "ms_per_iteration=" << QString::number(msPerIteration, 'f', 3) << "\n";
    out << "iterations=" << config.iterations << "\n";
    out << "starting_phase_mask=" << gsStartingPhaseMaskToString(config.startingPhaseMask) << "\n";
    out << "compute_backend_requested=" << gsComputeBackendToString(config.computeBackend) << "\n";
    out << "compute_backend_used=" << gsComputeBackendUsedToString(result.backendUsed) << "\n";
    out << "backend_info=" << (result.backendInfo.isEmpty() ? "<none>" : result.backendInfo) << "\n";
    out << "fallback_occurred=" << (result.fallbackOccurred ? "true" : "false") << "\n";
    out << "fallback_reason=" << (result.fallbackReason.isEmpty() ? "<none>" : result.fallbackReason) << "\n";
    out << "requested_target_count=" << result.requestedTargetCount << "\n";
    out << "used_target_count=" << result.usedTargetCount << "\n";
    out << "skipped_outside_camera_fov=" << result.skippedOutsideCameraFov << "\n";
    out << "skipped_outside_slm_bounds=" << result.skippedOutsideSlmBounds << "\n";
    out << "active_target_points=" << targetPointCount << "\n";
    out << "target_mode=" << targetModeLabel << "\n";
    if (targetModeLabel == "Pattern") {
        out << "pattern_summary=" << (patternSummary.isEmpty() ? "<none>" : patternSummary) << "\n";
        out << "pattern_details=" << (patternDetails.isEmpty() ? "<none>" : patternDetails) << "\n";
    }
    out << "source_mode=" << (usingDefaultSource ? "default_gaussian" : "custom_source_map") << "\n";
    if (!usingDefaultSource) {
        out << "source_preset=" << (sourcePresetName.isEmpty() ? "<unnamed>" : sourcePresetName) << "\n";
        out << "source_beam_waist_px=" << QString::number(sourceBeamWaistPx, 'f', 3) << "\n";
    }
    out << "slm_resolution=" << config.slmWidth << "x" << config.slmHeight << "\n";
    out << "cam_resolution=" << config.camWidth << "x" << config.camHeight << "\n";
    out << "slm_pixel_size_um=" << QString::number(config.slmPixelSizeUm, 'f', 4) << "\n";
    out << "cam_pixel_size_um=" << QString::number(config.camPixelSizeUm, 'f', 4) << "\n";
    out << "camera_imaging_magnification=" << QString::number(config.cameraImagingMagnification, 'f', 4) << "\n";
    out << "wavelength_nm=" << QString::number(config.wavelengthNm, 'f', 4) << "\n";
    out << "focal_length_mm=" << QString::number(config.focalLengthMm, 'f', 4) << "\n";
    out << "\n";
    out.flush();
}
#endif
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setWindowIcon(QIcon(":/favicon.ico"));
    setMinimumSize(800, 600);
    xpSenderOverlaySocket = new QUdpSocket(this);

    QSettings settings(configPath(), QSettings::IniFormat);

    slmWidth = settings.value("Hardware/SLM_Width", 1920).toInt();
    slmHeight = settings.value("Hardware/SLM_Height", 1080).toInt();
    slmPixelSize = settings.value("Hardware/SLM_PixelSize", 8.0).toDouble();
    cameraBackend = settings.value("Hardware/CameraBackend", 0).toInt();
    if (cameraBackend < 0 || cameraBackend > 2) {
        cameraBackend = 0;
    }

    camWidth = settings.value("Hardware/Cam_Width", 1920).toInt();
    camHeight = settings.value("Hardware/Cam_Height", 1080).toInt();
    camPixelSize = settings.value("Hardware/Cam_PixelSize", 5.0).toDouble();
    saveCompressed = settings.value("Hardware/save_compressed", false).toBool();
    flipCameraX = settings.value("Hardware/Camera_FlipX", false).toBool();
    flipCameraY = settings.value("Hardware/Camera_FlipY", false).toBool();
    saveFollowsTransforms = settings.value("Camera/save_follows_transforms", false).toBool();
    udpBindIp = settings.value("Hardware/UDP_BindIP", "0.0.0.0").toString();
    udpPort = settings.value("Hardware/UDP_Port", 9000).toInt();
    laserWavelength = settings.value("Optical/Wavelength", 1064.0).toDouble();
    fourierFocalLength = settings.value("Optical/FocalLength", 100.0).toDouble();
    cameraImagingMagnification = qMax(0.01, settings.value("Optical/CameraImagingMagnification", 1.0).toDouble());
    autoRunGsEnabled = settings.value("Hardware/AutoRunGS", false).toBool();
    autoSendSlmEnabled = settings.value("Hardware/AutoSendSLM", false).toBool();
    gsStartingPhaseMaskMode = settings.value("Hardware/GS_StartingPhaseMask", 0).toInt();
    gsComputeBackendMode = settings.value("Hardware/GS_ComputeBackend", 0).toInt();
    openClPlatformIndex = settings.value("Hardware/GS_OpenCLPlatformIndex", 0).toInt();
    openClDeviceIndex = settings.value("Hardware/GS_OpenCLDeviceIndex", 0).toInt();
    cudaDeviceIndex = settings.value("Hardware/GS_CUDADeviceIndex", 0).toInt();

    isDarkMode = settings.value("UI/DarkMode", true).toBool();
    slmOutputMode = settings.value("Hardware/SLM_OutputMode", DllOutputMode).toInt();
    selectedMonitorNumber = settings.value("Hardware/SLM_SelectedMonitor", kDefaultMonitorNumber).toInt();
    cameraPreviewMonitorNumber = settings.value("Hardware/CameraPreview_SelectedMonitor",
                                                kDefaultCameraPreviewMonitorNumber).toInt();
    slmActiveWidth = settings.value("Hardware/SLM_ActiveWidth", kDefaultActiveWidth).toInt();
    slmActiveHeight = settings.value("Hardware/SLM_ActiveHeight", kDefaultActiveHeight).toInt();
    slmActiveOffsetX = settings.value("Hardware/SLM_ActiveOffsetX", kDefaultActiveOffsetX).toInt();
    slmActiveOffsetY = settings.value("Hardware/SLM_ActiveOffsetY", kDefaultActiveOffsetY).toInt();
    correctionMaskPath = settings.value("Hardware/SLM_CorrectionPath", "").toString();

    setupUI();
    applyTheme(isDarkMode);

    pythonScriptEngine = new PythonTrapScriptEngine();
    if (!pythonScriptEngine->isReady()) {
        const QString runtimeError = pythonScriptEngine->initError().isEmpty()
            ? QString("Embedded Python runtime initialization failed.")
            : pythonScriptEngine->initError();
        statusBar()->showMessage(runtimeError, 7000);
        if (pythonStatusLabel) {
            pythonStatusLabel->setText(runtimeError);
        }
    }

    camManager = new CameraManager(cameraBackend, udpBindIp, static_cast<quint16>(udpPort), this);
    setupConnections();
    camManager->setZoomRegionNormalized(cameraZoomRoiNormalized, cameraZoomEnabled);
    refreshCameraPreviewMonitorOptions();

    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *) {
        onScreenTopologyChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
        onScreenTopologyChanged();
    });

    // Load SLM DLL safely
    slmLibrary.setFileName(QCoreApplication::applicationDirPath() + "/Image_Control.dll");
    if (!slmLibrary.load()) {
        qWarning() << "Could not load Image_Control.dll! Ensure it is in the build folder.";
    }

    tryAutoApplySavedCorrection();
}

MainWindow::~MainWindow() {
    if (camManager) {
        camManager->stopCamera();
        delete camManager;
        camManager = nullptr;
    }

    clearDirectOutput();
    if (directOutputWindow) {
        directOutputWindow->close();
        delete directOutputWindow;
        directOutputWindow = nullptr;
    }

    clearCameraPreviewOutput();
    if (cameraPreviewWindow) {
        cameraPreviewWindow->close();
        delete cameraPreviewWindow;
        cameraPreviewWindow = nullptr;
    }

    // Safety check: close SLM if app is closed
    if (slmLibrary.isLoaded()) {
        auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
        if (winTerm) winTerm(slmWindowID);
    }

    delete pythonScriptEngine;
    pythonScriptEngine = nullptr;
}

void MainWindow::setupUI() {
    createMenus();
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    mainLayout = new QGridLayout(centralWidget);
    mainLayout->setSpacing(10); 

    createMonitors(mainLayout);
    createControls(mainLayout);

    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(1, 1);
    mainLayout->setColumnStretch(2, 1);
    mainLayout->setRowStretch(0, 0); 
    mainLayout->setRowStretch(1, 3); 
    mainLayout->setRowStretch(2, 0); 
    mainLayout->setRowStretch(3, 2); 

    statusBar()->addWidget(new QLabel(" SLM: Connected | Algorithm: GS "));
}

void MainWindow::createMenus() {
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *settingsAction = fileMenu->addAction("Hardware Settings...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);

    QAction *corrAction = fileMenu->addAction("Load SLM Correction Mask...");
    connect(corrAction, &QAction::triggered, this, &MainWindow::loadCorrectionFile);

    QAction *clearCorrAction = fileMenu->addAction("Clear SLM Correction");
    connect(clearCorrAction, &QAction::triggered, this, &MainWindow::clearCorrectionMask);

    QAction *sourceAction = fileMenu->addAction("Source Intensity...");
    connect(sourceAction, &QAction::triggered, this, &MainWindow::openSourceIntensityDialog);

    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu("&View");
    QAction *themeAction = viewMenu->addAction("Toggle Light/Dark Theme");
    connect(themeAction, &QAction::triggered, this, &MainWindow::toggleTheme);

    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *holoAction = toolsMenu->addAction("Create Hologram...");
    connect(holoAction, &QAction::triggered, this, &MainWindow::openHologramGenerator);

    QAction *benchmarkAction = toolsMenu->addAction("Compute Benchmark...");
    connect(benchmarkAction, &QAction::triggered, this, &MainWindow::openBenchmarkDialog);

    monitorSelectionMenu = toolsMenu->addMenu("Select Monitor");
    monitorActionGroup = new QActionGroup(this);
    monitorActionGroup->setExclusive(true);
    connect(monitorSelectionMenu, &QMenu::aboutToShow, this, &MainWindow::refreshMonitorSelectionMenu);
    connect(monitorActionGroup, &QActionGroup::triggered, this, &MainWindow::onMonitorActionTriggered);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::showAboutDialog() {
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("Unversioned build")
        : QStringLiteral("Version %1").arg(QCoreApplication::applicationVersion());

    QMessageBox::about(
        this,
        QStringLiteral("About Holographic Optical Tweezer Control"),
        QStringLiteral(
            "Holographic Optical Tweezer Control\n\n"
            "%1\n\n"
            "Use this version note to track which build is currently running.")
            .arg(version));
}

void MainWindow::createMonitors(QGridLayout *layout) {
    QLabel *t1 = new QLabel("Target Pattern (Interactive Grid)"); t1->setAlignment(Qt::AlignCenter);
    QLabel *t2 = new QLabel("Phase Mask"); t2->setAlignment(Qt::AlignCenter);
    QLabel *t3 = new QLabel("Live Camera Feed"); t3->setAlignment(Qt::AlignCenter);
    layout->addWidget(t1, 0, 0); layout->addWidget(t2, 0, 1); layout->addWidget(t3, 0, 2);

    // Create grid title bar with maximize button
    gridTitleBar = new QWidget();
    QHBoxLayout *titleBarLayout = new QHBoxLayout(gridTitleBar);
    titleBarLayout->setContentsMargins(5, 3, 5, 3);
    gridTitleLabel = new QLabel("Grid View");
    titleBarLayout->addWidget(gridTitleLabel);
    titleBarLayout->addStretch();
    
    gridMaxMinBtn = new QPushButton();
    gridMaxMinBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    gridMaxMinBtn->setMaximumWidth(28);
    gridMaxMinBtn->setMaximumHeight(20);
    gridMaxMinBtn->setToolTip("Enlarge grid view");
    gridMaxMinBtn->setStyleSheet("padding: 0px;");
    titleBarLayout->addWidget(gridMaxMinBtn);
    gridTitleBar->setLayout(titleBarLayout);
    gridTitleBar->setObjectName("gridTitleBar");
    
    layout->addWidget(gridTitleBar, 0, 0);
    connect(gridMaxMinBtn, &QPushButton::clicked, this, &MainWindow::toggleGridEnlarged);

    // Create grid with camera resolution dimensions
    targetGridWidget = new TargetGridWidget(camWidth, camHeight);
    targetGridWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); 
    targetGridWidget->setMinimumSize(300, 200); 
    layout->addWidget(targetGridWidget, 1, 0);

    // Wrapper for column 1 (phase mask)
    QWidget *phaseColumn = new QWidget();
    QVBoxLayout *phaseColLayout = new QVBoxLayout(phaseColumn);
    phaseColLayout->setContentsMargins(0, 0, 0, 0);
    phaseColLayout->setSpacing(5);
    
    phaseMaskLabel = new QLabel();
    phaseMaskLabel->setObjectName("phaseMaskLabel");
    phaseMaskLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); 
    phaseMaskLabel->setMinimumSize(300, 200);
    phaseMaskLabel->setScaledContents(true);  // Auto-scale pixmap with label size
    phaseColLayout->addWidget(phaseMaskLabel);
    
    // Phase mask tools
    QHBoxLayout *phaseTools = new QHBoxLayout();
    resolutionLabel = new QLabel(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
    phaseTools->addWidget(resolutionLabel);
    phaseTools->addStretch();
    
    previewCorrectionCb = new QCheckBox("Show Correction");
    previewCorrectionCb->setEnabled(false); 
    phaseTools->addWidget(previewCorrectionCb);
    
    saveMaskBtn = new QPushButton("Save Mask");
    phaseTools->addWidget(saveMaskBtn);
    
    phaseColLayout->addLayout(phaseTools);
    phaseColumn->setLayout(phaseColLayout);
    layout->addWidget(phaseColumn, 1, 1);

    // Wrapper for column 2 (camera feed)
    QWidget *cameraColumn = new QWidget();
    QVBoxLayout *cameraColLayout = new QVBoxLayout(cameraColumn);
    cameraColLayout->setContentsMargins(0, 0, 0, 0);
    cameraColLayout->setSpacing(5);

    QHBoxLayout *cameraPreviewRouteLayout = new QHBoxLayout();
    cameraPreviewMonitorCombo = new QComboBox();
    cameraPreviewMonitorCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cameraPreviewMonitorCombo->setPlaceholderText("Select monitor");
    cameraPreviewToggleBtn = new QPushButton("Show On Monitor");
    cameraPreviewToggleBtn->setCheckable(true);
    cameraPreviewRouteLayout->addWidget(cameraPreviewMonitorCombo, 1);
    cameraPreviewRouteLayout->addWidget(cameraPreviewToggleBtn);
    cameraColLayout->addLayout(cameraPreviewRouteLayout);
    
    cameraFeedLabel = new QLabel("Camera Feed (Offline)");
    cameraFeedLabel->setObjectName("cameraFeedLabel");
    cameraFeedLabel->setAlignment(Qt::AlignCenter);
    cameraFeedLabel->setScaledContents(false);  // Keep pixmap geometry stable for precise hover mapping
    cameraFeedLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); 
    cameraFeedLabel->setMinimumSize(300, 200); 
    cameraFeedLabel->setMouseTracking(true);
    cameraFeedLabel->setToolTip("Left-drag to zoom. Right-click to undo one zoom step. Double-click to reset zoom.");
    cameraColLayout->addWidget(cameraFeedLabel);
    
    // Camera tools
    QHBoxLayout *camTools = new QHBoxLayout();
    fpsLabel = new QLabel("FPS: 0");
    camTools->addWidget(fpsLabel);
    cameraPixelLabel = new QLabel("Camera: --, -- | I: --");
    camTools->addWidget(cameraPixelLabel);
    camTools->addStretch();
    overlayTargetCb = new QCheckBox("Overlay Target");
    overlayTargetCb->setChecked(false);
    camTools->addWidget(overlayTargetCb);
    
    cameraColLayout->addLayout(camTools);
    cameraColumn->setLayout(cameraColLayout);
    layout->addWidget(cameraColumn, 1, 2);

    // Create wrapper for tools row (row 2) - grid column tools
    toolsRow = new QWidget();
    QHBoxLayout *gridToolsLayout = new QHBoxLayout(toolsRow);
    gridToolsLayout->setContentsMargins(0, 0, 0, 0);
    gridToolsLayout->setSpacing(10);
    
    gridToolsLayout->addSpacing(10);  // Spacer for target grid column
    gridHoverLabel = new QLabel("Grid: --, --");
    gridToolsLayout->addWidget(gridHoverLabel);
    gridToolsLayout->addStretch();
    
    toolsRow->setLayout(gridToolsLayout);
    layout->addWidget(toolsRow, 2, 0, 1, 3);
}

void MainWindow::createControls(QGridLayout *layout) {
    QVBoxLayout *leftCol = new QVBoxLayout();
    targetModeTabs = new QTabWidget();
    
    QWidget *manualTab = new QWidget();
    QVBoxLayout *manualLayout = new QVBoxLayout(manualTab);
    QHBoxLayout *manualBtns = new QHBoxLayout();
    addPointsBtn = new QPushButton("Add Points");
    clearAllPointsBtn = new QPushButton("Clear All");
    manualBtns->addWidget(addPointsBtn);
    manualBtns->addWidget(clearAllPointsBtn);
    manualLayout->addLayout(manualBtns);

    trapTable = new QTableWidget(0, 3);
    trapTable->setHorizontalHeaderLabels({"No", "X", "Y"});
    trapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    manualLayout->addWidget(trapTable);
    manualLayout->addStretch();
    
    targetModeTabs->addTab(manualTab, "Manual");
    patternPresetsWidget = new PatternPresetsWidget(camWidth, camHeight);
    targetModeTabs->addTab(patternPresetsWidget, "Pattern");

    QWidget *imageTab = new QWidget();
    QVBoxLayout *imageLayout = new QVBoxLayout(imageTab);
    QHBoxLayout *imageButtons = new QHBoxLayout();
    loadTargetImageBtn = new QPushButton("Load Image");
    clearTargetImageBtn = new QPushButton("Clear Image");
    clearTargetImageBtn->setEnabled(false);
    imageButtons->addWidget(loadTargetImageBtn);
    imageButtons->addWidget(clearTargetImageBtn);
    imageButtons->addStretch();

    targetImageInfoLabel = new QLabel("No image loaded. Will resize to camera resolution and convert to grayscale.");
    targetImageInfoLabel->setWordWrap(true);
    targetImageInfoLabel->setStyleSheet("font-size: 11px;");

    imageLayout->addLayout(imageButtons);
    imageLayout->addWidget(targetImageInfoLabel);
    imageLayout->addStretch();

    targetModeTabs->addTab(imageTab, "Image");

    QWidget *cameraTab = new QWidget();
    QVBoxLayout *cameraLayout = new QVBoxLayout(cameraTab);
    QLabel *cameraTabHelp = new QLabel("Shows the latest live camera frame in the target area.");
    cameraTabHelp->setWordWrap(true);
    cameraTabHelp->setStyleSheet("font-size: 11px;");
    cameraLayout->addWidget(cameraTabHelp);

    QHBoxLayout *cameraTabButtons = new QHBoxLayout();
    cameraTabAddPointsBtn = new QPushButton("Add Points");
    cameraTabClearAllPointsBtn = new QPushButton("Clear All");
    cameraTabButtons->addWidget(cameraTabAddPointsBtn);
    cameraTabButtons->addWidget(cameraTabClearAllPointsBtn);
    cameraLayout->addLayout(cameraTabButtons);

    cameraTrapTable = new QTableWidget(0, 3);
    cameraTrapTable->setHorizontalHeaderLabels({"No", "X", "Y"});
    cameraTrapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    cameraLayout->addWidget(cameraTrapTable);
    cameraLayout->addStretch();
    targetModeTabs->addTab(cameraTab, "Camera");

    animationTab = new QWidget();
    QVBoxLayout *animationTabLayout = new QVBoxLayout(animationTab);
    animationTabLayout->setContentsMargins(0, 0, 0, 0);
    animationTabLayout->setSpacing(0);

    animationScrollArea = new QScrollArea(animationTab);
    animationScrollArea->setWidgetResizable(true);
    animationScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    animationScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    animationScrollArea->setFrameShape(QFrame::NoFrame);

    animationContentWidget = new QWidget(animationScrollArea);
    QVBoxLayout *animationLayout = new QVBoxLayout(animationContentWidget);
    animationLayout->setContentsMargins(6, 6, 6, 6);
    animationLayout->setSpacing(8);

    QGroupBox *animationConfigGroup = new QGroupBox("Animation Settings");
    QFormLayout *animationForm = new QFormLayout();
    animationForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    animationForm->setFormAlignment(Qt::AlignTop);
    animationForm->setVerticalSpacing(6);

    animationPresetCombo = new QComboBox();
    animationPresetCombo->addItems({"Circle", "Triangle"});

    animationFpsSpin = new QSpinBox();
    animationFpsSpin->setRange(1, 240);
    animationFpsSpin->setValue(30);

    animationFrameCountSpin = new QSpinBox();
    animationFrameCountSpin->setRange(1, 1000);
    animationFrameCountSpin->setValue(60);

    animationParticlesSpin = new QSpinBox();
    animationParticlesSpin->setRange(1, 2000);
    animationParticlesSpin->setValue(24);

    animationRealtimeCheck = new QCheckBox("Realtime");
    animationRealtimeCheck->setChecked(true);

    animationForm->addRow("Preset:", animationPresetCombo);
    animationForm->addRow("Frame rate (FPS):", animationFpsSpin);
    animationForm->addRow("No. of frames:", animationFrameCountSpin);
    animationForm->addRow("No. of particles:", animationParticlesSpin);
    animationForm->addRow("Mode:", animationRealtimeCheck);

    animationParamsStack = new QStackedWidget();
    animationParamsStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    animationParamsStack->setMinimumHeight(170);

    QWidget *circleAnimPage = new QWidget();
    QFormLayout *circleAnimForm = new QFormLayout(circleAnimPage);
    circleAnimForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    circleAnimForm->setFormAlignment(Qt::AlignTop);
    circleAnimForm->setVerticalSpacing(6);
    animCircleRadiusFromSpin = new QDoubleSpinBox();
    animCircleRadiusToSpin = new QDoubleSpinBox();
    const double maxAnimRadius = qMax(1.0, static_cast<double>(qMin(camWidth, camHeight)) / 2.0);
    animCircleRadiusFromSpin->setRange(1.0, maxAnimRadius);
    animCircleRadiusToSpin->setRange(1.0, maxAnimRadius);
    animCircleRadiusFromSpin->setValue(qMin(80.0, maxAnimRadius));
    animCircleRadiusToSpin->setValue(qMin(180.0, maxAnimRadius));
    circleAnimForm->addRow("Radius from:", animCircleRadiusFromSpin);
    circleAnimForm->addRow("Radius to:", animCircleRadiusToSpin);

    QWidget *triangleAnimPage = new QWidget();
    QFormLayout *triangleAnimForm = new QFormLayout(triangleAnimPage);
    triangleAnimForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    triangleAnimForm->setFormAlignment(Qt::AlignTop);
    triangleAnimForm->setVerticalSpacing(6);
    animTriangleScaleFromSpin = new QDoubleSpinBox();
    animTriangleScaleToSpin = new QDoubleSpinBox();
    animTriangleRotationFromSpin = new QDoubleSpinBox();
    animTriangleRotationToSpin = new QDoubleSpinBox();
    animTriangleScaleFromSpin->setRange(1.0, maxAnimRadius);
    animTriangleScaleToSpin->setRange(1.0, maxAnimRadius);
    animTriangleScaleFromSpin->setValue(qMin(80.0, maxAnimRadius));
    animTriangleScaleToSpin->setValue(qMin(180.0, maxAnimRadius));
    animTriangleRotationFromSpin->setRange(-3600.0, 3600.0);
    animTriangleRotationToSpin->setRange(-3600.0, 3600.0);
    animTriangleRotationFromSpin->setValue(0.0);
    animTriangleRotationToSpin->setValue(360.0);
    triangleAnimForm->addRow("Scale from:", animTriangleScaleFromSpin);
    triangleAnimForm->addRow("Scale to:", animTriangleScaleToSpin);
    triangleAnimForm->addRow("Rotation from (deg):", animTriangleRotationFromSpin);
    triangleAnimForm->addRow("Rotation to (deg):", animTriangleRotationToSpin);

    animationParamsStack->addWidget(circleAnimPage);
    animationParamsStack->addWidget(triangleAnimPage);
    animationForm->addRow("Preset params:", animationParamsStack);

    animationConfigGroup->setLayout(animationForm);
    animationConfigGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    QHBoxLayout *animationButtons = new QHBoxLayout();
    animationGenerateBtn = new QPushButton("Generate Sequence");
    animationPlaySendBtn = new QPushButton("Play/Send");
    animationStopBtn = new QPushButton("Stop");
    animationResetBtn = new QPushButton("Reset");
    animationStopBtn->setEnabled(false);
    animationButtons->addWidget(animationGenerateBtn);
    animationButtons->addWidget(animationPlaySendBtn);
    animationButtons->addWidget(animationStopBtn);
    animationButtons->addWidget(animationResetBtn);

    QGroupBox *animationPreviewGroup = new QGroupBox("Preview");
    QHBoxLayout *animationPreviewLayout = new QHBoxLayout(animationPreviewGroup);
    animationIntensityPreviewLabel = new QLabel("No frame");
    animationIntensityPreviewLabel->setAlignment(Qt::AlignCenter);
    animationIntensityPreviewLabel->setMinimumSize(180, 120);
    animationIntensityPreviewLabel->setStyleSheet("background-color: black; border: 1px solid #555;");
    animationCameraPreviewLabel = new QLabel("No frame");
    animationCameraPreviewLabel->setAlignment(Qt::AlignCenter);
    animationCameraPreviewLabel->setMinimumSize(180, 120);
    animationCameraPreviewLabel->setStyleSheet("background-color: black; border: 1px solid #555;");
    animationPreviewLayout->addWidget(animationIntensityPreviewLabel);
    animationPreviewLayout->addWidget(animationCameraPreviewLabel);

    animationLayout->addWidget(animationConfigGroup);
    animationLayout->addLayout(animationButtons);
    animationLayout->addWidget(animationPreviewGroup);
    animationLayout->addStretch(1);

    animationScrollArea->setWidget(animationContentWidget);
    animationTabLayout->addWidget(animationScrollArea);

    targetModeTabs->addTab(animationTab, "Animation");

    pythonTab = new QWidget();
    QVBoxLayout *pythonLayout = new QVBoxLayout(pythonTab);
    pythonLayout->setContentsMargins(6, 6, 6, 6);
    pythonLayout->setSpacing(8);

    pythonFpsSpin = new QSpinBox();
    pythonFpsSpin->setRange(1, 240);
    pythonFpsSpin->setValue(30);

    pythonFrameCountSpin = new QSpinBox();
    pythonFrameCountSpin->setRange(1, 1000);
    pythonFrameCountSpin->setValue(120);

    pythonMaxPointsSpin = new QSpinBox();
    pythonMaxPointsSpin->setRange(1, 2000);
    pythonMaxPointsSpin->setValue(48);

    pythonRealtimeCheck = new QCheckBox("Realtime");
    pythonRealtimeCheck->setChecked(true);

    pythonTrapSelectorCombo = new QComboBox();
    pythonTrapSelectorCombo->addItem("None", -1);

    pythonCodeEditor = new PythonCodeEditor();
    pythonCodeEditor->setPlaceholderText("def build_frames(frame_count, width, height):\\n    return [[[0, 0]]]");
    pythonCodeEditor->setMinimumHeight(220);
    pythonCodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pythonCodeEditor->setTabStopDistance(4 * fontMetrics().horizontalAdvance(' '));
    pythonCodeEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pythonCodeEditor->setPlainText(defaultPythonScriptTemplate());
    new PythonSyntaxHighlighter(pythonCodeEditor->document());

    QHBoxLayout *pythonButtons = new QHBoxLayout();
    pythonGenerateBtn = new QPushButton("Run Code");
    pythonPlaySendBtn = new QPushButton("Send");
    pythonSaveScriptBtn = new QPushButton("Save .py");
    pythonLoadScriptBtn = new QPushButton("Load .py");
    pythonButtons->addWidget(pythonGenerateBtn);
    pythonButtons->addWidget(pythonPlaySendBtn);
    pythonButtons->addWidget(pythonSaveScriptBtn);
    pythonButtons->addWidget(pythonLoadScriptBtn);

    pythonLayout->addWidget(pythonCodeEditor, 1);
    pythonLayout->addLayout(pythonButtons);

    targetModeTabs->addTab(pythonTab, "Python");

    leftCol->addWidget(targetModeTabs);

    // Wrap bottom controls into a single widget for easy hide/show
    controlsRow = new QWidget();
    QHBoxLayout *controlsRowLayout = new QHBoxLayout(controlsRow);
    controlsRowLayout->setContentsMargins(0, 0, 0, 0);
    controlsRowLayout->setSpacing(10);

    // Left column widget
    QWidget *leftColWidget = new QWidget();
    leftColWidget->setLayout(leftCol);
    controlsRowLayout->addWidget(leftColWidget, 1);

    QVBoxLayout *midCol = new QVBoxLayout();
    QGroupBox *algoGroup = new QGroupBox("Algorithm Settings");
    QFormLayout *algoForm = new QFormLayout();

    algorithmCombo = new QComboBox();
    algorithmCombo->addItems({"Gerchberg-Saxton", "Weighted GS", "Random Mask Encoding (Paper)"});

    iterationsLabel = new QLabel("Iterations:");
    iterationsSpin = new ArrowSpinBox();
    iterationsSpin->setRange(1, 1000);
    iterationsSpin->setValue(20);

    relaxationLabel = new QLabel("Relaxation:");
    relaxationSpin = new ArrowDoubleSpinBox();
    relaxationSpin->setRange(0.0, 1.0);
    relaxationSpin->setSingleStep(0.05);
    relaxationSpin->setDecimals(2);
    relaxationSpin->setValue(0.50);

    generateGsBtn = new QPushButton("Generate GS Mask");

    algoForm->addRow("Algorithm:", algorithmCombo);
    algoForm->addRow(iterationsLabel, iterationsSpin);
    algoForm->addRow(relaxationLabel, relaxationSpin);
    algoForm->addRow(generateGsBtn);
    algoGroup->setLayout(algoForm);

    midCol->addWidget(algoGroup);
    midCol->addStretch();

    QWidget *midColWidget = new QWidget();
    midColWidget->setLayout(midCol);
    controlsRowLayout->addWidget(midColWidget, 1);

    QVBoxLayout *rightCol = new QVBoxLayout();
    
    QGroupBox *camGroup = new QGroupBox("Camera Control");
    QFormLayout *camForm = new QFormLayout();
    camSelect = new QComboBox(); 
    camForm->addRow("Camera:", camSelect);
    
    QHBoxLayout *captureLayout = new QHBoxLayout();
    captureImageBtn = new QPushButton("Save Image");
    recordVideoBtn = new QPushButton("Record Video");
    recordVideoBtn->setCheckable(true); 
    recordVideoBtn->setStyleSheet("QPushButton:checked { background-color: #aa0000; color: white; border: 1px solid #ff0000; }");
    recordTimeLabel = new QLabel("00:00");
    recordTimeLabel->setStyleSheet("color: #ff4444; font-weight: bold; font-family: monospace;");
    recordTimeLabel->setVisible(false);
    recordTimeHideTimer = new QTimer(this);
    recordTimeHideTimer->setSingleShot(true);
    recordTimeHideTimer->setInterval(2000);
    connect(recordTimeHideTimer, &QTimer::timeout, this, [this]() {
        if (recordTimeLabel && recordVideoBtn && !recordVideoBtn->isChecked()) {
            recordTimeLabel->setVisible(false);
            if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
                updateExternalCameraPreview();
            }
        }
    });
    captureLayout->addWidget(captureImageBtn);
    captureLayout->addWidget(recordVideoBtn);
    captureLayout->addWidget(recordTimeLabel);
    camForm->addRow("Capture:", captureLayout);
    
    QHBoxLayout *camBtns = new QHBoxLayout();
    camStartBtn = new QPushButton("Start Feed");
    camStopBtn = new QPushButton("Stop Feed");
    camBtns->addWidget(camStartBtn);
    camBtns->addWidget(camStopBtn);
    camForm->addRow(camBtns);
    camGroup->setLayout(camForm);

    QGroupBox *slmGroup = new QGroupBox("SLM Control");
    QVBoxLayout *slmLayout = new QVBoxLayout();
    QLabel *slmStatus = new QLabel("SLM: Connected");
    slmStatus->setStyleSheet("color: #4CAF50; font-weight: bold;");
    slmLayout->addWidget(slmStatus);
    
    loadPhaseBtn = new QPushButton("Load Phase Mask");
    sendSlmBtn = new QPushButton("Send to SLM");
    clearSlmBtn = new QPushButton("Clear SLM");

    slmLayout->addWidget(loadPhaseBtn);
    slmLayout->addWidget(sendSlmBtn);
    slmLayout->addWidget(clearSlmBtn);
    
    slmGroup->setLayout(slmLayout);

    rightCol->addWidget(camGroup);
    rightCol->addWidget(slmGroup);
    rightCol->addStretch();

    QWidget *rightColWidget = new QWidget();
    rightColWidget->setLayout(rightCol);
    controlsRowLayout->addWidget(rightColWidget, 1);

    layout->addWidget(controlsRow, 3, 0, 1, 3);
}

void MainWindow::setupConnections() {
    if (cameraFeedLabel) {
        cameraFeedLabel->installEventFilter(this);
    }
    if (targetGridWidget && targetGridWidget->viewport()) {
        targetGridWidget->setMouseTracking(true);
        targetGridWidget->viewport()->setMouseTracking(true);
        targetGridWidget->viewport()->installEventFilter(this);
    }

    for (const QString &camName : camManager->getCameraNames()) {
        camSelect->addItem(camName);
    }

    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(saveMaskBtn, &QPushButton::clicked, this, &MainWindow::savePhaseMask);
    connect(loadTargetImageBtn, &QPushButton::clicked, this, &MainWindow::loadTargetImage);
    connect(clearTargetImageBtn, &QPushButton::clicked, this, &MainWindow::clearTargetImage);
    connect(patternPresetsWidget, &PatternPresetsWidget::patternGenerated, this, &MainWindow::onPatternGenerated);
    connect(animationPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAnimationPresetChanged);
    connect(animationGenerateBtn, &QPushButton::clicked, this, &MainWindow::onGenerateAnimationSequenceClicked);
    connect(animationPlaySendBtn, &QPushButton::clicked, this, &MainWindow::onPlayAnimationClicked);
    connect(animationStopBtn, &QPushButton::clicked, this, &MainWindow::onStopAnimationClicked);
    connect(animationResetBtn, &QPushButton::clicked, this, &MainWindow::onResetAnimationClicked);
    connect(pythonGenerateBtn, &QPushButton::clicked, this, &MainWindow::onGeneratePythonSequenceClicked);
    connect(pythonPlaySendBtn, &QPushButton::clicked, this, &MainWindow::onPlayPythonSequenceClicked);
    connect(pythonStopBtn, &QPushButton::clicked, this, &MainWindow::onStopPythonSequenceClicked);
    connect(pythonResetBtn, &QPushButton::clicked, this, &MainWindow::onResetPythonSequenceClicked);
    connect(pythonSaveScriptBtn, &QPushButton::clicked, this, &MainWindow::onSavePythonScriptClicked);
    connect(pythonLoadScriptBtn, &QPushButton::clicked, this, &MainWindow::onLoadPythonScriptClicked);
    connect(pythonTrapSelectorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPythonTrapSelectionChanged);

    connect(algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAlgorithmSelectionChanged);
    connect(generateGsBtn, &QPushButton::clicked, this, &MainWindow::onGenerateGsMaskClicked);
    
    // Grid Widget connections
    connect(targetGridWidget, &TargetGridWidget::pointAdded, this, &MainWindow::onGridPointAdded);
    connect(targetGridWidget, &TargetGridWidget::pointMoved, this, &MainWindow::onGridPointMoved);
    connect(targetGridWidget, &TargetGridWidget::pointRemoved, this, &MainWindow::onGridPointRemoved);
    connect(targetGridWidget, &TargetGridWidget::pointSelected, this, &MainWindow::onGridPointSelected);
    connect(targetGridWidget, &TargetGridWidget::pointDeselected, this, &MainWindow::onGridPointDeselected);
    connect(trapTable, &QTableWidget::itemChanged, this, &MainWindow::onTrapTableItemChanged);
    connect(cameraTrapTable, &QTableWidget::itemChanged, this, &MainWindow::onTrapTableItemChanged);
    // Manual tab button connections
    connect(addPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->addPoint(QPointF(0, 0));
        targetGridWidget->setFocus();
    });
    connect(cameraTabAddPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->addPoint(QPointF(0, 0));
        targetGridWidget->setFocus();
    });

    connect(clearAllPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->clearAllPoints();
        gridPointData.clear();
        clearPointTables();
        selectedPointId = -1;
        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }
    });
    connect(cameraTabClearAllPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->clearAllPoints();
        gridPointData.clear();
        clearPointTables();
        selectedPointId = -1;
        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }
    });

    connect(camSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), camManager, &CameraManager::changeCamera);
    connect(camStartBtn, &QPushButton::clicked, camManager, &CameraManager::startCamera);
    connect(camStopBtn, &QPushButton::clicked, camManager, &CameraManager::stopCamera);
    connect(camStopBtn, &QPushButton::clicked, this, &MainWindow::handleCameraFeedStopped);
    connect(captureImageBtn, &QPushButton::clicked, camManager, &CameraManager::captureImage);
    connect(recordVideoBtn, &QPushButton::toggled, camManager, &CameraManager::toggleRecording);
    connect(recordVideoBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (!recordTimeHideTimer || !recordTimeLabel) {
            return;
        }
        if (checked) {
            recordTimeHideTimer->stop();
            recordTimeLabel->setVisible(true);
        } else {
            recordTimeHideTimer->start();
        }
        if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
            updateExternalCameraPreview();
        }
    });

    connect(cameraPreviewMonitorCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &MainWindow::onCameraPreviewMonitorChanged);
    connect(cameraPreviewToggleBtn, &QPushButton::toggled, this, &MainWindow::onCameraPreviewToggled);

    QShortcut *hideCameraPreviewShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    hideCameraPreviewShortcut->setContext(Qt::ApplicationShortcut);
    connect(hideCameraPreviewShortcut, &QShortcut::activated, this, [this]() {
        if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
            cameraPreviewToggleBtn->setChecked(false);
        }
    });

    auto addGlobalPointShortcut = [this](const QKeySequence &sequence, auto handler) {
        QShortcut *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };

    addGlobalPointShortcut(QKeySequence(Qt::Key_Up), [this]() { moveSelectedPointByKeyboard(0, 1); });
    addGlobalPointShortcut(QKeySequence(Qt::Key_Down), [this]() { moveSelectedPointByKeyboard(0, -1); });
    addGlobalPointShortcut(QKeySequence(Qt::Key_Left), [this]() { moveSelectedPointByKeyboard(-1, 0); });
    addGlobalPointShortcut(QKeySequence(Qt::Key_Right), [this]() { moveSelectedPointByKeyboard(1, 0); });
    addGlobalPointShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Up), [this]() { moveSelectedPointByKeyboard(0, 5); });
    addGlobalPointShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Down), [this]() { moveSelectedPointByKeyboard(0, -5); });
    addGlobalPointShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left), [this]() { moveSelectedPointByKeyboard(-5, 0); });
    addGlobalPointShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right), [this]() { moveSelectedPointByKeyboard(5, 0); });
    addGlobalPointShortcut(QKeySequence(Qt::Key_Delete), [this]() { removeSelectedPointByKeyboard(); });
    addGlobalPointShortcut(QKeySequence(Qt::Key_Backspace), [this]() { removeLastCreatedPointByKeyboard(); });

    connect(camManager, &CameraManager::frameReady, this, [this](const QImage &image) {
        cameraFeedActive = true;
        updateCameraFeed(image);
    });
    connect(camManager, &CameraManager::statusMessage, this, [this](const QString &msg){
        statusBar()->showMessage(msg);
    });
    connect(camManager, &CameraManager::recordingTimeUpdated, this, &MainWindow::onRecordingTimeUpdated);
    connect(camManager, &CameraManager::fpsUpdated, this, &MainWindow::onFPSUpdated);
    connect(overlayTargetCb, &QCheckBox::toggled, this, [this](bool) {
        if (!lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        } else {
            clearXpSenderOverlay();
        }
    });

    if (camSelect->count() > 0) camManager->changeCamera(0);

    // SLM Connections
    connect(loadPhaseBtn, &QPushButton::clicked, this, &MainWindow::loadPhasePattern);
    connect(sendSlmBtn, &QPushButton::clicked, this, &MainWindow::onSendToSlmRequested);
    connect(clearSlmBtn, &QPushButton::clicked, this, &MainWindow::clearSLM);
    connect(previewCorrectionCb, &QCheckBox::toggled, this, &MainWindow::updatePhasePreview);

    gsAutoRunTimer = new QTimer(this);
    gsAutoRunTimer->setSingleShot(true);
    gsAutoRunTimer->setInterval(kGsAutoRunDebounceMs);
    connect(gsAutoRunTimer, &QTimer::timeout, this, &MainWindow::onGsAutoRunTimeout);

    animationTimer = new QTimer(this);
    animationTimer->setSingleShot(false);
    connect(animationTimer, &QTimer::timeout, this, &MainWindow::onAnimationTimerTimeout);

    updateAlgorithmSettingsUi();
    onAnimationPresetChanged(animationPresetCombo ? animationPresetCombo->currentIndex() : 0);
    updateAnimationControlsEnabledState();

    if (animationPresetCombo && animationFpsSpin && animationFrameCountSpin && animationParticlesSpin &&
        animationRealtimeCheck && animCircleRadiusFromSpin && animCircleRadiusToSpin &&
        animTriangleScaleFromSpin && animTriangleScaleToSpin && animTriangleRotationFromSpin &&
        animTriangleRotationToSpin && animationGenerateBtn && animationPlaySendBtn &&
        animationStopBtn && animationResetBtn) {
        QWidget::setTabOrder(animationPresetCombo, animationFpsSpin);
        QWidget::setTabOrder(animationFpsSpin, animationFrameCountSpin);
        QWidget::setTabOrder(animationFrameCountSpin, animationParticlesSpin);
        QWidget::setTabOrder(animationParticlesSpin, animationRealtimeCheck);
        QWidget::setTabOrder(animationRealtimeCheck, animCircleRadiusFromSpin);
        QWidget::setTabOrder(animCircleRadiusFromSpin, animCircleRadiusToSpin);
        QWidget::setTabOrder(animCircleRadiusToSpin, animTriangleScaleFromSpin);
        QWidget::setTabOrder(animTriangleScaleFromSpin, animTriangleScaleToSpin);
        QWidget::setTabOrder(animTriangleScaleToSpin, animTriangleRotationFromSpin);
        QWidget::setTabOrder(animTriangleRotationFromSpin, animTriangleRotationToSpin);
        QWidget::setTabOrder(animTriangleRotationToSpin, animationGenerateBtn);
        QWidget::setTabOrder(animationGenerateBtn, animationPlaySendBtn);
        QWidget::setTabOrder(animationPlaySendBtn, animationStopBtn);
        QWidget::setTabOrder(animationStopBtn, animationResetBtn);
    }

    if (pythonFpsSpin && pythonFrameCountSpin && pythonMaxPointsSpin && pythonRealtimeCheck &&
        pythonTrapSelectorCombo && pythonCodeEditor && pythonGenerateBtn && pythonPlaySendBtn &&
        pythonStopBtn && pythonResetBtn) {
        QWidget::setTabOrder(pythonFpsSpin, pythonFrameCountSpin);
        QWidget::setTabOrder(pythonFrameCountSpin, pythonMaxPointsSpin);
        QWidget::setTabOrder(pythonMaxPointsSpin, pythonRealtimeCheck);
        QWidget::setTabOrder(pythonRealtimeCheck, pythonTrapSelectorCombo);
        QWidget::setTabOrder(pythonTrapSelectorCombo, pythonCodeEditor);
        QWidget::setTabOrder(pythonCodeEditor, pythonGenerateBtn);
        QWidget::setTabOrder(pythonGenerateBtn, pythonPlaySendBtn);
        QWidget::setTabOrder(pythonPlaySendBtn, pythonStopBtn);
        QWidget::setTabOrder(pythonStopBtn, pythonResetBtn);
    }
}

// ==========================================
// CORE SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    int currentCameraRotation = cameraViewRotationDegrees;
    bool currentFlipX = flipCameraX;
    bool currentFlipY = flipCameraY;

    QSettings hwSettings(QCoreApplication::applicationDirPath() + "/hardware_config.ini", QSettings::IniFormat);
    currentCameraRotation = hwSettings.value("Hardware/Camera_ViewRotation", currentCameraRotation).toInt();
    currentFlipX = hwSettings.value("Hardware/Camera_FlipX", currentFlipX).toBool();
    currentFlipY = hwSettings.value("Hardware/Camera_FlipY", currentFlipY).toBool();

    SettingsDialog dialog(slmWidth, slmHeight, slmPixelSize, cameraBackend,
                          camWidth, camHeight, camPixelSize,
                          udpBindIp, udpPort,
                          laserWavelength, fourierFocalLength, cameraImagingMagnification,
                          slmOutputMode, autoRunGsEnabled, autoSendSlmEnabled,
                          gsStartingPhaseMaskMode, gsComputeBackendMode, openClPlatformIndex, openClDeviceIndex, cudaDeviceIndex,
                          currentCameraRotation, currentFlipX, saveCompressed, saveFollowsTransforms, currentFlipY, this);

    auto applyFn = [&]() {
        const int prevSlmWidth = slmWidth;
        const int prevSlmHeight = slmHeight;
        const QMap<int, QPointF> preservedGridPoints = gridPointData;

        slmWidth = dialog.getWidth();
        slmHeight = dialog.getHeight();
        slmPixelSize = dialog.getPixelSize();

        bool backendChanged = (cameraBackend != dialog.getCameraBackend());
        cameraBackend = dialog.getCameraBackend();

        camWidth = dialog.getCamWidth();
        camHeight = dialog.getCamHeight();
        camPixelSize = dialog.getCamPixelSize();
        udpBindIp = dialog.getUdpBindIp();
        udpPort = dialog.getUdpPort();
        laserWavelength = dialog.getWavelength();
        fourierFocalLength = dialog.getFocalLength();
        cameraImagingMagnification = dialog.getCameraImagingMagnification();

        QSettings settings(configPath(), QSettings::IniFormat);

        settings.setValue("Hardware/SLM_Width", slmWidth);
        settings.setValue("Hardware/SLM_Height", slmHeight);
        settings.setValue("Hardware/SLM_PixelSize", slmPixelSize);
        settings.setValue("Hardware/CameraBackend", cameraBackend);
        settings.setValue("Hardware/Cam_Width", camWidth);
        settings.setValue("Hardware/Cam_Height", camHeight);
        settings.setValue("Hardware/Cam_PixelSize", camPixelSize);
        settings.setValue("Hardware/UDP_BindIP", udpBindIp);
        settings.setValue("Hardware/UDP_Port", udpPort);
        settings.setValue("Optical/Wavelength", laserWavelength);
        settings.setValue("Optical/CameraImagingMagnification", cameraImagingMagnification);
        slmOutputMode = dialog.getSlmOutputMode();
        autoRunGsEnabled = dialog.getAutoRunGsEnabled();
        autoSendSlmEnabled = dialog.getAutoSendSlmEnabled();
        gsStartingPhaseMaskMode = dialog.getStartingPhaseMaskMode();
        gsComputeBackendMode = dialog.getGsComputeBackendMode();

        openClPlatformIndex = dialog.getOpenClPlatformIndex();
        openClDeviceIndex = dialog.getOpenClDeviceIndex();
        cudaDeviceIndex = dialog.getCudaDeviceIndex();

        settings.setValue("Optical/FocalLength", fourierFocalLength);
        settings.setValue("Hardware/SLM_OutputMode", slmOutputMode);
        settings.setValue("Hardware/AutoRunGS", autoRunGsEnabled);
        settings.setValue("Hardware/AutoSendSLM", autoSendSlmEnabled);
        settings.setValue("Hardware/GS_StartingPhaseMask", gsStartingPhaseMaskMode);
        settings.setValue("Hardware/GS_ComputeBackend", gsComputeBackendMode);
        settings.setValue("Hardware/GS_OpenCLPlatformIndex", openClPlatformIndex);
        settings.setValue("Hardware/GS_OpenCLDeviceIndex", openClDeviceIndex);
        settings.setValue("Hardware/GS_CUDADeviceIndex", cudaDeviceIndex);
        settings.sync();

        // Save UI settings to hardware_config.ini
        cameraViewRotationDegrees = dialog.getCameraRotation();
        flipCameraX = dialog.getFlipX();
        flipCameraY = dialog.getFlipY();
        saveCompressed = dialog.getSaveCompressed();
        saveFollowsTransforms = dialog.getSaveFollowsTransforms();

        hwSettings.setValue("Hardware/Camera_ViewRotation", cameraViewRotationDegrees);
        hwSettings.setValue("Hardware/Camera_FlipX", flipCameraX);
        hwSettings.setValue("Hardware/Camera_FlipY", flipCameraY);
        hwSettings.setValue("Hardware/save_compressed", saveCompressed);
        hwSettings.setValue("Camera/save_follows_transforms", saveFollowsTransforms);
        hwSettings.sync();

        if (!lastCameraFrame.isNull()) {
            // Apply transformations immediately using the cached unflipped/unrotated frame
            updateCameraFeed(lastCameraFrame);
        }

        if (!autoRunGsEnabled && gsAutoRunTimer) {
            gsAutoRunTimer->stop();
        }

        resolutionLabel->setText(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));

        // Update grid resolution dynamically
        targetGridWidget->setGridResolution(camWidth, camHeight);
        targetGridWidget->centerView();
        suppressGridStatusMessages = true;
        selectedPointId = -1;
        updateGridHoverReadout(QPoint(), false);
        gridPointData.clear();
        clearPointTables();
        targetGridWidget->clearAllPoints();
        for (auto it = preservedGridPoints.cbegin(); it != preservedGridPoints.cend(); ++it) {
            targetGridWidget->addPoint(clampPointToCameraBounds(it.value(), camWidth, camHeight), it.key());
        }
        suppressGridStatusMessages = false;
        if (patternPresetsWidget) {
            patternPresetsWidget->setCameraResolution(camWidth, camHeight);
        }
        const double maxAnimRadius = qMax(1.0, static_cast<double>(qMin(camWidth, camHeight)) / 2.0);
        if (animCircleRadiusFromSpin) {
            animCircleRadiusFromSpin->setRange(1.0, maxAnimRadius);
            animCircleRadiusFromSpin->setValue(qMin(animCircleRadiusFromSpin->value(), maxAnimRadius));
        }
        if (animCircleRadiusToSpin) {
            animCircleRadiusToSpin->setRange(1.0, maxAnimRadius);
            animCircleRadiusToSpin->setValue(qMin(animCircleRadiusToSpin->value(), maxAnimRadius));
        }
        if (animTriangleScaleFromSpin) {
            animTriangleScaleFromSpin->setRange(1.0, maxAnimRadius);
            animTriangleScaleFromSpin->setValue(qMin(animTriangleScaleFromSpin->value(), maxAnimRadius));
        }
        if (animTriangleScaleToSpin) {
            animTriangleScaleToSpin->setRange(1.0, maxAnimRadius);
            animTriangleScaleToSpin->setValue(qMin(animTriangleScaleToSpin->value(), maxAnimRadius));
        }
        clearAnimationSequenceState(true);
        updateAnimationControlsEnabledState();

        if (!loadedTargetImageOriginal.isNull()) {
            loadedTargetImageGray = loadedTargetImageOriginal.convertToFormat(QImage::Format_Grayscale8).scaled(
                camWidth, camHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            if (targetImageInfoLabel) {
                targetImageInfoLabel->setText(QString("Loaded image mapped to camera resolution: %1 x %2 (8-bit grayscale)")
                    .arg(camWidth).arg(camHeight));
            }

            if (targetModeTabs && targetModeTabs->currentIndex() == kImageTabIndex) {
                targetGridWidget->setBackgroundImage(loadedTargetImageGray);
                targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::StaticImage);
            }
        }

        const bool slmResolutionChanged = (slmWidth != prevSlmWidth) || (slmHeight != prevSlmHeight);
        const int expectedSourceSize = slmWidth * slmHeight;
        if (!sourceIntensityMap.isEmpty() && (slmResolutionChanged || sourceIntensityMap.size() != expectedSourceSize)) {
            sourceIntensityMap.clear();
            sourceIntensityPreview = QImage();
            sourcePresetName.clear();
            sourceBeamWaistPx = 0.0;
            statusBar()->showMessage("SLM resolution changed. Source intensity invalidated; please re-apply Source Intensity.", 6000);
        }

        if (!backendChanged && camManager) {
            camManager->setUdpConfig(udpBindIp, static_cast<quint16>(udpPort));
        }

        if (backendChanged) {
            QMessageBox::information(this, "Restart Required",
                "You have changed the Camera Engine. Please restart the application for this to take effect.");
        } else {
            statusBar()->showMessage("Settings applied and saved to: " + configPath(), 5000);
        }
    };

    connect(&dialog, &SettingsDialog::applyRequested, this, applyFn);

    if (dialog.exec() == QDialog::Accepted) {
        applyFn();
    }
}

void MainWindow::openHologramGenerator() {
    HologramDialog dialog(slmWidth, slmHeight, this);
    connect(&dialog, &HologramDialog::maskReadyToLoad, this, &MainWindow::receiveHologram);
    connect(&dialog, &HologramDialog::sendToSLMRequested, this, &MainWindow::sendHologramToSLM);
    dialog.exec();
}

void MainWindow::openBenchmarkDialog() {
    // Use current device selections so the dialog defaults match the active backend choices.
    auto *dlg = new ComputeBenchmarkDialog(openClPlatformIndex, openClDeviceIndex, cudaDeviceIndex, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->show();
}

void MainWindow::openSourceIntensityDialog() {
    double defaultWaist = sourceBeamWaistPx;
    if (defaultWaist <= 0.0) {
        defaultWaist = static_cast<double>(qMin(slmWidth, slmHeight)) / 6.0;
    }

    SourceIntensityDialog dialog(slmWidth, slmHeight, sourceIntensityMap, defaultWaist, this);
    connect(&dialog, &SourceIntensityDialog::sourceIntensityApplied, this, &MainWindow::onSourceIntensityApplied);
    dialog.exec();
}

void MainWindow::onSourceIntensityApplied(const QVector<float> &intensityMap,
                                          int width,
                                          int height,
                                          const QString &presetName,
                                          double beamWaistPx,
                                          const QImage &previewImage) {
    const int expectedSize = slmWidth * slmHeight;
    if (width != slmWidth || height != slmHeight || intensityMap.size() != expectedSize) {
        QMessageBox::warning(this, "Source Intensity", "Incoming source intensity size does not match current SLM resolution.");
        return;
    }

    sourceIntensityMap = intensityMap;
    sourceIntensityPreview = previewImage;
    sourcePresetName = presetName;
    sourceBeamWaistPx = beamWaistPx;

    statusBar()->showMessage(QString("Source intensity applied: %1, waist %2 px, array size %3")
                                 .arg(sourcePresetName)
                                 .arg(sourceBeamWaistPx, 0, 'f', 1)
                                 .arg(sourceIntensityMap.size()),
                             5000);
}

bool MainWindow::isGerchbergSaxtonSelected() const {
    return algorithmCombo && algorithmCombo->currentIndex() == kAlgorithmGsIndex;
}

bool MainWindow::isWeightedGsSelected() const {
    return algorithmCombo && algorithmCombo->currentIndex() == kAlgorithmWeightedGsIndex;
}

bool MainWindow::isRandomMaskEncodingSelected() const {
    return algorithmCombo && algorithmCombo->currentIndex() == kAlgorithmRmeIndex;
}

bool MainWindow::isAutoMaskGenerationAlgorithmSelected() const {
    return isGerchbergSaxtonSelected() || isRandomMaskEncodingSelected();
}

QVector<float> MainWindow::defaultGsSourceAmplitude() const {
    const double defaultWaistPx = static_cast<double>(qMin(slmWidth, slmHeight)) / 6.0;
    return GSAlgorithm::buildGaussianSourceAmplitude(slmWidth, slmHeight, defaultWaistPx);
}

void MainWindow::updateAlgorithmSettingsUi() {
    const bool gsSelected = isGerchbergSaxtonSelected();
    const bool wgsSelected = isWeightedGsSelected();
    const bool rmeSelected = isRandomMaskEncodingSelected();

    if (iterationsLabel) {
        iterationsLabel->setVisible(!rmeSelected);
    }
    if (iterationsSpin) {
        iterationsSpin->setVisible(!rmeSelected);
    }

    if (relaxationLabel) {
        relaxationLabel->setVisible(wgsSelected);
    }
    if (relaxationSpin) {
        relaxationSpin->setVisible(wgsSelected);
    }

    if (generateGsBtn) {
        if (gsSelected) {
            generateGsBtn->setText("Generate GS Mask");
        } else if (rmeSelected) {
            generateGsBtn->setText("Generate RME Mask");
        } else {
            generateGsBtn->setText("Generate (WGS unavailable)");
        }
    }
}

void MainWindow::onAlgorithmSelectionChanged(int index) {
    Q_UNUSED(index);

    updateAlgorithmSettingsUi();

    if (!isAutoMaskGenerationAlgorithmSelected()) {
        if (gsAutoRunTimer) {
            gsAutoRunTimer->stop();
        }
        return;
    }

    scheduleGsAutoRun();
}

void MainWindow::onGenerateGsMaskClicked() {
    generateAlgorithmMask(true, GsRunTrigger::ManualButton);
}

void MainWindow::scheduleGsAutoRun() {
    if (!autoRunGsEnabled || !isAutoMaskGenerationAlgorithmSelected() || !gsAutoRunTimer) {
        return;
    }
    if (animationRealtimeRunning || animationPlaybackRunning) {
        return;
    }

    if (gridPointData.isEmpty()) {
        gsAutoRunTimer->stop();
        return;
    }

    gsAutoRunTimer->start();
}

void MainWindow::autoSendToSlmIfEnabled() {
    if (!autoSendSlmEnabled) {
        return;
    }

    if (currentMask.isNull() && correctionMask.isNull()) {
        return;
    }

    sendToSLM();
}
void MainWindow::onGsAutoRunTimeout() {
    if (!autoRunGsEnabled || !isAutoMaskGenerationAlgorithmSelected()) {
        return;
    }
    if (animationRealtimeRunning || animationPlaybackRunning) {
        return;
    }

    if (gridPointData.isEmpty()) {
        return;
    }

    generateAlgorithmMask(false, GsRunTrigger::AutoRunTimer);
}

bool MainWindow::generateAlgorithmMask(bool showWarnings, GsRunTrigger trigger) {
    if (isWeightedGsSelected()) {
        if (showWarnings) {
            QMessageBox::information(this, "Weighted GS", "Weighted GS is not implemented yet.");
        }
        return false;
    }

    if (!isGerchbergSaxtonSelected() && !isRandomMaskEncodingSelected()) {
        if (showWarnings) {
            QMessageBox::warning(this, "Algorithm", "Selected algorithm is not supported.");
        }
        return false;
    }

    if (gridPointData.isEmpty()) {
        if (showWarnings) {
            const QString title = isRandomMaskEncodingSelected() ? "RME Algorithm" : "GS Algorithm";
            QMessageBox::warning(this, title, "No target points found. Add points to the target grid.");
        }
        return false;
    }

    if (isRandomMaskEncodingSelected()) {
        QVector<RMEAlgorithm::RMETargetPoint> targets;
        targets.reserve(gridPointData.size());
        for (auto it = gridPointData.constBegin(); it != gridPointData.constEnd(); ++it) {
            RMEAlgorithm::RMETargetPoint target;
            target.xCamPx = it.value().x();
            target.yCamPx = it.value().y();
            targets.append(target);
        }

        RMEAlgorithm::RMEConfig config;
        config.slmWidth = slmWidth;
        config.slmHeight = slmHeight;
        config.slmPixelSizeUm = slmPixelSize;
        config.camWidth = camWidth;
        config.camHeight = camHeight;
        config.camPixelSizeUm = camPixelSize;
        config.cameraImagingMagnification = cameraImagingMagnification;
        config.wavelengthNm = laserWavelength;
        config.focalLengthMm = fourierFocalLength;

        const RMEAlgorithm::RMEResult result = RMEAlgorithm::runRandomMaskEncoding(config, targets);
        if (!result.success) {
            if (showWarnings) {
                QMessageBox::warning(this, "RME Algorithm", result.error);
            }
            return false;
        }

        currentMask = result.phaseMask8Bit.convertToFormat(QImage::Format_Grayscale8);
        if (currentMask.size() != QSize(slmWidth, slmHeight)) {
            currentMask = currentMask.scaled(slmWidth, slmHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }

        updatePhasePreview();

        QString statusMessage = QString("RME mask generated (non-iterative, %1/%2 valid targets).")
                                    .arg(result.usedTargetCount)
                                    .arg(result.requestedTargetCount);
        if (result.skippedOutsideCameraFov > 0 || result.skippedOutsideSlmBounds > 0) {
            statusMessage += QString(" Skipped (camera/slm): %1/%2.")
                                 .arg(result.skippedOutsideCameraFov)
                                 .arg(result.skippedOutsideSlmBounds);
        }
        statusBar()->showMessage(statusMessage, 4000);
        autoSendToSlmIfEnabled();
        return true;
    }

    const int expectedSourceSize = slmWidth * slmHeight;
    const bool usingDefaultSource = sourceIntensityMap.size() != expectedSourceSize;
    const QVector<float> sourceAmplitude = usingDefaultSource ? defaultGsSourceAmplitude() : sourceIntensityMap;

    QVector<GSAlgorithm::GSTargetPoint> targets;
    targets.reserve(gridPointData.size());

    for (auto it = gridPointData.constBegin(); it != gridPointData.constEnd(); ++it) {
        GSAlgorithm::GSTargetPoint target;
        target.xCamPx = it.value().x();
        target.yCamPx = it.value().y();
        targets.append(target);
    }

    GSAlgorithm::GSConfig config;
    config.slmWidth = slmWidth;
    config.slmHeight = slmHeight;
    config.slmPixelSizeUm = slmPixelSize;
    config.camWidth = camWidth;
    config.camHeight = camHeight;
    config.camPixelSizeUm = camPixelSize;
    config.cameraImagingMagnification = cameraImagingMagnification;
    config.wavelengthNm = laserWavelength;
    config.focalLengthMm = fourierFocalLength;
    config.iterations = iterationsSpin ? iterationsSpin->value() : 20;
    switch (gsComputeBackendMode) {
    case 1:
        config.computeBackend = GSAlgorithm::GSComputeBackend::CPU;
        break;
    case 2:
        config.computeBackend = GSAlgorithm::GSComputeBackend::OpenCL;
        break;
    case 3:
        config.computeBackend = GSAlgorithm::GSComputeBackend::CUDA;
        break;
    case 0:
    default:
        config.computeBackend = GSAlgorithm::GSComputeBackend::Auto;
        break;
    }
    config.openClPlatformIndex = openClPlatformIndex;
    config.openClDeviceIndex = openClDeviceIndex;
    config.cudaDeviceIndex = cudaDeviceIndex;

    switch (gsStartingPhaseMaskMode) {
    case 1:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::BinaryGrating;
        break;
    case 2:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::RandomPhase;
        break;
    case 0:
    default:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::Checkerboard;
        break;
    }

#if HOT_ENABLE_TEMP_GS_PROFILING
    QElapsedTimer gsTimer;
    gsTimer.start();
#endif

    const GSAlgorithm::GSResult result = GSAlgorithm::runGerchbergSaxton(config, sourceAmplitude, targets);

#if HOT_ENABLE_TEMP_GS_PROFILING
    const qint64 elapsedMs = gsTimer.elapsed();
    const double msPerIteration = config.iterations > 0
        ? static_cast<double>(elapsedMs) / static_cast<double>(config.iterations)
        : 0.0;
    const QString triggerLabel = [trigger]() {
        switch (trigger) {
        case GsRunTrigger::ManualButton:
            return QString("manual_button");
        case GsRunTrigger::AutoRunTimer:
            return QString("auto_run_timer");
        case GsRunTrigger::SendToSlmPreRun:
            return QString("send_to_slm_pre_run");
        default:
            return QString("unknown");
        }
    }();

    if (trigger == GsRunTrigger::ManualButton) {
        appendGsRuntimeLogEntry(triggerLabel,
                                targetModeLabelFromIndex(targetModeTabs ? targetModeTabs->currentIndex() : -1),
                                lastGeneratedPatternSummary,
                                lastGeneratedPatternDetails,
                                result.success,
                                result.error,
                                elapsedMs,
                                msPerIteration,
                                config,
                                result,
                                usingDefaultSource,
                                sourcePresetName,
                                sourceBeamWaistPx,
                                targets.size());
    }
#endif

    if (!result.success) {
        if (showWarnings) {
            QMessageBox::warning(this, "GS Algorithm", result.error);
        }
        return false;
    }

    currentMask = result.phaseMask8Bit.convertToFormat(QImage::Format_Grayscale8);
    if (currentMask.size() != QSize(slmWidth, slmHeight)) {
        currentMask = currentMask.scaled(slmWidth, slmHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    updatePhasePreview();

    const QString sourceMsg = usingDefaultSource
        ? "Default Gaussian source used"
        : "Source Intensity map used";
    QString backendUsed = "CPU";
    switch (result.backendUsed) {
    case GSAlgorithm::GSComputeBackendUsed::CUDA:
        backendUsed = "CUDA";
        break;
    case GSAlgorithm::GSComputeBackendUsed::OpenCL:
        backendUsed = "OpenCL";
        break;
    case GSAlgorithm::GSComputeBackendUsed::CPU:
    default:
        backendUsed = "CPU";
        break;
    }
    QString statusMessage = QString("GS mask generated (%1 iterations, %2/%3 valid targets, %4, backend: %5).")
                                .arg(config.iterations)
                                .arg(result.usedTargetCount)
                                .arg(result.requestedTargetCount)
                                .arg(sourceMsg)
                                .arg(backendUsed);
    if (!result.backendInfo.isEmpty()) {
        statusMessage += QString(" Device: %1.").arg(result.backendInfo);
    }
    if (result.fallbackOccurred && !result.fallbackReason.isEmpty()) {
        statusMessage += QString(" Auto-fallback: %1.").arg(result.fallbackReason);
    }
#if HOT_ENABLE_TEMP_GS_PROFILING
    if (trigger == GsRunTrigger::ManualButton) {
        statusMessage += QString(" Runtime: %1 ms (%2 ms/iter).")
            .arg(elapsedMs)
            .arg(msPerIteration, 0, 'f', 3);
    }
#endif
    statusBar()->showMessage(statusMessage, 4000);

    autoSendToSlmIfEnabled();
    return true;
}

bool MainWindow::runGsForTargetPoints(const QVector<QPointF> &points,
                                      int iterationsOverride,
                                      QImage &outMask,
                                      QString *errorOut) {
    if (!isGerchbergSaxtonSelected()) {
        if (errorOut) {
            *errorOut = "Gerchberg-Saxton is required for animation playback in v1. Weighted GS and Random Mask Encoding playback are not supported.";
        }
        return false;
    }

    if (points.isEmpty()) {
        if (errorOut) {
            *errorOut = "Animation frame has no target points.";
        }
        return false;
    }

    const int expectedSourceSize = slmWidth * slmHeight;
    const bool usingDefaultSource = sourceIntensityMap.size() != expectedSourceSize;
    const QVector<float> sourceAmplitude = usingDefaultSource ? defaultGsSourceAmplitude() : sourceIntensityMap;

    QVector<GSAlgorithm::GSTargetPoint> targets;
    targets.reserve(points.size());
    for (const QPointF &p : points) {
        GSAlgorithm::GSTargetPoint target;
        target.xCamPx = p.x();
        target.yCamPx = p.y();
        targets.append(target);
    }

    GSAlgorithm::GSConfig config;
    config.slmWidth = slmWidth;
    config.slmHeight = slmHeight;
    config.slmPixelSizeUm = slmPixelSize;
    config.camWidth = camWidth;
    config.camHeight = camHeight;
    config.camPixelSizeUm = camPixelSize;
    config.cameraImagingMagnification = cameraImagingMagnification;
    config.wavelengthNm = laserWavelength;
    config.focalLengthMm = fourierFocalLength;
    config.iterations = qMax(1, iterationsOverride);
    switch (gsComputeBackendMode) {
    case 1:
        config.computeBackend = GSAlgorithm::GSComputeBackend::CPU;
        break;
    case 2:
        config.computeBackend = GSAlgorithm::GSComputeBackend::OpenCL;
        break;
    case 3:
        config.computeBackend = GSAlgorithm::GSComputeBackend::CUDA;
        break;
    case 0:
    default:
        config.computeBackend = GSAlgorithm::GSComputeBackend::Auto;
        break;
    }
    config.openClPlatformIndex = openClPlatformIndex;
    config.openClDeviceIndex = openClDeviceIndex;
    config.cudaDeviceIndex = cudaDeviceIndex;

    switch (gsStartingPhaseMaskMode) {
    case 1:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::BinaryGrating;
        break;
    case 2:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::RandomPhase;
        break;
    case 0:
    default:
        config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::Checkerboard;
        break;
    }

    const GSAlgorithm::GSResult result = GSAlgorithm::runGerchbergSaxton(config, sourceAmplitude, targets);
    if (!result.success) {
        if (errorOut) {
            *errorOut = result.error;
        }
        return false;
    }

    outMask = result.phaseMask8Bit.convertToFormat(QImage::Format_Grayscale8);
    if (outMask.size() != QSize(slmWidth, slmHeight)) {
        outMask = outMask.scaled(slmWidth, slmHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return true;
}

bool MainWindow::shouldHandleGlobalPointShortcut() const {
    if (!isActiveWindow() || !targetGridWidget || selectedPointId < 0) {
        return false;
    }

    QWidget *focusedWidget = QApplication::focusWidget();
    if (!focusedWidget) {
        return true;
    }

    if (focusedWidget->window() != this) {
        return false;
    }

    return !isEditableInputWidget(focusedWidget);
}

void MainWindow::restoreSelectedPointSelection() {
    if (!targetGridWidget || selectedPointId < 0) {
        return;
    }

    if (!targetGridWidget->selectPoint(selectedPointId)) {
        selectedPointId = -1;
        updateGridHoverReadout(QPoint(), false);
        return;
    }

    if (pointTables().isEmpty()) {
        return;
    }
    selectPointRowInTables(selectedPointId);
}

QList<QTableWidget *> MainWindow::pointTables() const {
    QList<QTableWidget *> tables;
    if (trapTable) {
        tables.append(trapTable);
    }
    if (cameraTrapTable) {
        tables.append(cameraTrapTable);
    }
    return tables;
}

void MainWindow::clearPointTables() {
    for (QTableWidget *table : pointTables()) {
        table->setRowCount(0);
    }
}

void MainWindow::addPointRowToTables(int pointId, const QPointF &pixelCoords) {
    for (QTableWidget *table : pointTables()) {
        const int row = table->rowCount();
        table->insertRow(row);

        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(pointId));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, idItem);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(static_cast<int>(pixelCoords.x()))));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(static_cast<int>(pixelCoords.y()))));
    }
}

void MainWindow::updatePointRowInTables(int pointId, const QPointF &pixelCoords) {
    for (QTableWidget *table : pointTables()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *idItem = table->item(row, 0);
            if (!idItem || idItem->text().toInt() != pointId) {
                continue;
            }

            QTableWidgetItem *xItem = table->item(row, 1);
            QTableWidgetItem *yItem = table->item(row, 2);
            if (!xItem) {
                xItem = new QTableWidgetItem();
                table->setItem(row, 1, xItem);
            }
            if (!yItem) {
                yItem = new QTableWidgetItem();
                table->setItem(row, 2, yItem);
            }
            xItem->setText(QString::number(static_cast<int>(pixelCoords.x())));
            yItem->setText(QString::number(static_cast<int>(pixelCoords.y())));
            break;
        }
    }
}

void MainWindow::removePointRowFromTables(int pointId) {
    for (QTableWidget *table : pointTables()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *idItem = table->item(row, 0);
            if (idItem && idItem->text().toInt() == pointId) {
                table->removeRow(row);
                break;
            }
        }
    }
}

void MainWindow::selectPointRowInTables(int pointId) {
    for (QTableWidget *table : pointTables()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *idItem = table->item(row, 0);
            if (idItem && idItem->text().toInt() == pointId) {
                table->selectRow(row);
                break;
            }
        }
    }
}

void MainWindow::clearPointTableSelections() {
    for (QTableWidget *table : pointTables()) {
        table->clearSelection();
    }
}

void MainWindow::moveSelectedPointByKeyboard(int deltaX, int deltaY) {
    if (!shouldHandleGlobalPointShortcut()) {
        return;
    }

    if (!gridPointData.contains(selectedPointId)) {
        selectedPointId = -1;
        return;
    }

    restoreSelectedPointSelection();
    if (!targetGridWidget->movePointById(selectedPointId, deltaX, deltaY)) {
        selectedPointId = -1;
    }
}

void MainWindow::removeSelectedPointByKeyboard() {
    if (!shouldHandleGlobalPointShortcut()) {
        return;
    }

    if (!targetGridWidget->hasPoint(selectedPointId)) {
        selectedPointId = -1;
        return;
    }

    restoreSelectedPointSelection();
    targetGridWidget->removePoint(selectedPointId);
}

void MainWindow::removeLastCreatedPointByKeyboard() {
    if (!isActiveWindow() || !targetGridWidget) {
        return;
    }

    QWidget *focusedWidget = QApplication::focusWidget();
    if (focusedWidget) {
        if (focusedWidget->window() != this || isEditableInputWidget(focusedWidget)) {
            return;
        }
    }

    if (gridPointData.isEmpty()) {
        return;
    }

    int lastPointId = -1;
    for (auto it = gridPointData.cbegin(); it != gridPointData.cend(); ++it) {
        lastPointId = qMax(lastPointId, it.key());
    }

    if (lastPointId < 0 || !targetGridWidget->hasPoint(lastPointId)) {
        return;
    }

    targetGridWidget->removePoint(lastPointId);
}

void MainWindow::onSendToSlmRequested() {
    if (isAutoMaskGenerationAlgorithmSelected() && !gridPointData.isEmpty()) {
        if (!generateAlgorithmMask(true, GsRunTrigger::SendToSlmPreRun)) {
            return;
        }

        if (autoSendSlmEnabled) {
            return;
        }
    }

    sendToSLM();
}
void MainWindow::savePhaseMask() {
    // construct the image we actually want to write rather than relying on the scaled
    // widget pixmap (which may be resized to fit the label)
    QImage saveImg = composeFullResolutionMask(previewCorrectionCb->isChecked());
    if (saveImg.isNull()) {
        QMessageBox::warning(this, "No Mask Found", "There is no phase mask or correction loaded to save.");
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultFileName = QDir(defaultPath).filePath("PhaseMask_" + timestamp + ".bmp");

    QString fileName = QFileDialog::getSaveFileName(this, "Save Phase Mask", defaultFileName, "Images (*.png *.bmp *.jpg)");
    if (!fileName.isEmpty()) {
        if (saveImg.save(fileName)) {
            statusBar()->showMessage("Phase mask saved to: " + fileName, 5000);
        } else {
            QMessageBox::critical(this, "Save Error", "Failed to save the image. Please check folder permissions.");
        }
    }
}

void MainWindow::updateCameraFeed(const QImage &img) {
    if (img.isNull()) {
        return;
    }

    lastCameraFrame = img.copy();
    lastRenderedCameraFrame = buildCameraDisplayImage(lastCameraFrame, false);
    lastOverlayRenderedCameraFrame = buildCameraDisplayImage(lastCameraFrame, true);
    lastZoomedRenderedCameraFrame = applyCameraZoomToDisplayImage(lastOverlayRenderedCameraFrame);
    updateCameraFeedLabel(lastZoomedRenderedCameraFrame);

    if (targetGridWidget) {
        const QSize liveGridSize = lastRenderedCameraFrame.size();
        if (liveGridSize.isValid() && targetGridWidget->gridResolution() != liveGridSize) {
            targetGridWidget->setGridResolution(liveGridSize.width(), liveGridSize.height());
            targetGridWidget->centerView();
        }
    }

    if (targetModeTabs && targetGridWidget && targetModeTabs->currentIndex() == kCameraTabIndex) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        constexpr qint64 kTargetCameraPreviewIntervalMs = 100;
        if ((nowMs - lastTargetCameraTabUpdateMs) >= kTargetCameraPreviewIntervalMs) {
            lastTargetCameraTabUpdateMs = nowMs;
            targetGridWidget->setBackgroundImage(lastRenderedCameraFrame);
            targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::LiveCamera);
        }
    }

    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }

    publishXpSenderOverlay();
}

void MainWindow::loadTargetImage() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Target Image", "", "Images (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)");
    if (fileName.isEmpty()) {
        return;
    }

    QImage src(fileName);
    if (src.isNull()) {
        QMessageBox::warning(this, "Image Load Error", "Failed to load selected image.");
        return;
    }

    loadedTargetImageOriginal = src;
    loadedTargetImageGray = src.convertToFormat(QImage::Format_Grayscale8).scaled(
        camWidth, camHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    clearTargetImageBtn->setEnabled(true);
    targetImageInfoLabel->setText(QString("%1 -> %2 x %3 (8-bit grayscale)")
        .arg(QFileInfo(fileName).fileName())
        .arg(camWidth)
        .arg(camHeight));

    if (targetModeTabs->currentIndex() == kImageTabIndex) {
        targetGridWidget->setBackgroundImage(loadedTargetImageGray);
        targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::StaticImage);
    }

    statusBar()->showMessage("Target image loaded and converted to camera-sized grayscale.", 4000);
}

void MainWindow::clearTargetImage() {
    loadedTargetImageOriginal = QImage();
    loadedTargetImageGray = QImage();

    clearTargetImageBtn->setEnabled(false);
    targetImageInfoLabel->setText("No image loaded. Will resize to camera resolution and convert to grayscale.");

    targetGridWidget->clearBackgroundImage();
    if (targetModeTabs->currentIndex() == kImageTabIndex) {
        targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::StaticImage);
    }

    statusBar()->showMessage("Target image cleared.", 3000);
}

void MainWindow::onTabChanged(int index) {
    if (index == kImageTabIndex) {
        targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::StaticImage);
        if (!loadedTargetImageGray.isNull()) {
            targetGridWidget->setBackgroundImage(loadedTargetImageGray);
        } else {
            targetGridWidget->clearBackgroundImage();
        }
    } else if (index == kCameraTabIndex) {
        targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::LiveCamera);
        lastTargetCameraTabUpdateMs = 0;
        if (!lastRenderedCameraFrame.isNull()) {
            targetGridWidget->setBackgroundImage(lastRenderedCameraFrame);
        } else {
            targetGridWidget->clearBackgroundImage();
        }
    } else {
        targetGridWidget->setDisplayMode(TargetGridWidget::DisplayMode::GridOnly);
        targetGridWidget->clearBackgroundImage();
    }
}

void MainWindow::onCameraZoomRoiChanged(const QRectF &roiNormalized, bool enabled) {
    applyCameraZoomState(roiNormalized, enabled, true);
}

void MainWindow::applyCameraZoomState(const QRectF &roiNormalized, bool enabled, bool recordHistory) {
    const QRectF fullView(0.0, 0.0, 1.0, 1.0);
    QRectF nextRoi = fullView;
    bool nextEnabled = enabled;

    if (enabled) {
        const QSize basisSize = !lastRenderedCameraFrame.isNull()
                                    ? lastRenderedCameraFrame.size()
                                    : QSize(qMax(1, camWidth), qMax(1, camHeight));
        nextRoi = normalizedCameraZoomRoiForSize(basisSize, roiNormalized);
        const bool fullWidth = qAbs(nextRoi.width() - 1.0) < 1e-6;
        const bool fullHeight = qAbs(nextRoi.height() - 1.0) < 1e-6;
        const bool fullX = qAbs(nextRoi.x()) < 1e-6;
        const bool fullY = qAbs(nextRoi.y()) < 1e-6;
        if (fullWidth && fullHeight && fullX && fullY) {
            nextEnabled = false;
            nextRoi = fullView;
        }
    }

    const QRectF currentRoi = cameraZoomEnabled ? cameraZoomRoiNormalized : fullView;
    if (recordHistory && currentRoi != nextRoi) {
        cameraZoomHistory.append(currentRoi);
    }

    cameraZoomEnabled = nextEnabled;
    cameraZoomRoiNormalized = nextEnabled ? nextRoi : fullView;

    if (camManager) {
        camManager->setZoomRegionNormalized(cameraZoomRoiNormalized, cameraZoomEnabled);
    }

    if (!lastOverlayRenderedCameraFrame.isNull()) {
        lastZoomedRenderedCameraFrame = applyCameraZoomToDisplayImage(lastOverlayRenderedCameraFrame);
        updateCameraFeedLabel(lastZoomedRenderedCameraFrame);
    }

    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
}

void MainWindow::resetCameraZoom() {
    cameraZoomHistory.clear();
    applyCameraZoomState(QRectF(0.0, 0.0, 1.0, 1.0), false, false);
}

void MainWindow::undoCameraZoomStep() {
    if (cameraZoomHistory.isEmpty()) {
        resetCameraZoom();
        return;
    }

    const QRectF previousRoi = cameraZoomHistory.takeLast();
    const QRectF fullView(0.0, 0.0, 1.0, 1.0);
    applyCameraZoomState(previousRoi, previousRoi != fullView, false);
}

void MainWindow::onRecordingTimeUpdated(const QString &timeString) {
    if (recordVideoBtn->isChecked()) {
        if (recordTimeHideTimer) {
            recordTimeHideTimer->stop();
        }
        recordTimeLabel->setVisible(true);
    }
    recordTimeLabel->setText(timeString);
    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
}

void MainWindow::onFPSUpdated(const QString &fpsString) {
    fpsLabel->setText(fpsString);
    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
}

void MainWindow::onCameraPreviewMonitorChanged(int index) {
    if (!cameraPreviewMonitorCombo || index < 0) {
        updateCameraPreviewButtonState();
        return;
    }

    bool ok = false;
    const int monitorNumber = cameraPreviewMonitorCombo->itemData(index).toInt(&ok);
    if (!ok || monitorNumber < 1) {
        updateCameraPreviewButtonState();
        return;
    }

    cameraPreviewMonitorNumber = monitorNumber;
    persistSelectedCameraPreviewMonitor();
    updateCameraPreviewButtonState();
    statusBar()->showMessage(QString("Camera preview monitor set to Monitor %1").arg(cameraPreviewMonitorNumber), 4000);

    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
}

void MainWindow::onCameraPreviewToggled(bool checked) {
    updateCameraPreviewButtonText();

    if (!checked) {
        clearCameraPreviewOutput();
        statusBar()->showMessage("Camera preview monitor output hidden.", 3000);
        return;
    }

    if (!isSelectedCameraPreviewMonitorAvailable()) {
        QSignalBlocker blocker(cameraPreviewToggleBtn);
        cameraPreviewToggleBtn->setChecked(false);
        updateCameraPreviewButtonText();
        statusBar()->showMessage("Choose a connected monitor for camera preview.", 4000);
        return;
    }

    updateExternalCameraPreview();
    statusBar()->showMessage(QString("Camera preview mirrored to Monitor %1").arg(cameraPreviewMonitorNumber), 4000);
}

void MainWindow::onScreenTopologyChanged() {
    const bool wasActive = cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked();
    const int missingMonitorNumber = cameraPreviewMonitorNumber;

    refreshCameraPreviewMonitorOptions();

    if (wasActive && !isSelectedCameraPreviewMonitorAvailable()) {
        if (cameraPreviewToggleBtn) {
            QSignalBlocker blocker(cameraPreviewToggleBtn);
            cameraPreviewToggleBtn->setChecked(false);
        }
        updateCameraPreviewButtonText();
        clearCameraPreviewOutput();
        statusBar()->showMessage(
            QString("Camera preview monitor %1 is not connected. External preview stopped.")
                .arg(missingMonitorNumber),
            5000);
        return;
    }

    if (wasActive) {
        updateExternalCameraPreview();
    }
}

void MainWindow::onGridPointAdded(int pointId, QPointF pixelCoords) {
    gridPointData[pointId] = pixelCoords;

    trapTableSyncInProgress = true;
    addPointRowToTables(pointId, pixelCoords);
    trapTableSyncInProgress = false;

    if (!suppressGridStatusMessages) {
        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        statusBar()->showMessage(QString("Point #%1 added at (%2, %3)").arg(pointId).arg((int)pixelCoords.x()).arg((int)pixelCoords.y()), 3000);
    }

    scheduleGsAutoRun();
    if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
        updateCameraFeed(lastCameraFrame);
    }
}

void MainWindow::onGridPointMoved(int pointId, QPointF newPixelCoords) {
    if (gridPointData.contains(pointId)) {
        gridPointData[pointId] = newPixelCoords;

        trapTableSyncInProgress = true;
        updatePointRowInTables(pointId, newPixelCoords);
        trapTableSyncInProgress = false;

        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        statusBar()->showMessage(QString("Point #%1 moved to (%2, %3)").arg(pointId).arg((int)newPixelCoords.x()).arg((int)newPixelCoords.y()), 2000);
        if (pointId == selectedPointId && gridHoverLabel) {
            gridHoverLabel->setText(QString("Grid: %1, %2").arg((int)newPixelCoords.x()).arg((int)newPixelCoords.y()));
            restoreSelectedPointSelection();
        }
        scheduleGsAutoRun();
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }
    }
}

void MainWindow::onTrapTableItemChanged(QTableWidgetItem *item) {
    if (!item || trapTableSyncInProgress) {
        return;
    }

    QTableWidget *sourceTable = item->tableWidget();
    if (!sourceTable) {
        return;
    }

    const int row = item->row();
    const int column = item->column();
    if (row < 0 || row >= sourceTable->rowCount()) {
        return;
    }
    if (column != 1 && column != 2) {
        return;
    }

    QTableWidgetItem *idItem = sourceTable->item(row, 0);
    if (!idItem) {
        return;
    }

    bool idOk = false;
    const int pointId = idItem->text().toInt(&idOk);
    if (!idOk || !gridPointData.contains(pointId)) {
        return;
    }

    const QPointF previousCoords = gridPointData.value(pointId);
    bool valueOk = false;
    const int typedValue = item->text().trimmed().toInt(&valueOk);
    if (!valueOk) {
        trapTableSyncInProgress = true;
        item->setText(QString::number(column == 1 ? static_cast<int>(previousCoords.x())
                                                  : static_cast<int>(previousCoords.y())));
        trapTableSyncInProgress = false;
        return;
    }

    const double halfWidth = camWidth / 2.0;
    const double halfHeight = camHeight / 2.0;

    QPointF updatedCoords = previousCoords;
    if (column == 1) {
        updatedCoords.setX(typedValue);
    } else {
        updatedCoords.setY(typedValue);
    }
    updatedCoords.setX(qBound(-halfWidth, updatedCoords.x(), halfWidth));
    updatedCoords.setY(qBound(-halfHeight, updatedCoords.y(), halfHeight));

    if (!targetGridWidget || !targetGridWidget->setPointCoordinates(pointId, updatedCoords)) {
        trapTableSyncInProgress = true;
        item->setText(QString::number(column == 1 ? static_cast<int>(previousCoords.x())
                                                  : static_cast<int>(previousCoords.y())));
        trapTableSyncInProgress = false;
        return;
    }

    gridPointData[pointId] = updatedCoords;

    trapTableSyncInProgress = true;
    updatePointRowInTables(pointId, updatedCoords);
    trapTableSyncInProgress = false;

    if (updatedCoords != previousCoords) {
        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        statusBar()->showMessage(QString("Point #%1 moved to (%2, %3)")
                                     .arg(pointId)
                                     .arg(static_cast<int>(updatedCoords.x()))
                                     .arg(static_cast<int>(updatedCoords.y())),
                                 2000);
        scheduleGsAutoRun();
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }
    }
}

void MainWindow::onGridPointRemoved(int pointId) {
    if (gridPointData.contains(pointId)) {
        gridPointData.remove(pointId);

        trapTableSyncInProgress = true;
        removePointRowFromTables(pointId);
        trapTableSyncInProgress = false;

        lastGeneratedPatternSummary.clear();
        lastGeneratedPatternDetails.clear();
        statusBar()->showMessage(QString("Point #%1 removed").arg(pointId), 2000);
        scheduleGsAutoRun();
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }

        if (pointId == selectedPointId) {
            selectedPointId = -1;
            updateGridHoverReadout(QPoint(), false);
        }
    }
}

void MainWindow::onGridPointSelected(int pointId) {
    selectedPointId = pointId;
    
    selectPointRowInTables(pointId);
    
    if (gridPointData.contains(pointId) && gridHoverLabel) {
        const QPointF p = gridPointData.value(pointId);
        gridHoverLabel->setText(QString("Grid: %1, %2").arg((int)p.x()).arg((int)p.y()));
    }

    statusBar()->showMessage(QString("Point #%1 selected (use arrow keys to move, Delete to remove)").arg(pointId), 3000);
    if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
        updateCameraFeed(lastCameraFrame);
    }
}

void MainWindow::onGridPointDeselected() {
    selectedPointId = -1;
    if (!targetGridWidget || !targetGridWidget->viewport()) {
        updateGridHoverReadout(QPoint(), false);
        return;
    }

    const QPoint viewportPos = targetGridWidget->viewport()->mapFromGlobal(QCursor::pos());
    const bool inside = targetGridWidget->viewport()->rect().contains(viewportPos);
    updateGridHoverReadout(viewportPos, inside);
}

void MainWindow::onPatternGenerated(const QVector<QPointF> &points, const QString &summary, const QString &details) {
    lastGeneratedPatternSummary = summary;
    lastGeneratedPatternDetails = details;
    replaceGridWithPoints(points);
    statusBar()->showMessage(summary, 4000);
}

void MainWindow::replaceGridWithPoints(const QVector<QPointF> &points) {
    suppressGridStatusMessages = true;
    selectedPointId = -1;
    updateGridHoverReadout(QPoint(), false);

    gridPointData.clear();
    clearPointTables();
    targetGridWidget->clearAllPoints();

    for (const QPointF &point : points) {
        targetGridWidget->addPoint(point);
    }

    suppressGridStatusMessages = false;
    applyTrapHighlightForCurrentFrame(points);
    scheduleGsAutoRun();
    if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
        updateCameraFeed(lastCameraFrame);
    }
}

void MainWindow::onAnimationPresetChanged(int index) {
    if (animationParamsStack) {
        const int pageCount = animationParamsStack->count();
        if (pageCount <= 0) {
            return;
        }
        const int safeIndex = (index >= 0 && index < pageCount) ? index : 0;
        animationParamsStack->setCurrentIndex(safeIndex);
        animationParamsStack->updateGeometry();
    }

    if (animationContentWidget) {
        animationContentWidget->adjustSize();
    }

    if (animationScrollArea && animationParamsStack) {
        animationScrollArea->ensureWidgetVisible(animationParamsStack, 0, 24);
    }
}

void MainWindow::onGenerateAnimationSequenceClicked() {
    activeSequenceSource = SequenceSource::AnimationPreset;
    selectedSequenceTrapIndexOneBased = -1;
    if (pythonTrapSelectorCombo) {
        const int noneIndex = pythonTrapSelectorCombo->findData(-1);
        if (noneIndex >= 0) {
            pythonTrapSelectorCombo->setCurrentIndex(noneIndex);
        }
    }
    if (!buildAnimationSequenceFromUi(true)) {
        return;
    }

    animationIterationsSnapshot = iterationsSpin ? iterationsSpin->value() : 20;
    animationCurrentFrameIndex = 0;
    animationComputeLimitedWarned = false;

    if (!animationFramePoints.isEmpty()) {
        updateAnimationPreviewLabels(animationFramePoints.first());
        replaceGridWithPoints(animationFramePoints.first());
    }

    animationPrecomputeReady = false;
    animationPrecomputedMasks.clear();

    if (animationRealtimeCheck && !animationRealtimeCheck->isChecked()) {
        if (!precomputeAnimationMasks(true)) {
            return;
        }
    }

    animationSequenceReady = true;
    if (activeSequenceSource == SequenceSource::AnimationPreset) {
        if (animationParticlesSpin) {
            animationParticlesSpin->setEnabled(false);
        }
    } else if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(false);
    }
    if (pythonStatusLabel) {
        pythonStatusLabel->setText("Animation preset sequence is active.");
    }
    updateAnimationControlsEnabledState();

    statusBar()->showMessage(
        QString("Animation sequence generated (%1 frames, %2 particles, iterations snapshot: %3).")
            .arg(animationFramePoints.size())
            .arg(animationParticlesSpin ? animationParticlesSpin->value() : 0)
            .arg(animationIterationsSnapshot),
        5000);
}

void MainWindow::onGeneratePythonSequenceClicked() {
    activeSequenceSource = SequenceSource::PythonScript;
    if (!buildPythonSequenceFromUi(true)) {
        return;
    }

    animationIterationsSnapshot = iterationsSpin ? iterationsSpin->value() : 20;
    animationCurrentFrameIndex = 0;
    animationComputeLimitedWarned = false;

    if (!animationFramePoints.isEmpty()) {
        updateAnimationPreviewLabels(animationFramePoints.first());
        replaceGridWithPoints(animationFramePoints.first());
    }

    animationPrecomputeReady = false;
    animationPrecomputedMasks.clear();

    if (!currentSequenceRealtime()) {
        if (!precomputeAnimationMasks(true)) {
            return;
        }
    }

    animationSequenceReady = true;
    updateAnimationControlsEnabledState();

    statusBar()->showMessage(
        QString("Python sequence generated (%1 frames).").arg(animationFramePoints.size()),
        5000);
}

void MainWindow::onPlayAnimationClicked() {
    if (!animationSequenceReady || animationFramePoints.isEmpty()) {
        const bool shouldUsePython = (activeSequenceSource == SequenceSource::PythonScript) ||
                                     (targetModeTabs && targetModeTabs->currentIndex() == kPythonTabIndex);
        if (shouldUsePython) {
            activeSequenceSource = SequenceSource::PythonScript;
        } else {
            activeSequenceSource = SequenceSource::AnimationPreset;
        }

        const bool built = shouldUsePython
            ? buildPythonSequenceFromUi(true)
            : buildAnimationSequenceFromUi(true);
        if (!built) {
            return;
        }
        animationIterationsSnapshot = iterationsSpin ? iterationsSpin->value() : 20;
        animationSequenceReady = true;
    }

    const bool realtime = currentSequenceRealtime();
    if (!realtime && !animationPrecomputeReady) {
        if (!precomputeAnimationMasks(true)) {
            return;
        }
    }

    animationCurrentFrameIndex = 0;
    animationComputeLimitedWarned = false;
    animationRealtimeRunning = realtime;
    animationPlaybackRunning = !realtime;
    if (activeSequenceSource == SequenceSource::AnimationPreset) {
        if (animationParticlesSpin) {
            animationParticlesSpin->setEnabled(false);
        }
    } else if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(false);
    }

    if (animationTimer) {
        animationTimer->setInterval(animationTimerIntervalMs());
        animationTimer->start();
    }
    updateAnimationControlsEnabledState();

    const QString sourceLabel = activeSequenceSource == SequenceSource::PythonScript ? "Python" : "Animation";
    QString playbackMsg = realtime
        ? QString("%1 realtime playback started.").arg(sourceLabel)
        : QString("%1 precomputed playback started.").arg(sourceLabel);
    if (!autoSendSlmEnabled) {
        playbackMsg += " Auto-send SLM is OFF, so frames update preview only.";
    }
    statusBar()->showMessage(playbackMsg, 4000);
}

void MainWindow::onPlayPythonSequenceClicked() {
    activeSequenceSource = SequenceSource::PythonScript;

    if (!animationSequenceReady || animationFramePoints.isEmpty()) {
        if (!buildPythonSequenceFromUi(true)) {
            return;
        }
        animationIterationsSnapshot = iterationsSpin ? iterationsSpin->value() : 20;
        animationSequenceReady = true;
    }

    if (animationFramePoints.isEmpty()) {
        QMessageBox::warning(this, "Python Script", "No frame points available to send.");
        return;
    }

    if (isStaticSequence(animationFramePoints)) {
        if (animationTimer) {
            animationTimer->stop();
        }
        animationRealtimeRunning = false;
        animationPlaybackRunning = false;
        animationCurrentFrameIndex = 0;
        animationComputeLimitedWarned = false;

        const QVector<QPointF> &points = animationFramePoints.first();
        updateAnimationPreviewLabels(points);
        replaceGridWithPoints(points);

        QImage frameMask;
        QString error;
        if (!runGsForTargetPoints(points, animationIterationsSnapshot, frameMask, &error)) {
            QMessageBox::warning(this, "Python GS", error.isEmpty() ? "Failed to generate GS frame." : error);
            return;
        }

        currentMask = frameMask;
        updatePhasePreview();
        sendToSLM();
        updateAnimationControlsEnabledState();
        statusBar()->showMessage("Static Python pattern sent to SLM.", 4000);
        return;
    }

    onPlayAnimationClicked();
}

void MainWindow::onStopAnimationClicked() {
    if (animationTimer) {
        animationTimer->stop();
    }
    animationRealtimeRunning = false;
    animationPlaybackRunning = false;
    if (animationParticlesSpin) {
        animationParticlesSpin->setEnabled(true);
    }
    if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(true);
    }
    clearAnimationSequenceState(false);
    updateAnimationControlsEnabledState();
    statusBar()->showMessage("Animation stopped.", 3000);
}

void MainWindow::onStopPythonSequenceClicked() {
    onStopAnimationClicked();
}

void MainWindow::onResetAnimationClicked() {
    if (animationTimer) {
        animationTimer->stop();
    }
    animationRealtimeRunning = false;
    animationPlaybackRunning = false;
    if (animationParticlesSpin) {
        animationParticlesSpin->setEnabled(true);
    }
    if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(true);
    }
    clearAnimationSequenceState(true);
    updateAnimationControlsEnabledState();
    statusBar()->showMessage("Animation reset.", 3000);
}

void MainWindow::onResetPythonSequenceClicked() {
    onResetAnimationClicked();
}

void MainWindow::onPythonTrapSelectionChanged(int index) {
    Q_UNUSED(index);

    if (!pythonTrapSelectorCombo) {
        selectedSequenceTrapIndexOneBased = -1;
    } else {
        selectedSequenceTrapIndexOneBased = pythonTrapSelectorCombo->currentData().toInt();
    }

    if (animationSequenceReady && !animationFramePoints.isEmpty()) {
        const int safeIndex = qBound(0, animationCurrentFrameIndex, animationFramePoints.size() - 1);
        applyTrapHighlightForCurrentFrame(animationFramePoints.at(safeIndex));
        updateAnimationPreviewLabels(animationFramePoints.at(safeIndex));
        if (overlayTargetCb && overlayTargetCb->isChecked() && !lastCameraFrame.isNull()) {
            updateCameraFeed(lastCameraFrame);
        }
    }
}

void MainWindow::onSavePythonScriptClicked() {
    if (!pythonCodeEditor) {
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Python Script"),
        QString(),
        tr("Python Files (*.py);;All Files (*)")
    );
    if (filePath.isEmpty()) {
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("Could not open file for writing:\n%1").arg(filePath));
        return;
    }
    QTextStream out(&file);
    out << pythonCodeEditor->toPlainText();
    file.close();
    statusBar()->showMessage(tr("Script saved to: %1").arg(filePath), 5000);
}

void MainWindow::onLoadPythonScriptClicked() {
    if (!pythonCodeEditor) {
        return;
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Load Python Script"),
        QString(),
        tr("Python Files (*.py);;All Files (*)")
    );
    if (filePath.isEmpty()) {
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Load Failed"),
                             tr("Could not open file for reading:\n%1").arg(filePath));
        return;
    }
    QTextStream in(&file);
    pythonCodeEditor->setPlainText(in.readAll());
    file.close();
    statusBar()->showMessage(tr("Script loaded from: %1").arg(filePath), 5000);
}

void MainWindow::onAnimationTimerTimeout() {
    if (!animationSequenceReady || animationFramePoints.isEmpty()) {
        if (animationTimer) {
            animationTimer->stop();
        }
        animationRealtimeRunning = false;
        animationPlaybackRunning = false;
        updateAnimationControlsEnabledState();
        return;
    }

    if (animationCurrentFrameIndex >= animationFramePoints.size()) {
        if (animationTimer) {
            animationTimer->stop();
        }
        animationRealtimeRunning = false;
        animationPlaybackRunning = false;
        updateAnimationControlsEnabledState();
        statusBar()->showMessage("Animation playback finished.", 3000);
        return;
    }

    const QVector<QPointF> &points = animationFramePoints.at(animationCurrentFrameIndex);
    updateAnimationPreviewLabels(points);
    replaceGridWithPoints(points);

    if (animationRealtimeRunning) {
        QElapsedTimer frameTimer;
        frameTimer.start();

        QImage frameMask;
        QString error;
        if (!runGsForTargetPoints(points, animationIterationsSnapshot, frameMask, &error)) {
            if (animationTimer) {
                animationTimer->stop();
            }
            animationRealtimeRunning = false;
            animationPlaybackRunning = false;
            updateAnimationControlsEnabledState();
            const QString title = activeSequenceSource == SequenceSource::PythonScript
                ? QString("Python GS")
                : QString("Animation GS");
            QMessageBox::warning(this, title, error.isEmpty() ? "Failed to generate GS frame." : error);
            return;
        }

        currentMask = frameMask;
        updatePhasePreview();
        if (autoSendSlmEnabled) {
            sendToSLM();
        }

        const int elapsed = static_cast<int>(frameTimer.elapsed());
        const int budget = animationTimerIntervalMs();
        if (!animationComputeLimitedWarned && elapsed > budget) {
            animationComputeLimitedWarned = true;
            statusBar()->showMessage(
                QString("Realtime animation is compute-limited (%1 ms frame time > %2 ms interval).")
                    .arg(elapsed)
                    .arg(budget),
                6000);
        }
    } else if (animationPlaybackRunning) {
        if (animationCurrentFrameIndex < animationPrecomputedMasks.size()) {
            currentMask = animationPrecomputedMasks.at(animationCurrentFrameIndex);
            updatePhasePreview();
            if (autoSendSlmEnabled) {
                sendToSLM();
            }
        }
    }

    ++animationCurrentFrameIndex;
}

void MainWindow::populatePythonTrapSelector() {
    if (!pythonTrapSelectorCombo) {
        return;
    }

    int maxPoints = 0;
    for (const QVector<QPointF> &frame : animationFramePoints) {
        maxPoints = qMax(maxPoints, frame.size());
    }

    pythonTrapSelectorCombo->blockSignals(true);
    pythonTrapSelectorCombo->clear();
    pythonTrapSelectorCombo->addItem("None", -1);
    for (int i = 1; i <= maxPoints; ++i) {
        pythonTrapSelectorCombo->addItem(QString("Trap %1").arg(i), i);
    }

    const int requestedId = selectedSequenceTrapIndexOneBased;
    int setIndex = 0;
    if (requestedId > 0) {
        const int found = pythonTrapSelectorCombo->findData(requestedId);
        if (found >= 0) {
            setIndex = found;
        } else {
            selectedSequenceTrapIndexOneBased = -1;
        }
    }
    pythonTrapSelectorCombo->setCurrentIndex(setIndex);
    pythonTrapSelectorCombo->blockSignals(false);
}

void MainWindow::applyTrapHighlightForCurrentFrame(const QVector<QPointF> &points) {
    if (selectedSequenceTrapIndexOneBased > 0 &&
        selectedSequenceTrapIndexOneBased <= points.size()) {
        selectedPointId = selectedSequenceTrapIndexOneBased;
        selectPointRowInTables(selectedPointId);
        return;
    } else {
        selectedPointId = -1;
        clearPointTableSelections();
    }
}

bool MainWindow::currentSequenceRealtime() const {
    if (activeSequenceSource == SequenceSource::PythonScript) {
        return pythonRealtimeCheck && pythonRealtimeCheck->isChecked();
    }
    return animationRealtimeCheck && animationRealtimeCheck->isChecked();
}

int MainWindow::currentSequenceFps() const {
    if (activeSequenceSource == SequenceSource::PythonScript) {
        return pythonFpsSpin ? qMax(1, pythonFpsSpin->value()) : 30;
    }
    return animationFpsSpin ? qMax(1, animationFpsSpin->value()) : 30;
}

int MainWindow::animationTimerIntervalMs() const {
    const int fps = currentSequenceFps();
    return qMax(1, static_cast<int>(1000.0 / static_cast<double>(fps)));
}

void MainWindow::updateAnimationControlsEnabledState() {
    const bool running = animationRealtimeRunning || animationPlaybackRunning;
    if (animationGenerateBtn) {
        animationGenerateBtn->setEnabled(!running);
    }
    if (animationPlaySendBtn) {
        animationPlaySendBtn->setEnabled(!running && animationSequenceReady);
    }
    if (animationStopBtn) {
        animationStopBtn->setEnabled(running);
    }
    if (animationPresetCombo) {
        animationPresetCombo->setEnabled(!running);
    }
    if (animationFpsSpin) {
        animationFpsSpin->setEnabled(!running);
    }
    if (animationFrameCountSpin) {
        animationFrameCountSpin->setEnabled(!running);
    }
    if (animationRealtimeCheck) {
        animationRealtimeCheck->setEnabled(!running);
    }
    if (pythonGenerateBtn) {
        pythonGenerateBtn->setEnabled(!running);
    }
    if (pythonPlaySendBtn) {
        pythonPlaySendBtn->setEnabled(!running && animationSequenceReady);
    }
    if (pythonStopBtn) {
        pythonStopBtn->setEnabled(running);
    }
    if (pythonResetBtn) {
        pythonResetBtn->setEnabled(!running || animationSequenceReady);
    }
    if (pythonFpsSpin) {
        pythonFpsSpin->setEnabled(!running);
    }
    if (pythonFrameCountSpin) {
        pythonFrameCountSpin->setEnabled(!running);
    }
    if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(!running);
    }
    if (pythonRealtimeCheck) {
        pythonRealtimeCheck->setEnabled(!running);
    }
    if (pythonTrapSelectorCombo) {
        pythonTrapSelectorCombo->setEnabled(!running && activeSequenceSource == SequenceSource::PythonScript);
    }
    if (pythonCodeEditor) {
        pythonCodeEditor->setReadOnly(running);
    }
}

QImage MainWindow::buildAnimationPreviewImage(const QVector<QPointF> &points, bool cameraStyle, int highlightIndexOneBased) const {
    QImage image(camWidth, camHeight, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor color = cameraStyle ? QColor(80, 190, 255) : QColor(255, 255, 255);
    painter.setPen(QPen(color, cameraStyle ? 2.0 : 1.0));
    painter.setBrush(QBrush(cameraStyle ? QColor(80, 190, 255, 110) : QColor(255, 255, 255, 200)));

    const double halfW = static_cast<double>(camWidth) / 2.0;
    const double halfH = static_cast<double>(camHeight) / 2.0;
    const int radius = cameraStyle ? qMax(3, qMin(camWidth, camHeight) / 90) : qMax(2, qMin(camWidth, camHeight) / 120);

    for (int i = 0; i < points.size(); ++i) {
        const QPointF &p = points.at(i);
        const QPointF imagePoint(halfW + p.x(), halfH - p.y());
        const int px = qBound(0, static_cast<int>(qRound(imagePoint.x())), camWidth - 1);
        const int py = qBound(0, static_cast<int>(qRound(imagePoint.y())), camHeight - 1);

        if (highlightIndexOneBased > 0 && (i + 1) == highlightIndexOneBased) {
            painter.setPen(QPen(QColor(120, 255, 120, 220), cameraStyle ? 2.5 : 2.0));
            painter.setBrush(QBrush(QColor(120, 255, 120, 90)));
            painter.drawEllipse(QPoint(px, py), radius + 3, radius + 3);
            painter.setPen(QPen(color, cameraStyle ? 2.0 : 1.0));
            painter.setBrush(QBrush(cameraStyle ? QColor(80, 190, 255, 110) : QColor(255, 255, 255, 200)));
        }
        painter.drawEllipse(QPoint(px, py), radius, radius);
    }

    return image;
}

void MainWindow::updateAnimationPreviewLabels(const QVector<QPointF> &points) {
    const int highlightIndex = selectedSequenceTrapIndexOneBased;
    if (animationIntensityPreviewLabel) {
        const QImage intensityImg = buildAnimationPreviewImage(points, false, highlightIndex);
        animationIntensityPreviewLabel->setPixmap(QPixmap::fromImage(intensityImg).scaled(
            animationIntensityPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (animationCameraPreviewLabel) {
        const QImage cameraImg = buildAnimationPreviewImage(points, true, highlightIndex);
        animationCameraPreviewLabel->setPixmap(QPixmap::fromImage(cameraImg).scaled(
            animationCameraPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (pythonIntensityPreviewLabel) {
        const QImage intensityImg = buildAnimationPreviewImage(points, false, highlightIndex);
        pythonIntensityPreviewLabel->setPixmap(QPixmap::fromImage(intensityImg).scaled(
            pythonIntensityPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (pythonCameraPreviewLabel) {
        const QImage cameraImg = buildAnimationPreviewImage(points, true, highlightIndex);
        pythonCameraPreviewLabel->setPixmap(QPixmap::fromImage(cameraImg).scaled(
            pythonCameraPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

bool MainWindow::buildAnimationSequenceFromUi(bool showWarnings) {
    if (!animationPresetCombo || !animationFrameCountSpin || !animationParticlesSpin) {
        return false;
    }

    PatternGenerator::AnimationRequest request;
    request.fps = animationFpsSpin ? animationFpsSpin->value() : 30;
    request.frameCount = animationFrameCountSpin->value();
    request.particleCount = animationParticlesSpin->value();
    request.realtime = animationRealtimeCheck && animationRealtimeCheck->isChecked();

    if (animationPresetCombo->currentIndex() == 0) {
        request.preset = PatternGenerator::AnimationPreset::Circle;
        request.circleRadiusFrom = animCircleRadiusFromSpin ? animCircleRadiusFromSpin->value() : 80.0;
        request.circleRadiusTo = animCircleRadiusToSpin ? animCircleRadiusToSpin->value() : 180.0;
    } else {
        request.preset = PatternGenerator::AnimationPreset::Triangle;
        request.triangleScaleFrom = animTriangleScaleFromSpin ? animTriangleScaleFromSpin->value() : 80.0;
        request.triangleScaleTo = animTriangleScaleToSpin ? animTriangleScaleToSpin->value() : 180.0;
        request.triangleRotationFromDeg = animTriangleRotationFromSpin ? animTriangleRotationFromSpin->value() : 0.0;
        request.triangleRotationToDeg = animTriangleRotationToSpin ? animTriangleRotationToSpin->value() : 360.0;
    }

    QVector<QVector<QPointF>> frames = PatternGenerator::generateAnimationFrames(request);
    if (frames.isEmpty()) {
        if (showWarnings) {
            QMessageBox::warning(this, "Animation", "Failed to generate animation frame points.");
        }
        return false;
    }

    animationFramePoints = frames;
    animationCurrentFrameIndex = 0;
    animationSequenceReady = true;
    animationPrecomputeReady = false;
    animationPrecomputedMasks.clear();
    activeSequenceSource = SequenceSource::AnimationPreset;
    selectedSequenceTrapIndexOneBased = -1;
    return true;
}

bool MainWindow::buildPythonSequenceFromUi(bool showWarnings) {
    if (!pythonCodeEditor || !pythonFrameCountSpin || !pythonMaxPointsSpin) {
        return false;
    }

    if (!pythonScriptEngine || !pythonScriptEngine->isReady()) {
        const QString msg = (pythonScriptEngine && !pythonScriptEngine->initError().isEmpty())
            ? pythonScriptEngine->initError()
            : QString("Embedded Python runtime is unavailable.");
        if (pythonStatusLabel) {
            pythonStatusLabel->setText(msg);
        }
        if (showWarnings) {
            QMessageBox::warning(this, "Python Script", msg);
        }
        return false;
    }

    PythonTrapScriptResult result = pythonScriptEngine->runScript(
        pythonCodeEditor->toPlainText(),
        pythonFrameCountSpin->value(),
        camWidth,
        camHeight,
        pythonMaxPointsSpin->value());

    if (!result.success) {
        if (pythonStatusLabel) {
            pythonStatusLabel->setText(result.errorMessage);
        }
        if (showWarnings) {
            QMessageBox::warning(this, "Python Script", result.errorMessage);
        }
        return false;
    }

    animationFramePoints = result.frames;
    animationCurrentFrameIndex = 0;
    animationSequenceReady = true;
    animationPrecomputeReady = false;
    animationPrecomputedMasks.clear();
    activeSequenceSource = SequenceSource::PythonScript;
    populatePythonTrapSelector();

    // Apply script-declared FPS back to the UI spinbox
    if (result.overrideFps > 0 && pythonFpsSpin) {
        pythonFpsSpin->setValue(result.overrideFps);
    }

    if (pythonStatusLabel) {
        QString status = QString("Generated %1 frame(s) from Python script.").arg(animationFramePoints.size());
        if (result.overrideFps > 0) {
            status += QString(" (FPS set to %1 by script.)").arg(result.overrideFps);
        }
        if (!result.warningMessage.isEmpty()) {
            status += " " + result.warningMessage;
        }
        pythonStatusLabel->setText(status);
    }

    return true;
}

bool MainWindow::precomputeAnimationMasks(bool showWarnings) {
    if (!animationSequenceReady || animationFramePoints.isEmpty()) {
        if (showWarnings) {
            QMessageBox::warning(this, "Animation", "Generate an animation sequence first.");
        }
        return false;
    }

    animationPrecomputedMasks.clear();
    animationPrecomputedMasks.reserve(animationFramePoints.size());

    const int iterSnapshot = animationIterationsSnapshot > 0
        ? animationIterationsSnapshot
        : (iterationsSpin ? iterationsSpin->value() : 20);
    animationIterationsSnapshot = iterSnapshot;

    if (activeSequenceSource == SequenceSource::AnimationPreset) {
        if (animationParticlesSpin) {
            animationParticlesSpin->setEnabled(false);
        }
    } else if (pythonMaxPointsSpin) {
        pythonMaxPointsSpin->setEnabled(false);
    }
    for (int i = 0; i < animationFramePoints.size(); ++i) {
        QImage frameMask;
        QString error;
        if (!runGsForTargetPoints(animationFramePoints.at(i), iterSnapshot, frameMask, &error)) {
            animationPrecomputedMasks.clear();
            animationPrecomputeReady = false;
            if (animationParticlesSpin) {
                animationParticlesSpin->setEnabled(true);
            }
            if (pythonMaxPointsSpin) {
                pythonMaxPointsSpin->setEnabled(true);
            }
            if (showWarnings) {
                const QString title = activeSequenceSource == SequenceSource::PythonScript
                    ? QString("Python Precompute")
                    : QString("Animation Precompute");
                QMessageBox::warning(this,
                                     title,
                                     QString("Failed at frame %1/%2: %3")
                                         .arg(i + 1)
                                         .arg(animationFramePoints.size())
                                         .arg(error.isEmpty() ? "GS frame generation failed." : error));
            }
            return false;
        }
        animationPrecomputedMasks.append(frameMask);
    }

    animationPrecomputeReady = true;
    const QString sourceLabel = activeSequenceSource == SequenceSource::PythonScript ? "python" : "animation";
    statusBar()->showMessage(
        QString("Precomputed %1 phase masks for %2 playback.")
            .arg(animationPrecomputedMasks.size())
            .arg(sourceLabel),
        5000);
    return true;
}

void MainWindow::clearAnimationSequenceState(bool clearPreviews) {
    animationFramePoints.clear();
    animationPrecomputedMasks.clear();
    animationCurrentFrameIndex = 0;
    animationSequenceReady = false;
    animationPrecomputeReady = false;
    animationComputeLimitedWarned = false;
    selectedSequenceTrapIndexOneBased = -1;
    if (pythonTrapSelectorCombo) {
        pythonTrapSelectorCombo->blockSignals(true);
        pythonTrapSelectorCombo->clear();
        pythonTrapSelectorCombo->addItem("None", -1);
        pythonTrapSelectorCombo->setCurrentIndex(0);
        pythonTrapSelectorCombo->blockSignals(false);
    }

    if (clearPreviews) {
        if (animationIntensityPreviewLabel) {
            animationIntensityPreviewLabel->clear();
            animationIntensityPreviewLabel->setText("No frame");
        }
        if (animationCameraPreviewLabel) {
            animationCameraPreviewLabel->clear();
            animationCameraPreviewLabel->setText("No frame");
        }
        if (pythonIntensityPreviewLabel) {
            pythonIntensityPreviewLabel->clear();
            pythonIntensityPreviewLabel->setText("No frame");
        }
        if (pythonCameraPreviewLabel) {
            pythonCameraPreviewLabel->clear();
            pythonCameraPreviewLabel->setText("No frame");
        }
    }
}

QString MainWindow::defaultPythonScriptTemplate() const {
    return QString::fromUtf8(
        "import math\n"
        "\n"
        "# Regular polygon – Example 1\n"
        "# Returns a list of [x, y] points arranged as a regular N-gon.\n"
        "# Origin is the camera centre; units are pixels.\n"
        "# Load more examples via File → Load .py or from the python_examples/ folder.\n"
        "\n"
        "def build_pattern(width, height):\n"
        "    N      = 8                           # number of vertices\n"
        "    radius = min(width, height) * 0.25\n"
        "    pts = []\n"
        "    for i in range(N):\n"
        "        a = 2 * math.pi * i / N - math.pi / 2\n"
        "        pts.append([radius * math.cos(a), radius * math.sin(a)])\n"
        "    return pts\n");
}

void MainWindow::toggleTheme() {
    isDarkMode = !isDarkMode;
    applyTheme(isDarkMode);

    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("UI/DarkMode", isDarkMode);
    settings.sync();
}

void MainWindow::applyTheme(bool dark) {
    QString styleSheet;
    if (dark) {
        // Keep dark mode intentionally minimal/stable to avoid startup crashes caused by
        // aggressive QSS sub-control overrides on some Windows/Qt combinations.
        styleSheet = R"(
QMainWindow, QWidget {
    background-color: #2b2b2b;
    color: #e0e0e0;
    font-family: Arial, sans-serif;
    font-size: 10pt;
}
QGroupBox {
    border: 1px solid #555;
    border-radius: 4px;
    margin-top: 12px;
    padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 4px;
    background-color: #2b2b2b;
    color: #e0e0e0;
    font-weight: bold;
}
QPushButton, QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
    background-color: #3c3f41;
    border: 1px solid #555;
    border-radius: 3px;
    padding: 4px;
}
QTabWidget::pane {
    border: 1px solid #555;
    background: #3c3f41;
}
QTabBar::tab {
    background: #2b2b2b;
    border: 1px solid #555;
    padding: 6px 12px;
}
QTabBar::tab:selected {
    background: #4b4d4f;
    font-weight: bold;
}
QMenuBar, QMenu {
    background-color: #2b2b2b;
}
QStatusBar {
    background-color: #2b2b2b;
    border-top: 1px solid #444;
}
#cameraFeedLabel, #phaseMaskLabel {
    background-color: black;
    color: white;
    border: 1px solid #444;
}
QGraphicsView {
    background-color: #1a1a1a;
    border: 1px solid #444;
}
)";
    } else {
        const QString themeName = "light_theme.qss";
        const QStringList diskCandidates = {
            QDir(QCoreApplication::applicationDirPath()).filePath("resources/" + themeName),
            QDir(QCoreApplication::applicationDirPath()).filePath("../resources/" + themeName),
            QDir(QCoreApplication::applicationDirPath()).filePath("../../resources/" + themeName),
            QDir::current().filePath("resources/" + themeName)
        };
        for (const QString &candidate : diskCandidates) {
            QFile diskFile(candidate);
            if (diskFile.exists() && diskFile.open(QFile::ReadOnly | QFile::Text)) {
                styleSheet = QTextStream(&diskFile).readAll();
                diskFile.close();
                break;
            }
        }
        if (styleSheet.isEmpty()) {
            QFile qrcFile(":/light_theme.qss");
            if (qrcFile.open(QFile::ReadOnly | QFile::Text)) {
                styleSheet = QTextStream(&qrcFile).readAll();
                qrcFile.close();
            }
        }
    }
    if (!styleSheet.isEmpty()) {
        qApp->setStyleSheet(styleSheet);
    }
    // Update grid title bar styling
    if (gridTitleBar) {
        if (dark) {
            gridTitleBar->setStyleSheet("background-color: #3c3f41; border: 1px solid #555;");
            if (gridTitleLabel) {
                gridTitleLabel->setStyleSheet("color: #e0e0e0; font-size: 11px; font-weight: bold;");
            }
        } else {
            gridTitleBar->setStyleSheet("background-color: #e2e6ea; border: 1px solid #bcc1cb;");
            if (gridTitleLabel) {
                gridTitleLabel->setStyleSheet("color: #111827; font-size: 11px; font-weight: bold;");
            }
        }
    }
    // Update grid widget theme
    if (targetGridWidget) {
        targetGridWidget->setDarkMode(dark);
    }
}
QString MainWindow::configPath() const {
    return hardwareConfigPath();
}

bool MainWindow::isSelectedMonitorAvailable() const {
    const int count = QGuiApplication::screens().size();
    return selectedMonitorNumber >= 1 && selectedMonitorNumber <= count;
}

QScreen *MainWindow::selectedScreen() const {
    if (!isSelectedMonitorAvailable()) {
        return nullptr;
    }
    return QGuiApplication::screens().at(selectedMonitorNumber - 1);
}

void MainWindow::persistSelectedMonitor() {
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("Hardware/SLM_SelectedMonitor", selectedMonitorNumber);
    settings.sync();
}

bool MainWindow::isSelectedCameraPreviewMonitorAvailable() const {
    const int count = QGuiApplication::screens().size();
    return cameraPreviewMonitorNumber >= 1 && cameraPreviewMonitorNumber <= count;
}

QScreen *MainWindow::selectedCameraPreviewScreen() const {
    if (!isSelectedCameraPreviewMonitorAvailable()) {
        return nullptr;
    }
    return QGuiApplication::screens().at(cameraPreviewMonitorNumber - 1);
}

void MainWindow::persistSelectedCameraPreviewMonitor() {
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("Hardware/CameraPreview_SelectedMonitor", cameraPreviewMonitorNumber);
    settings.sync();
}

void MainWindow::persistCorrectionPath(const QString &path) {
    correctionMaskPath = path;
    QSettings settings(configPath(), QSettings::IniFormat);
    if (path.isEmpty()) {
        settings.remove("Hardware/SLM_CorrectionPath");
    } else {
        settings.setValue("Hardware/SLM_CorrectionPath", path);
    }
    settings.sync();
}

void MainWindow::ensureDirectOutputWindow() {
    if (directOutputWindow) {
        return;
    }

    directOutputWindow = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    directOutputWindow->setAttribute(Qt::WA_QuitOnClose, false);
    directOutputWindow->setWindowFlag(Qt::BypassWindowManagerHint, true);
    directOutputLabel = new QLabel(directOutputWindow);
    directOutputLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    directOutputLabel->setScaledContents(false);
}

void MainWindow::displayDirectOutput(const QImage &finalMask) {
    QScreen *screen = selectedScreen();
    if (!screen) {
        return;
    }

    ensureDirectOutputWindow();

    const QRect screenGeometry = screen->geometry();
    QImage canvas(screenGeometry.size(), QImage::Format_Grayscale8);
    canvas.fill(0);

    // Always render at SLM settings resolution, not full monitor resolution.
    const int targetW = qBound(1, slmWidth, canvas.width());
    const int targetH = qBound(1, slmHeight, canvas.height());
    const int maxOffsetX = qMax(0, canvas.width() - targetW);
    const int maxOffsetY = qMax(0, canvas.height() - targetH);
    const int drawX = qBound(0, slmActiveOffsetX, maxOffsetX);
    const int drawY = qBound(0, slmActiveOffsetY, maxOffsetY);

    QRect targetRect(drawX, drawY, targetW, targetH);

    QImage mapped = finalMask;
    if (mapped.size() != targetRect.size()) {
        mapped = mapped.scaled(targetRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QPainter painter(&canvas);
    painter.drawImage(targetRect.topLeft(), mapped);
    painter.end();

    directOutputWindow->setGeometry(screenGeometry);
    directOutputLabel->setGeometry(0, 0, canvas.width(), canvas.height());
    directOutputLabel->setPixmap(QPixmap::fromImage(canvas));

    directOutputWindow->createWinId();
    if (directOutputWindow->windowHandle()) {
        directOutputWindow->windowHandle()->setScreen(screen);
    }

    directOutputWindow->showFullScreen();
    directOutputWindow->raise();
}

void MainWindow::clearDirectOutput() {
    if (!directOutputWindow) {
        return;
    }

    directOutputWindow->hide();
}

QImage MainWindow::buildCameraDisplayImage(const QImage &img, bool includeTargetOverlay) const {
    if (img.isNull()) {
        return QImage();
    }

    QImage displayImg = img.convertToFormat(QImage::Format_ARGB32);

    if (cameraViewRotationDegrees != 0 || flipCameraX || flipCameraY) {
        QTransform transform;
        if (flipCameraX || flipCameraY) {
            transform.scale(flipCameraX ? -1 : 1, flipCameraY ? -1 : 1);
        }
        if (cameraViewRotationDegrees != 0) {
            transform.rotate(static_cast<qreal>(cameraViewRotationDegrees));
        }
        displayImg = displayImg.transformed(transform, Qt::SmoothTransformation);
    }

    if (includeTargetOverlay && overlayTargetCb && overlayTargetCb->isChecked() && !gridPointData.isEmpty()) {
        QPainter painter(&displayImg);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int imgW = displayImg.width();
        const int imgH = displayImg.height();
        const double displayHalfW = static_cast<double>(imgW) / 2.0;
        const double displayHalfH = static_cast<double>(imgH) / 2.0;
        const int pointRadius = qMax(3, qMin(imgW, imgH) / 90);
        const int highlightRadius = pointRadius + 4;

        for (auto it = gridPointData.constBegin(); it != gridPointData.constEnd(); ++it) {
            const int pointId = it.key();
            const QPointF p = it.value();

            // gridPointData is stored in the same centered coordinate system as the
            // transformed target camera view, so map directly into the displayed frame.
            const QPointF imagePoint(displayHalfW + p.x(), displayHalfH - p.y());
            const int px = qBound(0, static_cast<int>(qRound(imagePoint.x())), imgW - 1);
            const int py = qBound(0, static_cast<int>(qRound(imagePoint.y())), imgH - 1);

            if (pointId == selectedPointId) {
                painter.setPen(QPen(QColor(120, 255, 120, 190), 2));
                painter.setBrush(QColor(120, 255, 120, 80));
                painter.drawEllipse(QPoint(px, py), highlightRadius, highlightRadius);
            }

            painter.setPen(QPen(QColor(80, 190, 255, 210), 2));
            painter.setBrush(QColor(80, 190, 255, 95));
            painter.drawEllipse(QPoint(px, py), pointRadius, pointRadius);
        }
    }

    return displayImg;
}

QImage MainWindow::applyCameraZoomToDisplayImage(const QImage &img) const {
    if (img.isNull() || !cameraZoomEnabled) {
        return img;
    }

    const QRect cropRect = cameraZoomRectForSize(img.size());
    if (cropRect.width() <= 0 || cropRect.height() <= 0 ||
        cropRect == QRect(QPoint(0, 0), img.size())) {
        return img;
    }
    return img.copy(cropRect);
}

QRect MainWindow::cameraZoomRectForSize(const QSize &size) const {
    if (!cameraZoomEnabled || size.width() <= 0 || size.height() <= 0) {
        return QRect(QPoint(0, 0), size);
    }

    const QRectF roi = normalizedCameraZoomRoiForSize(size, cameraZoomRoiNormalized);
    const int x0 = qBound(0, static_cast<int>(qFloor(roi.left() * size.width())), size.width() - 1);
    const int y0 = qBound(0, static_cast<int>(qFloor(roi.top() * size.height())), size.height() - 1);
    const int x1 = qBound(x0 + 1, static_cast<int>(qCeil(roi.right() * size.width())), size.width());
    const int y1 = qBound(y0 + 1, static_cast<int>(qCeil(roi.bottom() * size.height())), size.height());
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

QRectF MainWindow::normalizedCameraZoomRoiForSize(const QSize &size, const QRectF &roi) const {
    if (size.width() <= 0 || size.height() <= 0) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal left = qBound(0.0, qMin(roi.left(), roi.right()), 1.0);
    const qreal right = qBound(0.0, qMax(roi.left(), roi.right()), 1.0);
    const qreal top = qBound(0.0, qMin(roi.top(), roi.bottom()), 1.0);
    const qreal bottom = qBound(0.0, qMax(roi.top(), roi.bottom()), 1.0);

    qreal width = qMax<qreal>(kMinZoomRoiNormalized, right - left);
    qreal height = qMax<qreal>(kMinZoomRoiNormalized, bottom - top);
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

    width = qBound<qreal>(kMinZoomRoiNormalized, width, 1.0);
    height = qBound<qreal>(kMinZoomRoiNormalized, height, 1.0);

    const qreal cx = qBound(0.0, (left + right) * 0.5, 1.0);
    const qreal cy = qBound(0.0, (top + bottom) * 0.5, 1.0);
    const qreal x = qBound(0.0, cx - width * 0.5, 1.0 - width);
    const qreal y = qBound(0.0, cy - height * 0.5, 1.0 - height);
    return QRectF(x, y, width, height);
}

QRect MainWindow::cameraFeedDrawRectForImage(const QSize &imageSize) const {
    return previewDrawRectForImage(cameraFeedLabel, imageSize);
}

QRect MainWindow::previewDrawRectForImage(const QLabel *label, const QSize &imageSize) const {
    if (!label || imageSize.width() <= 0 || imageSize.height() <= 0) {
        return QRect();
    }
    QSize targetSize = imageSize;
    targetSize.scale(label->size(), Qt::KeepAspectRatio);
    const int x = (label->width() - targetSize.width()) / 2;
    const int y = (label->height() - targetSize.height()) / 2;
    return QRect(QPoint(x, y), targetSize);
}

QRectF MainWindow::normalizedSelectionFromPoints(const QPoint &start, const QPoint &end, const QRect &drawRect) const {
    if (drawRect.isEmpty()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const QPoint startClamped(qBound(drawRect.left(), start.x(), drawRect.right()),
                              qBound(drawRect.top(), start.y(), drawRect.bottom()));
    const QPoint endClamped(qBound(drawRect.left(), end.x(), drawRect.right()),
                            qBound(drawRect.top(), end.y(), drawRect.bottom()));

    const int left = qMin(startClamped.x(), endClamped.x());
    const int right = qMax(startClamped.x(), endClamped.x());
    const int top = qMin(startClamped.y(), endClamped.y());
    const int bottom = qMax(startClamped.y(), endClamped.y());

    const qreal w = qMax(1, drawRect.width());
    const qreal h = qMax(1, drawRect.height());
    return QRectF((left - drawRect.left()) / w,
                  (top - drawRect.top()) / h,
                  (right - left) / w,
                  (bottom - top) / h);
}

void MainWindow::updateCameraPixelReadout(const QPoint &labelPos, bool validHover, const QLabel *sourceLabel) {
    if (!cameraPixelLabel) {
        return;
    }

    if (!validHover || lastRenderedCameraFrame.isNull() || lastZoomedRenderedCameraFrame.isNull()) {
        cameraPixelLabel->setText("Camera: --, -- | I: --");
        if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
            updateExternalCameraPreview();
        }
        return;
    }

    const QLabel *activeLabel = sourceLabel ? sourceLabel : cameraFeedLabel;
    const QRect drawRect = previewDrawRectForImage(activeLabel, lastZoomedRenderedCameraFrame.size());
    if (drawRect.isEmpty() || !drawRect.contains(labelPos)) {
        cameraPixelLabel->setText("Camera: --, -- | I: --");
        if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
            updateExternalCameraPreview();
        }
        return;
    }

    const qreal nx = (labelPos.x() - drawRect.left()) / static_cast<qreal>(qMax(1, drawRect.width()));
    const qreal ny = (labelPos.y() - drawRect.top()) / static_cast<qreal>(qMax(1, drawRect.height()));
    const qreal clampedNx = qBound(0.0, nx, 1.0);
    const qreal clampedNy = qBound(0.0, ny, 1.0);

    const QRect cropRect = cameraZoomRectForSize(lastRenderedCameraFrame.size());
    if (cropRect.width() <= 0 || cropRect.height() <= 0) {
        cameraPixelLabel->setText("Camera: --, -- | I: --");
        return;
    }

    const int xInCrop = qBound(0, static_cast<int>(qFloor(clampedNx * cropRect.width())), cropRect.width() - 1);
    const int yInCrop = qBound(0, static_cast<int>(qFloor(clampedNy * cropRect.height())), cropRect.height() - 1);
    const int absX = cropRect.x() + xInCrop;
    const int absY = cropRect.y() + yInCrop;
    const QRgb rgb = lastRenderedCameraFrame.pixel(absX, absY);
    const int intensity = qGray(rgb);
    const int centeredX = absX - (lastRenderedCameraFrame.width() / 2);
    const int centeredY = (lastRenderedCameraFrame.height() / 2) - absY;
    cameraPixelLabel->setText(QString("Camera: %1, %2 | I: %3").arg(centeredX).arg(centeredY).arg(intensity));
    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
}

void MainWindow::updateGridHoverReadout(const QPoint &viewportPos, bool validHover) {
    if (!gridHoverLabel || !targetGridWidget) {
        return;
    }

    if (selectedPointId >= 0 && gridPointData.contains(selectedPointId)) {
        const QPointF p = gridPointData.value(selectedPointId);
        gridHoverLabel->setText(QString("Grid: %1, %2").arg((int)p.x()).arg((int)p.y()));
        return;
    }

    if (!validHover) {
        gridHoverLabel->setText("Grid: --, --");
        return;
    }

    const QPointF scenePos = targetGridWidget->mapToScene(viewportPos);
    const QRectF bounds = targetGridWidget->sceneRect();
    if (!bounds.contains(scenePos)) {
        gridHoverLabel->setText("Grid: --, --");
        return;
    }

    const QPointF logicalPos = targetGridWidget->sceneToPixel(scenePos);
    const int gx = qRound(logicalPos.x());
    const int gy = qRound(logicalPos.y());
    gridHoverLabel->setText(QString("Grid: %1, %2").arg(gx).arg(gy));
}

void MainWindow::updateCameraFeedLabel(const QImage &displayImg) {
    updatePreviewLabelImage(cameraFeedLabel, displayImg, true);
}

void MainWindow::updatePreviewLabelImage(QLabel *label, const QImage &displayImg, bool showZoomOverlay) {
    if (!label || displayImg.isNull()) {
        return;
    }

    label->setText(QString());
    QPixmap pixmap = QPixmap::fromImage(displayImg).scaled(
        label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (showZoomOverlay && cameraZoomDragActive && !pixmap.isNull()) {
        const QRect drawRect = previewDrawRectForImage(label, displayImg.size());
        if (!drawRect.isEmpty()) {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing, true);

            const QRect selectionRect(QPoint(qMin(cameraZoomDragStart.x(), cameraZoomDragCurrent.x()),
                                             qMin(cameraZoomDragStart.y(), cameraZoomDragCurrent.y())),
                                      QPoint(qMax(cameraZoomDragStart.x(), cameraZoomDragCurrent.x()),
                                             qMax(cameraZoomDragStart.y(), cameraZoomDragCurrent.y())));
            const QRect clamped = selectionRect.intersected(drawRect);
            if (!clamped.isEmpty()) {
                QRect pixRect = clamped.translated(-drawRect.topLeft());
                pixRect = pixRect.intersected(QRect(QPoint(0, 0), pixmap.size()));
                painter.setPen(QPen(QColor(255, 210, 84), 2, Qt::DashLine));
                painter.setBrush(QColor(255, 210, 84, 42));
                painter.drawRect(pixRect);
            }
        }
    }

    label->setPixmap(pixmap);
}

void MainWindow::publishXpSenderOverlay() {
    if (!xpSenderOverlaySocket || !camManager) {
        return;
    }

    if (cameraBackend != static_cast<int>(CameraManager::CameraBackend::UdpStream) ||
        !cameraFeedActive ||
        lastCameraFrame.isNull()) {
        clearXpSenderOverlay();
        return;
    }

    const QString senderIp = camManager->latestUdpSenderIp().trimmed();
    if (senderIp.isEmpty()) {
        return;
    }

    XpSenderOverlayPacketHeader header{};
    std::memcpy(header.magic, kXpSenderOverlayMagic, sizeof(header.magic));
    header.version = kXpSenderOverlayProtocolVersion;
    header.pointCount = 0;
    header.imageWidth = static_cast<quint16>(qBound(0, lastCameraFrame.width(), 65535));
    header.imageHeight = static_cast<quint16>(qBound(0, lastCameraFrame.height(), 65535));
    header.frameId = camManager->latestUdpFrameId();
    header.rotationDegrees = static_cast<quint16>(normalizeCameraRotation(cameraViewRotationDegrees));
    if (overlayTargetCb && overlayTargetCb->isChecked()) {
        header.flags |= kXpSenderOverlayFlagEnabled;
    }
    if (flipCameraX) {
        header.flags |= kXpSenderOverlayFlagFlipX;
    }
    if (flipCameraY) {
        header.flags |= kXpSenderOverlayFlagFlipY;
    }

    QByteArray payload;
    payload.reserve(static_cast<int>(sizeof(XpSenderOverlayPacketHeader) +
                                     sizeof(XpSenderOverlayPoint) * gridPointData.size()));
    payload.append(reinterpret_cast<const char *>(&header), static_cast<int>(sizeof(header)));

    if ((header.flags & kXpSenderOverlayFlagEnabled) != 0) {
        const int imgW = lastCameraFrame.width();
        const int imgH = lastCameraFrame.height();
        const int displayW = lastRenderedCameraFrame.width();
        const int displayH = lastRenderedCameraFrame.height();
        const double displayHalfW = static_cast<double>(displayW) / 2.0;
        const double displayHalfH = static_cast<double>(displayH) / 2.0;
        QTransform displayTransform;
        if (flipCameraX || flipCameraY) {
            displayTransform.scale(flipCameraX ? -1 : 1, flipCameraY ? -1 : 1);
        }
        if (cameraViewRotationDegrees != 0) {
            displayTransform.rotate(static_cast<qreal>(cameraViewRotationDegrees));
        }
        const QPolygonF mappedRect = displayTransform.map(QPolygonF(QRectF(0.0, 0.0, imgW, imgH)));
        const QRectF mappedBounds = mappedRect.boundingRect();
        const QTransform displayToImageTransform = displayTransform.inverted();

        quint16 pointCount = 0;
        for (auto it = gridPointData.constBegin(); it != gridPointData.constEnd(); ++it) {
            const QPointF displayPixelPoint(displayHalfW + it.value().x(), displayHalfH - it.value().y());
            const QPointF rawImagePoint = displayToImageTransform.map(displayPixelPoint + mappedBounds.topLeft());
            XpSenderOverlayPoint point{};
            point.x = static_cast<quint16>(qBound(0, static_cast<int>(qRound(rawImagePoint.x())), imgW - 1));
            point.y = static_cast<quint16>(qBound(0, static_cast<int>(qRound(rawImagePoint.y())), imgH - 1));
            if (it.key() == selectedPointId) {
                point.flags |= kXpSenderOverlayPointFlagSelected;
            }
            payload.append(reinterpret_cast<const char *>(&point), static_cast<int>(sizeof(point)));
            ++pointCount;
        }

        if (pointCount > 0) {
            auto *mutableHeader = reinterpret_cast<XpSenderOverlayPacketHeader *>(payload.data());
            mutableHeader->pointCount = pointCount;
        }
    }

    xpSenderOverlaySocket->writeDatagram(payload, QHostAddress(senderIp), kXpSenderOverlayPort);
}

void MainWindow::clearXpSenderOverlay() {
    if (!xpSenderOverlaySocket || !camManager) {
        return;
    }

    const QString senderIp = camManager->latestUdpSenderIp().trimmed();
    if (senderIp.isEmpty()) {
        return;
    }

    XpSenderOverlayPacketHeader header{};
    std::memcpy(header.magic, kXpSenderOverlayMagic, sizeof(header.magic));
    header.version = kXpSenderOverlayProtocolVersion;
    header.imageWidth = static_cast<quint16>(qBound(0, lastCameraFrame.width(), 65535));
    header.imageHeight = static_cast<quint16>(qBound(0, lastCameraFrame.height(), 65535));
    header.frameId = camManager->latestUdpFrameId();
    header.rotationDegrees = static_cast<quint16>(normalizeCameraRotation(cameraViewRotationDegrees));

    const QByteArray payload(reinterpret_cast<const char *>(&header), static_cast<int>(sizeof(header)));
    xpSenderOverlaySocket->writeDatagram(payload, QHostAddress(senderIp), kXpSenderOverlayPort);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    const bool mainPreviewHovered = (watched == cameraFeedLabel && cameraFeedLabel);
    const bool externalPreviewHovered = (watched == cameraPreviewWindowLabel && cameraPreviewWindowLabel);
    if (mainPreviewHovered || externalPreviewHovered) {
        QLabel *activeLabel = mainPreviewHovered ? cameraFeedLabel : cameraPreviewWindowLabel;
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                cameraZoomDragActive = false;
                undoCameraZoomStep();
                updateCameraPixelReadout(mouseEvent->pos(), true, activeLabel);
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton && !lastZoomedRenderedCameraFrame.isNull()) {
                const QRect drawRect = previewDrawRectForImage(activeLabel, lastZoomedRenderedCameraFrame.size());
                if (drawRect.contains(mouseEvent->pos())) {
                    cameraZoomDragActive = true;
                    cameraZoomDragStart = mouseEvent->pos();
                    cameraZoomDragCurrent = cameraZoomDragStart;
                    updatePreviewLabelImage(activeLabel, lastZoomedRenderedCameraFrame, true);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            updateCameraPixelReadout(mouseEvent->pos(), true, activeLabel);
            if (cameraZoomDragActive) {
                cameraZoomDragCurrent = mouseEvent->pos();
                updatePreviewLabelImage(activeLabel, lastZoomedRenderedCameraFrame, true);
                return true;
            }
        } else if (event->type() == QEvent::Leave) {
            updateCameraPixelReadout(QPoint(), false, activeLabel);
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (cameraZoomDragActive && mouseEvent->button() == Qt::LeftButton && !lastRenderedCameraFrame.isNull()) {
                cameraZoomDragActive = false;
                cameraZoomDragCurrent = mouseEvent->pos();

                const QRect drawRect = previewDrawRectForImage(activeLabel, lastZoomedRenderedCameraFrame.size());
                const QRect currentCropRect = cameraZoomRectForSize(lastRenderedCameraFrame.size());
                const QRectF localNorm = normalizedSelectionFromPoints(cameraZoomDragStart, cameraZoomDragCurrent, drawRect);

                QRectF absoluteNorm(
                    (currentCropRect.x() + localNorm.x() * currentCropRect.width()) / static_cast<qreal>(lastRenderedCameraFrame.width()),
                    (currentCropRect.y() + localNorm.y() * currentCropRect.height()) / static_cast<qreal>(lastRenderedCameraFrame.height()),
                    (localNorm.width() * currentCropRect.width()) / static_cast<qreal>(lastRenderedCameraFrame.width()),
                    (localNorm.height() * currentCropRect.height()) / static_cast<qreal>(lastRenderedCameraFrame.height()));

                onCameraZoomRoiChanged(absoluteNorm, true);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton || mouseEvent->button() == Qt::RightButton) {
                cameraZoomDragActive = false;
                resetCameraZoom();
                updateCameraPixelReadout(mouseEvent->pos(), true, activeLabel);
                return true;
            }
        }
    }

    if (targetGridWidget && watched == targetGridWidget->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            updateGridHoverReadout(mouseEvent->pos(), true);
        } else if (event->type() == QEvent::Leave) {
            updateGridHoverReadout(QPoint(), false);
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshCameraPreviewMonitorOptions() {
    if (!cameraPreviewMonitorCombo) {
        return;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    QSignalBlocker blocker(cameraPreviewMonitorCombo);
    cameraPreviewMonitorCombo->clear();

    for (int i = 0; i < screens.size(); ++i) {
        QScreen *screen = screens.at(i);
        const QSize size = screen->geometry().size();
        cameraPreviewMonitorCombo->addItem(
            QString("Monitor %1: %2 (%3x%4)")
                .arg(i + 1)
                .arg(screen->name())
                .arg(size.width())
                .arg(size.height()),
            i + 1);
    }

    cameraPreviewMonitorCombo->setEnabled(!screens.isEmpty());

    const int selectedIndex = cameraPreviewMonitorCombo->findData(cameraPreviewMonitorNumber);
    if (selectedIndex >= 0) {
        cameraPreviewMonitorCombo->setPlaceholderText("Select monitor");
        cameraPreviewMonitorCombo->setCurrentIndex(selectedIndex);
        cameraPreviewMonitorCombo->setToolTip(cameraPreviewMonitorCombo->itemText(selectedIndex));
    } else if (screens.isEmpty()) {
        cameraPreviewMonitorCombo->setCurrentIndex(-1);
        cameraPreviewMonitorCombo->setPlaceholderText("No monitors detected");
        cameraPreviewMonitorCombo->setToolTip("No connected monitors are available for camera preview.");
    } else {
        cameraPreviewMonitorCombo->setCurrentIndex(-1);
        cameraPreviewMonitorCombo->setPlaceholderText(
            QString("Monitor %1 unavailable").arg(cameraPreviewMonitorNumber));
        cameraPreviewMonitorCombo->setToolTip(
            QString("Selected monitor %1 is not connected. Choose another monitor.")
                .arg(cameraPreviewMonitorNumber));
    }

    updateCameraPreviewButtonState();
}

void MainWindow::updateCameraPreviewButtonState() {
    if (!cameraPreviewToggleBtn) {
        return;
    }

    const bool monitorAvailable = isSelectedCameraPreviewMonitorAvailable();
    cameraPreviewToggleBtn->setEnabled(monitorAvailable);
    cameraPreviewToggleBtn->setToolTip(monitorAvailable
                                           ? "Mirror the current camera preview on the selected monitor."
                                           : "Choose a connected monitor for camera preview.");
    updateCameraPreviewButtonText();
}

void MainWindow::updateCameraPreviewButtonText() {
    if (!cameraPreviewToggleBtn) {
        return;
    }

    cameraPreviewToggleBtn->setText(cameraPreviewToggleBtn->isChecked()
                                        ? "Hide From Monitor"
                                        : "Show On Monitor");
}

void MainWindow::ensureCameraPreviewWindow() {
    if (cameraPreviewWindow) {
        return;
    }

    cameraPreviewWindow = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    cameraPreviewWindow->setAttribute(Qt::WA_QuitOnClose, false);
    cameraPreviewWindow->setAttribute(Qt::WA_ShowWithoutActivating, true);
    cameraPreviewWindow->setWindowFlag(Qt::BypassWindowManagerHint, true);
    cameraPreviewWindow->setStyleSheet("background-color: #101010; color: #f0f0f0;");

    QVBoxLayout *windowLayout = new QVBoxLayout(cameraPreviewWindow);
    windowLayout->setContentsMargins(28, 28, 28, 28);
    windowLayout->setSpacing(18);

    cameraPreviewWindowLabel = new QLabel("Camera Feed (Offline)", cameraPreviewWindow);
    cameraPreviewWindowLabel->setObjectName("cameraPreviewWindowLabel");
    cameraPreviewWindowLabel->setAlignment(Qt::AlignCenter);
    cameraPreviewWindowLabel->setScaledContents(false);
    cameraPreviewWindowLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cameraPreviewWindowLabel->setMinimumSize(320, 200);
    cameraPreviewWindowLabel->setMouseTracking(true);
    cameraPreviewWindowLabel->setToolTip("Left-drag to zoom. Right-click to undo one zoom step. Double-click to reset zoom.");
    cameraPreviewWindowLabel->setStyleSheet("background-color: #000000; border: 2px solid #3a3a3a; border-radius: 12px;");
    cameraPreviewWindowLabel->installEventFilter(this);
    windowLayout->addWidget(cameraPreviewWindowLabel, 1);

    QWidget *infoPanel = new QWidget(cameraPreviewWindow);
    infoPanel->setStyleSheet("background-color: #1c1c1c; border-radius: 16px;");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(18, 18, 18, 18);
    infoLayout->setSpacing(12);

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(12);
    cameraPreviewWindowSaveBtn = new QPushButton("Save Image", infoPanel);
    cameraPreviewWindowRecordBtn = new QPushButton("Record Video", infoPanel);
    cameraPreviewWindowRecordBtn->setCheckable(true);
    cameraPreviewWindowRecordBtn->setStyleSheet("QPushButton:checked { background-color: #aa0000; color: white; border: 1px solid #ff0000; }");
    cameraPreviewWindowRecordTimeLabel = new QLabel("00:00", infoPanel);
    cameraPreviewWindowRecordTimeLabel->setStyleSheet("color: #ff4444; font-weight: bold; font-family: monospace;");
    buttonRow->addWidget(cameraPreviewWindowSaveBtn);
    buttonRow->addWidget(cameraPreviewWindowRecordBtn);
    buttonRow->addWidget(cameraPreviewWindowRecordTimeLabel);
    buttonRow->addStretch();
    infoLayout->addLayout(buttonRow);

    cameraPreviewWindowFpsLabel = new QLabel(fpsLabel ? fpsLabel->text() : "FPS: 0", infoPanel);
    cameraPreviewWindowPixelLabel = new QLabel(cameraPixelLabel ? cameraPixelLabel->text() : "Camera: --, -- | I: --", infoPanel);
    infoLayout->addWidget(cameraPreviewWindowFpsLabel);
    infoLayout->addWidget(cameraPreviewWindowPixelLabel);
    windowLayout->addWidget(infoPanel, 0);

    connect(cameraPreviewWindowSaveBtn, &QPushButton::clicked, this, [this]() {
        if (captureImageBtn) {
            captureImageBtn->click();
        }
    });
    connect(cameraPreviewWindowRecordBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (!recordVideoBtn) {
            return;
        }
        recordVideoBtn->setChecked(checked);
    });
}

void MainWindow::updateExternalCameraPreview() {
    if (!cameraPreviewToggleBtn || !cameraPreviewToggleBtn->isChecked()) {
        return;
    }

    QScreen *screen = selectedCameraPreviewScreen();
    if (!screen) {
        clearCameraPreviewOutput();
        return;
    }

    ensureCameraPreviewWindow();

    if (cameraFeedActive && !lastZoomedRenderedCameraFrame.isNull()) {
        updatePreviewLabelImage(cameraPreviewWindowLabel, lastZoomedRenderedCameraFrame, false);
    } else {
        cameraPreviewWindowLabel->setPixmap(QPixmap());
        cameraPreviewWindowLabel->setText("Camera Feed (Offline)");
    }

    const bool recording = recordVideoBtn && recordVideoBtn->isChecked();
    if (cameraPreviewWindowRecordBtn) {
        QSignalBlocker blocker(cameraPreviewWindowRecordBtn);
        cameraPreviewWindowRecordBtn->setChecked(recording);
        cameraPreviewWindowRecordBtn->setText(recording ? "Recording" : "Record Video");
    }
    if (cameraPreviewWindowRecordTimeLabel) {
        cameraPreviewWindowRecordTimeLabel->setText(
            (recordTimeLabel && recordTimeLabel->isVisible()) ? recordTimeLabel->text() : "00:00");
    }
    if (cameraPreviewWindowFpsLabel) {
        cameraPreviewWindowFpsLabel->setText(fpsLabel ? fpsLabel->text() : "FPS: --");
    }
    if (cameraPreviewWindowPixelLabel) {
        cameraPreviewWindowPixelLabel->setText(cameraPixelLabel ? cameraPixelLabel->text() : "Camera: --, -- | I: --");
    }

    const QRect screenGeometry = screen->geometry();
    const bool monitorChanged = (cameraPreviewWindowMonitorNumber != cameraPreviewMonitorNumber);
    const bool geometryChanged = (cameraPreviewWindow->geometry() != screenGeometry);
    const bool needsReshow = monitorChanged || geometryChanged || !cameraPreviewWindow->isVisible();

    cameraPreviewWindow->setGeometry(screenGeometry);

    if (needsReshow) {
        cameraPreviewWindow->createWinId();
        if (cameraPreviewWindow->windowHandle()) {
            cameraPreviewWindow->windowHandle()->setScreen(screen);
        }

        cameraPreviewWindow->showFullScreen();
        cameraPreviewWindowMonitorNumber = cameraPreviewMonitorNumber;
    }
}

void MainWindow::clearCameraPreviewOutput() {
    if (!cameraPreviewWindow) {
        return;
    }

    cameraPreviewWindow->hide();
    cameraPreviewWindowMonitorNumber = -1;
}

void MainWindow::handleCameraFeedStopped() {
    cameraFeedActive = false;
    updateCameraPixelReadout(QPoint(), false);
    if (cameraPreviewToggleBtn && cameraPreviewToggleBtn->isChecked()) {
        updateExternalCameraPreview();
    }
    clearXpSenderOverlay();
}

void MainWindow::refreshMonitorSelectionMenu() {
    if (!monitorSelectionMenu || !monitorActionGroup) {
        return;
    }

    monitorSelectionMenu->clear();
    const QList<QScreen *> screens = QGuiApplication::screens();

    for (QAction *action : monitorActionGroup->actions()) {
        monitorActionGroup->removeAction(action);
        action->deleteLater();
    }

    if (screens.isEmpty()) {
        QAction *noneAction = monitorSelectionMenu->addAction("No monitors detected");
        noneAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < screens.size(); ++i) {
        QScreen *screen = screens.at(i);
        const int monitorNumber = i + 1;
        const QSize size = screen->geometry().size();
        QString label = QString("Monitor %1: %2 (%3x%4)")
            .arg(monitorNumber)
            .arg(screen->name())
            .arg(size.width())
            .arg(size.height());

        QAction *action = monitorSelectionMenu->addAction(label);
        action->setCheckable(true);
        action->setData(monitorNumber);
        action->setChecked(monitorNumber == selectedMonitorNumber);
        monitorActionGroup->addAction(action);
    }

    if (!isSelectedMonitorAvailable()) {
        monitorSelectionMenu->addSeparator();
        QAction *missingAction = monitorSelectionMenu->addAction(
            QString("Selected monitor %1 is not connected").arg(selectedMonitorNumber));
        missingAction->setEnabled(false);
    }
}

void MainWindow::onMonitorActionTriggered(QAction *action) {
    if (!action) {
        return;
    }

    bool ok = false;
    const int monitor = action->data().toInt(&ok);
    if (!ok || monitor < 1) {
        return;
    }

    selectedMonitorNumber = monitor;
    persistSelectedMonitor();
    statusBar()->showMessage(QString("SLM monitor set to Monitor %1").arg(selectedMonitorNumber), 4000);
}

void MainWindow::tryAutoApplySavedCorrection() {
    if (correctionMaskPath.isEmpty()) {
        return;
    }

    QFileInfo fileInfo(correctionMaskPath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this,
                             "Correction Mask",
                             "Saved correction file is missing and has been cleared:\n" + correctionMaskPath);
        persistCorrectionPath(QString());
        return;
    }

    QImage loadedImage = QImage(correctionMaskPath).convertToFormat(QImage::Format_Grayscale8);
    if (loadedImage.isNull()) {
        QMessageBox::warning(this,
                             "Correction Mask",
                             "Saved correction file could not be loaded and has been cleared:\n" + correctionMaskPath);
        persistCorrectionPath(QString());
        return;
    }

    if (loadedImage.size() != QSize(slmWidth, slmHeight)) {
        loadedImage = loadedImage.scaled(slmWidth, slmHeight);
    }

    correctionMask = loadedImage;
    previewCorrectionCb->setEnabled(true);
    previewCorrectionCb->setChecked(true);
    updatePhasePreview();

    if (isSelectedMonitorAvailable()) {
        sendToSLM();
        statusBar()->showMessage("Saved correction mask auto-applied on startup.", 5000);
    } else {
        statusBar()->showMessage("Saved correction loaded. Select monitor in Tools > Select Monitor to apply.", 7000);
    }
}
// ==========================================
// DYNAMIC SLM LOGIC & PREVIEW
// ==========================================

// Combine the currently loaded hologram and correction into a single full-resolution
// image.  Performs identical modulo-256 addition used in sendToSLM()/updatePhasePreview.
// Returns a null QImage if nothing is available.
QImage MainWindow::composeFullResolutionMask(bool applyCorrection) const {
    // start with hologram or flat canvas
    QImage result;
    if (currentMask.isNull()) {
        if (correctionMask.isNull()) {
            return QImage(); // nothing to compose
        }
        // create blank image same size as correction (should match slm dims)
        result = QImage(slmWidth, slmHeight, QImage::Format_Grayscale8);
        result.fill(0);
    } else {
        result = currentMask.copy();
    }

    if (applyCorrection && !correctionMask.isNull()) {
        if (correctionMask.size() == result.size()) {
            for (int y = 0; y < result.height(); ++y) {
                uchar *rRow = result.scanLine(y);
                const uchar *cRow = correctionMask.constScanLine(y);
                for (int x = 0; x < result.width(); ++x) {
                    rRow[x] = static_cast<uchar>(rRow[x] + cRow[x]);
                }
            }
        }
        // if sizes mismatch we simply ignore correction; caller may warn separately
    }
    return result;
}

void MainWindow::updatePhasePreview() {
    if (currentMask.isNull()) {
        if (!correctionMask.isNull() && previewCorrectionCb->isChecked()) {
            phaseMaskLabel->setPixmap(QPixmap::fromImage(correctionMask).scaled(
                phaseMaskLabel->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        } else {
            phaseMaskLabel->clear();
            phaseMaskLabel->setText(slmLibrary.isLoaded() ? "Cleared / Offline" : "SLM Offline");
        }
        return;
    }

    QImage displayMask = currentMask.copy();

    if (previewCorrectionCb->isChecked() && !correctionMask.isNull()) {
        if (correctionMask.size() == displayMask.size()) {
            for (int y = 0; y < displayMask.height(); ++y) {
                uchar *pRow = displayMask.scanLine(y);
                const uchar *cRow = correctionMask.constScanLine(y);
                for (int x = 0; x < displayMask.width(); ++x) {
                    pRow[x] = static_cast<uchar>(pRow[x] + cRow[x]);
                }
            }
        }
    }

    phaseMaskLabel->setPixmap(QPixmap::fromImage(displayMask).scaled(
        phaseMaskLabel->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::receiveHologram(const QImage &mask) {
    currentMask = mask.convertToFormat(QImage::Format_Grayscale8);

    // Resize to SLM dimensions if needed
    if (currentMask.size() != QSize(slmWidth, slmHeight)) {
        currentMask = currentMask.scaled(slmWidth, slmHeight);
    }

    updatePhasePreview();
    autoSendToSlmIfEnabled();
    statusBar()->showMessage("Generated Hologram loaded successfully (resized to " + QString::number(slmWidth) + "x" + QString::number(slmHeight) + ").", 5000);
}

void MainWindow::sendHologramToSLM(const QImage &mask) {
    // Set the current mask to the generated hologram
    currentMask = mask.convertToFormat(QImage::Format_Grayscale8);
    
    // Resize to SLM dimensions if needed
    if (currentMask.size() != QSize(slmWidth, slmHeight)) {
        currentMask = currentMask.scaled(slmWidth, slmHeight);
    }
    
    // Update the preview
    updatePhasePreview();
    
    // Send directly to SLM
    sendToSLM();
    
    statusBar()->showMessage("Generated Hologram sent directly to SLM.", 5000);
}

void MainWindow::loadPhasePattern() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Phase Mask", "", "Images (*.png *.bmp *.jpg)");
    if (!fileName.isEmpty()) {
        QImage loadedImage = QImage(fileName).convertToFormat(QImage::Format_Grayscale8);

        // Check and warn about size mismatch
        if (loadedImage.size() != QSize(slmWidth, slmHeight)) {
            QString origSize = QString::number(loadedImage.width()) + "x" + QString::number(loadedImage.height());
            QString targetSize = QString::number(slmWidth) + "x" + QString::number(slmHeight);
            int ret = QMessageBox::warning(this, "Size Mismatch",
                "Phase mask size (" + origSize + ") does not match SLM resolution (" + targetSize + ").\n\nResize to SLM dimensions?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (ret == QMessageBox::Yes) {
                loadedImage = loadedImage.scaled(slmWidth, slmHeight);
            }
        }

        currentMask = loadedImage;
        updatePhasePreview();
        autoSendToSlmIfEnabled();
        statusBar()->showMessage("Mask loaded: " + fileName, 3000);
    }
}

void MainWindow::loadCorrectionFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Flatness Correction Mask", "", "Images (*.png *.bmp *.jpg)");
    if (!fileName.isEmpty()) {
        QImage loadedImage = QImage(fileName).convertToFormat(QImage::Format_Grayscale8);

        // Check and warn about size mismatch
        if (loadedImage.size() != QSize(slmWidth, slmHeight)) {
            QString origSize = QString::number(loadedImage.width()) + "x" + QString::number(loadedImage.height());
            QString targetSize = QString::number(slmWidth) + "x" + QString::number(slmHeight);
            int ret = QMessageBox::warning(this, "Size Mismatch",
                "Correction mask size (" + origSize + ") does not match SLM resolution (" + targetSize + ").\n\nResize to SLM dimensions?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (ret == QMessageBox::Yes) {
                loadedImage = loadedImage.scaled(slmWidth, slmHeight);
            } else {
                statusBar()->showMessage("Correction load cancelled due to size mismatch.", 3000);
                return;
            }
        }

        correctionMask = loadedImage;
        previewCorrectionCb->setEnabled(true);
        previewCorrectionCb->setChecked(true);

        persistCorrectionPath(QFileInfo(fileName).absoluteFilePath());

        updatePhasePreview();
        autoSendToSlmIfEnabled();
        statusBar()->showMessage("Correction Mask loaded.", 5000);
    }
}

void MainWindow::clearCorrectionMask() {
    if (correctionMask.isNull()) {
        statusBar()->showMessage("No correction mask is currently loaded.", 3000);
        return;
    }

    const bool hadTargetMask = !currentMask.isNull();

    correctionMask = QImage();
    persistCorrectionPath(QString());

    previewCorrectionCb->setChecked(false);
    previewCorrectionCb->setEnabled(false);
    updatePhasePreview();

    if (hadTargetMask) {
        autoSendToSlmIfEnabled();
        if (autoSendSlmEnabled) {
            statusBar()->showMessage("SLM correction removed. Target mask remains active.", 5000);
        } else {
            statusBar()->showMessage("SLM correction removed. Click Send to SLM to apply.", 5000);
        }
        return;
    }

    if (autoSendSlmEnabled) {
        if (slmOutputMode == DirectScreenOutputMode) {
            clearDirectOutput();
        } else if (slmLibrary.isLoaded()) {
            auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
            if (winTerm) {
                winTerm(slmWindowID);
            }
        }

        phaseMaskLabel->clear();
        phaseMaskLabel->setText("SLM Offline");
    }

    statusBar()->showMessage(autoSendSlmEnabled
                                 ? "SLM correction removed."
                                 : "SLM correction removed locally. Click Send to SLM to apply.",
                             5000);
}

void MainWindow::sendToSLM() {
    if (currentMask.isNull() && correctionMask.isNull()) {
        QMessageBox::warning(this, "Error", "No mask or correction loaded to send!");
        return;
    }

    if (!isSelectedMonitorAvailable()) {
        QMessageBox::warning(this,
                             "Monitor Selection",
                             QString("Selected monitor %1 is not connected.\nChoose a monitor in Tools > Select Monitor.")
                                 .arg(selectedMonitorNumber));
        return;
    }

    QImage finalMask = composeFullResolutionMask();
    if (finalMask.isNull()) {
        QMessageBox::warning(this, "Error", "Unable to compose final mask for SLM.");
        return;
    }

    if (slmOutputMode == DirectScreenOutputMode) {
        displayDirectOutput(finalMask);
    } else {
        if (!slmLibrary.isLoaded()) {
            QMessageBox::critical(this, "DLL Error", "Image_Control.dll is not loaded.");
            return;
        }

        auto winSettings = (Window_Settings_Func)slmLibrary.resolve("Window_Settings");
        auto winArrayToDisplay = (Window_Array_to_Display_Func)slmLibrary.resolve("Window_Array_to_Display");

        if (!winSettings || !winArrayToDisplay) {
            QMessageBox::critical(this, "DLL Error", "Could not find SLM functions inside the DLL.");
            return;
        }

        winSettings(selectedMonitorNumber, slmWindowID, 0, 0);

        int width = finalMask.width();
        int height = finalMask.height();
        uint8_t *rawData = finalMask.bits();

        winArrayToDisplay(rawData, width, height, slmWindowID, width * height);
    }

    if (!correctionMask.isNull() && currentMask.isNull()) {
        statusBar()->showMessage("Background Correction Mask sent to SLM.", 5000);
    } else if (!correctionMask.isNull()) {
        statusBar()->showMessage("Phase Mask + Correction sent to SLM.", 5000);
    } else {
        statusBar()->showMessage("Phase Mask sent to SLM.", 5000);
    }
}

void MainWindow::clearSLM() {
    currentMask = QImage();
    updatePhasePreview();

    if (!correctionMask.isNull()) {
        sendToSLM();
        phaseMaskLabel->setText("Cleared (Correction Active)");
        statusBar()->showMessage("Target cleared. SLM correction remains active.", 5000);
        return;
    }

    if (slmOutputMode == DirectScreenOutputMode) {
        clearDirectOutput();
    } else if (slmLibrary.isLoaded()) {
        auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
        if (winTerm) {
            winTerm(slmWindowID);
        }
    }

    phaseMaskLabel->clear();
    phaseMaskLabel->setText("SLM Offline");
    statusBar()->showMessage("SLM display completely terminated.", 3000);
}

void MainWindow::toggleGridEnlarged() {
    if (!gridEnlarged) {
        // Enlarge grid - hide all other controls, expand grid
        gridEnlarged = true;
        
        // Hide all controls in column 1 and 2 for all rows
        for (int row = 0; row < mainLayout->rowCount(); ++row) {
            for (int col = 1; col < mainLayout->columnCount(); ++col) {
                QLayoutItem *item = mainLayout->itemAtPosition(row, col);
                if (item) {
                    if (item->widget()) {
                        item->widget()->setVisible(false);
                    }
                }
            }
        }
        
        // Hide controls row (row 2) and traplist row (row 3) in column 0
        for (int row = 2; row < mainLayout->rowCount(); ++row) {
            QLayoutItem *item = mainLayout->itemAtPosition(row, 0);
            if (item && item->widget()) {
                item->widget()->setVisible(false);
            }
        }
        
        // Remove grid from current position
        mainLayout->removeWidget(gridTitleBar);
        mainLayout->removeWidget(targetGridWidget);
        
        // Re-add grid to span full width (0, 0 to 0, 2 for title, 1, 0 to 3, 2 for grid)
        mainLayout->addWidget(gridTitleBar, 0, 0, 1, 3);
        mainLayout->addWidget(targetGridWidget, 1, 0, 3, 3);
        
        // Force layout update
        mainLayout->update();
        
        // Update button to show minimize symbol
        gridMaxMinBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        gridMaxMinBtn->setToolTip("Restore to normal view");
    } else {
        // Minimize grid - show all controls, restore grid position
        gridEnlarged = false;
        
        // Remove grid from full-span position
        mainLayout->removeWidget(gridTitleBar);
        mainLayout->removeWidget(targetGridWidget);
        
        // Re-add grid to original position
        mainLayout->addWidget(gridTitleBar, 0, 0);
        mainLayout->addWidget(targetGridWidget, 1, 0);
        
        // Show all hidden controls
        for (int row = 0; row < mainLayout->rowCount(); ++row) {
            for (int col = 0; col < mainLayout->columnCount(); ++col) {
                QLayoutItem *item = mainLayout->itemAtPosition(row, col);
                if (item && item->widget()) {
                    item->widget()->setVisible(true);
                }
            }
        }
        
        // Force layout update
        mainLayout->update();
        
        // Update button to show maximize symbol
        gridMaxMinBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        gridMaxMinBtn->setToolTip("Enlarge grid view");
    }
}
