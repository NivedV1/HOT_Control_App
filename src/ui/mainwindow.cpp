#include "mainwindow.h"
#include "settingsdialog.h"
#include "hologramdialog.h"
#include "sourceintensitydialog.h"
#include "components/targetgridwidget.h"
#include "components/patternpresetswidget.h"
#include "components/arrowspinbox.h"
#include "../core/algorithms/gs_algorithm.h"
#include "../camera/cameramanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
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

namespace {
constexpr int kImageTabIndex = 2;
constexpr int kDefaultMonitorNumber = 2;
constexpr int kDefaultActiveWidth = 1272;
constexpr int kDefaultActiveHeight = 1024;
constexpr int kDefaultActiveOffsetX = 0;
constexpr int kDefaultActiveOffsetY = 0;
constexpr int kGsAutoRunDebounceMs = 180;

QString hardwareConfigPath() {
    return QCoreApplication::applicationDirPath() + "/hardware_config.ini";
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setWindowIcon(QIcon(":/favicon.ico"));
    setMinimumSize(800, 600);

    QSettings settings(configPath(), QSettings::IniFormat);

    slmWidth = settings.value("Hardware/SLM_Width", 1920).toInt();
    slmHeight = settings.value("Hardware/SLM_Height", 1080).toInt();
    slmPixelSize = settings.value("Hardware/SLM_PixelSize", 8.0).toDouble();
    cameraBackend = settings.value("Hardware/CameraBackend", 0).toInt();

    camWidth = settings.value("Hardware/Cam_Width", 1920).toInt();
    camHeight = settings.value("Hardware/Cam_Height", 1080).toInt();
    camPixelSize = settings.value("Hardware/Cam_PixelSize", 5.0).toDouble();
    laserWavelength = settings.value("Optical/Wavelength", 1064.0).toDouble();
    fourierFocalLength = settings.value("Optical/FocalLength", 100.0).toDouble();
    autoRunGsEnabled = settings.value("Hardware/AutoRunGS", false).toBool();
    autoSendSlmEnabled = settings.value("Hardware/AutoSendSLM", false).toBool();
    gsStartingPhaseMaskMode = settings.value("Hardware/GS_StartingPhaseMask", 0).toInt();

    isDarkMode = settings.value("UI/DarkMode", true).toBool();
    slmOutputMode = settings.value("Hardware/SLM_OutputMode", DllOutputMode).toInt();
    selectedMonitorNumber = settings.value("Hardware/SLM_SelectedMonitor", kDefaultMonitorNumber).toInt();
    slmActiveWidth = settings.value("Hardware/SLM_ActiveWidth", kDefaultActiveWidth).toInt();
    slmActiveHeight = settings.value("Hardware/SLM_ActiveHeight", kDefaultActiveHeight).toInt();
    slmActiveOffsetX = settings.value("Hardware/SLM_ActiveOffsetX", kDefaultActiveOffsetX).toInt();
    slmActiveOffsetY = settings.value("Hardware/SLM_ActiveOffsetY", kDefaultActiveOffsetY).toInt();
    correctionMaskPath = settings.value("Hardware/SLM_CorrectionPath", "").toString();

    setupUI();
    applyTheme(isDarkMode);

    camManager = new CameraManager(cameraBackend, this);
    setupConnections();

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

    // Safety check: close SLM if app is closed
    if (slmLibrary.isLoaded()) {
        auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
        if (winTerm) winTerm(slmWindowID);
    }
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

    monitorSelectionMenu = toolsMenu->addMenu("Select Monitor");
    monitorActionGroup = new QActionGroup(this);
    monitorActionGroup->setExclusive(true);
    connect(monitorSelectionMenu, &QMenu::aboutToShow, this, &MainWindow::refreshMonitorSelectionMenu);
    connect(monitorActionGroup, &QActionGroup::triggered, this, &MainWindow::onMonitorActionTriggered);

    menuBar()->addMenu("&Help");
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
    gridMaxMinBtn->setText("[Ã¢â€ â€˜]");  // Maximize symbol (up arrow)
    gridMaxMinBtn->setMaximumWidth(28);
    gridMaxMinBtn->setMaximumHeight(20);
    gridMaxMinBtn->setStyleSheet("padding: 0px; font-size: 12px;");
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
    
    cameraFeedLabel = new QLabel("Camera Feed (Offline)");
    cameraFeedLabel->setObjectName("cameraFeedLabel");
    cameraFeedLabel->setAlignment(Qt::AlignCenter);
    cameraFeedLabel->setScaledContents(true);  // Auto-scale pixmap with label size
    cameraFeedLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); 
    cameraFeedLabel->setMinimumSize(300, 200); 
    cameraColLayout->addWidget(cameraFeedLabel);
    
    // Camera tools
    QHBoxLayout *camTools = new QHBoxLayout();
    fpsLabel = new QLabel("FPS: 0");
    camTools->addWidget(fpsLabel);
    camTools->addStretch();
    camTools->addWidget(new QCheckBox("Overlay Target"));
    
    cameraColLayout->addLayout(camTools);
    cameraColumn->setLayout(cameraColLayout);
    layout->addWidget(cameraColumn, 1, 2);

    // Create wrapper for tools row (row 2) - grid column tools
    toolsRow = new QWidget();
    QHBoxLayout *gridToolsLayout = new QHBoxLayout(toolsRow);
    gridToolsLayout->setContentsMargins(0, 0, 0, 0);
    gridToolsLayout->setSpacing(10);
    
    gridToolsLayout->addSpacing(10);  // Spacer for target grid column
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
    targetModeTabs->addTab(new QWidget(), "Camera");

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
    algorithmCombo->addItems({"Gerchberg-Saxton", "Weighted GS"});

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
    algoForm->addRow("Iterations:", iterationsSpin);
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
    for (const QString &camName : camManager->getCameraNames()) {
        camSelect->addItem(camName);
    }

    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(saveMaskBtn, &QPushButton::clicked, this, &MainWindow::savePhaseMask);
    connect(loadTargetImageBtn, &QPushButton::clicked, this, &MainWindow::loadTargetImage);
    connect(clearTargetImageBtn, &QPushButton::clicked, this, &MainWindow::clearTargetImage);
    connect(patternPresetsWidget, &PatternPresetsWidget::patternGenerated, this, &MainWindow::onPatternGenerated);

    connect(algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAlgorithmSelectionChanged);
    connect(generateGsBtn, &QPushButton::clicked, this, &MainWindow::onGenerateGsMaskClicked);
    
    // Grid Widget connections
    connect(targetGridWidget, &TargetGridWidget::pointAdded, this, &MainWindow::onGridPointAdded);
    connect(targetGridWidget, &TargetGridWidget::pointMoved, this, &MainWindow::onGridPointMoved);
    connect(targetGridWidget, &TargetGridWidget::pointRemoved, this, &MainWindow::onGridPointRemoved);
    connect(targetGridWidget, &TargetGridWidget::pointSelected, this, &MainWindow::onGridPointSelected);
    // Manual tab button connections
    connect(addPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->addPoint(QPointF(0, 0));
        targetGridWidget->setFocus();
    });

    connect(clearAllPointsBtn, &QPushButton::clicked, this, [this]() {
        targetGridWidget->clearAllPoints();
        gridPointData.clear();
        trapTable->setRowCount(0);
        selectedPointId = -1;
    });

    connect(camSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), camManager, &CameraManager::changeCamera);
    connect(camStartBtn, &QPushButton::clicked, camManager, &CameraManager::startCamera);
    connect(camStopBtn, &QPushButton::clicked, camManager, &CameraManager::stopCamera);
    connect(captureImageBtn, &QPushButton::clicked, camManager, &CameraManager::captureImage);
    connect(recordVideoBtn, &QPushButton::toggled, camManager, &CameraManager::toggleRecording);
    
    connect(camManager, &CameraManager::frameReady, this, &MainWindow::updateCameraFeed);
    connect(camManager, &CameraManager::statusMessage, this, [this](const QString &msg){
        statusBar()->showMessage(msg);
    });
    connect(camManager, &CameraManager::recordingTimeUpdated, this, &MainWindow::onRecordingTimeUpdated);
    connect(camManager, &CameraManager::fpsUpdated, this, &MainWindow::onFPSUpdated);

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

    updateAlgorithmSettingsUi();
}

// ==========================================
// CORE SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    SettingsDialog dialog(slmWidth, slmHeight, slmPixelSize, cameraBackend,
                          camWidth, camHeight, camPixelSize,
                          laserWavelength, fourierFocalLength, slmOutputMode, autoRunGsEnabled, autoSendSlmEnabled, gsStartingPhaseMaskMode, this);

    if (dialog.exec() == QDialog::Accepted) {
        const int prevSlmWidth = slmWidth;
        const int prevSlmHeight = slmHeight;

        slmWidth = dialog.getWidth();
        slmHeight = dialog.getHeight();
        slmPixelSize = dialog.getPixelSize();

        bool backendChanged = (cameraBackend != dialog.getCameraBackend());
        cameraBackend = dialog.getCameraBackend();

        camWidth = dialog.getCamWidth();
        camHeight = dialog.getCamHeight();
        camPixelSize = dialog.getCamPixelSize();
        laserWavelength = dialog.getWavelength();
        fourierFocalLength = dialog.getFocalLength();

        QSettings settings(configPath(), QSettings::IniFormat);

        settings.setValue("Hardware/SLM_Width", slmWidth);
        settings.setValue("Hardware/SLM_Height", slmHeight);
        settings.setValue("Hardware/SLM_PixelSize", slmPixelSize);
        settings.setValue("Hardware/CameraBackend", cameraBackend);
        settings.setValue("Hardware/Cam_Width", camWidth);
        settings.setValue("Hardware/Cam_Height", camHeight);
        settings.setValue("Hardware/Cam_PixelSize", camPixelSize);
        settings.setValue("Optical/Wavelength", laserWavelength);
        slmOutputMode = dialog.getSlmOutputMode();
        autoRunGsEnabled = dialog.getAutoRunGsEnabled();
        autoSendSlmEnabled = dialog.getAutoSendSlmEnabled();
        gsStartingPhaseMaskMode = dialog.getStartingPhaseMaskMode();

        settings.setValue("Optical/FocalLength", fourierFocalLength);
        settings.setValue("Hardware/SLM_OutputMode", slmOutputMode);
        settings.setValue("Hardware/AutoRunGS", autoRunGsEnabled);
        settings.setValue("Hardware/AutoSendSLM", autoSendSlmEnabled);
        settings.setValue("Hardware/GS_StartingPhaseMask", gsStartingPhaseMaskMode);
        settings.sync();

        if (!autoRunGsEnabled && gsAutoRunTimer) {
            gsAutoRunTimer->stop();
        }

        resolutionLabel->setText(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));

        // Update grid resolution dynamically
        targetGridWidget->setGridResolution(camWidth, camHeight);
        targetGridWidget->centerView();
        targetGridWidget->clearAllPoints();
        if (patternPresetsWidget) {
            patternPresetsWidget->setCameraResolution(camWidth, camHeight);
        }

        if (!loadedTargetImageOriginal.isNull()) {
            loadedTargetImageGray = loadedTargetImageOriginal.convertToFormat(QImage::Format_Grayscale8).scaled(
                camWidth, camHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            if (targetImageInfoLabel) {
                targetImageInfoLabel->setText(QString("Loaded image mapped to camera resolution: %1 x %2 (8-bit grayscale)")
                    .arg(camWidth).arg(camHeight));
            }

            if (targetModeTabs && targetModeTabs->currentIndex() == kImageTabIndex) {
                targetGridWidget->setBackgroundImage(loadedTargetImageGray);
                targetGridWidget->setImageMode(true);
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

        if (backendChanged) {
            QMessageBox::information(this, "Restart Required",
                "You have changed the Camera Engine. Please restart the application for this to take effect.");
        } else {
            statusBar()->showMessage("Settings saved to: " + configPath(), 5000);
        }
    }
}

void MainWindow::openHologramGenerator() {
    HologramDialog dialog(slmWidth, slmHeight, this);
    connect(&dialog, &HologramDialog::maskReadyToLoad, this, &MainWindow::receiveHologram);
    connect(&dialog, &HologramDialog::sendToSLMRequested, this, &MainWindow::sendHologramToSLM);
    dialog.exec();
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
    return algorithmCombo && algorithmCombo->currentIndex() == 0;
}

QVector<float> MainWindow::defaultGsSourceAmplitude() const {
    const double defaultWaistPx = static_cast<double>(qMin(slmWidth, slmHeight)) / 6.0;
    return GSAlgorithm::buildGaussianSourceAmplitude(slmWidth, slmHeight, defaultWaistPx);
}

void MainWindow::updateAlgorithmSettingsUi() {
    const bool gsSelected = isGerchbergSaxtonSelected();

    if (relaxationLabel) {
        relaxationLabel->setVisible(!gsSelected);
    }
    if (relaxationSpin) {
        relaxationSpin->setVisible(!gsSelected);
    }

    if (generateGsBtn) {
        generateGsBtn->setText(gsSelected ? "Generate GS Mask" : "Generate (WGS unavailable)");
    }
}

void MainWindow::onAlgorithmSelectionChanged(int index) {
    Q_UNUSED(index);

    updateAlgorithmSettingsUi();

    if (!isGerchbergSaxtonSelected()) {
        if (gsAutoRunTimer) {
            gsAutoRunTimer->stop();
        }
        return;
    }

    scheduleGsAutoRun();
}

void MainWindow::onGenerateGsMaskClicked() {
    generateAlgorithmMask(true);
}

void MainWindow::scheduleGsAutoRun() {
    if (!autoRunGsEnabled || !isGerchbergSaxtonSelected() || !gsAutoRunTimer) {
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
    if (!autoRunGsEnabled || !isGerchbergSaxtonSelected()) {
        return;
    }

    if (gridPointData.isEmpty()) {
        return;
    }

    generateAlgorithmMask(false);
}

bool MainWindow::generateAlgorithmMask(bool showWarnings) {
    if (!isGerchbergSaxtonSelected()) {
        if (showWarnings) {
            QMessageBox::information(this, "Weighted GS", "Weighted GS is not implemented yet.");
        }
        return false;
    }

    if (gridPointData.isEmpty()) {
        if (showWarnings) {
            QMessageBox::warning(this, "GS Algorithm", "No target points found. Add points to the target grid.");
        }
        return false;
    }

    const int expectedSourceSize = slmWidth * slmHeight;
    const bool usingDefaultSource = sourceIntensityMap.size() != expectedSourceSize;
    const QVector<float> sourceAmplitude = usingDefaultSource ? defaultGsSourceAmplitude() : sourceIntensityMap;

    QVector<GSAlgorithm::GSTargetPoint> targets;
    targets.reserve(gridPointData.size());

    for (auto it = gridPointData.constBegin(); it != gridPointData.constEnd(); ++it) {
        GSAlgorithm::GSTargetPoint target;
        target.xCamPx = it.value().x();
        target.yCamPx = -it.value().y(); // Convert Qt grid Y to Cartesian (+Y up).
        targets.append(target);
    }

    GSAlgorithm::GSConfig config;
    config.slmWidth = slmWidth;
    config.slmHeight = slmHeight;
    config.slmPixelSizeUm = slmPixelSize;
    config.camWidth = camWidth;
    config.camHeight = camHeight;
    config.camPixelSizeUm = camPixelSize;
    config.wavelengthNm = laserWavelength;
    config.focalLengthMm = fourierFocalLength;
    config.iterations = iterationsSpin ? iterationsSpin->value() : 20;
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
    statusBar()->showMessage(QString("GS mask generated (%1 iterations, %2/%3 valid targets, %4).")
                                 .arg(config.iterations)
                                 .arg(result.usedTargetCount)
                                 .arg(result.requestedTargetCount)
                                 .arg(sourceMsg),
                             4000);

    autoSendToSlmIfEnabled();
    return true;
}

void MainWindow::onSendToSlmRequested() {
    if (isGerchbergSaxtonSelected() && !gridPointData.isEmpty()) {
        if (!generateAlgorithmMask(true)) {
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
    cameraFeedLabel->setPixmap(QPixmap::fromImage(img).scaled(
        cameraFeedLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
        targetGridWidget->setImageMode(true);
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
        targetGridWidget->setImageMode(true);
    }

    statusBar()->showMessage("Target image cleared.", 3000);
}

void MainWindow::onTabChanged(int index) {
    if (index == kImageTabIndex) {
        targetGridWidget->setImageMode(true);
        if (!loadedTargetImageGray.isNull()) {
            targetGridWidget->setBackgroundImage(loadedTargetImageGray);
        } else {
            targetGridWidget->clearBackgroundImage();
        }
    } else {
        targetGridWidget->setImageMode(false);
    }
}

void MainWindow::onRecordingTimeUpdated(const QString &timeString) {
    if (timeString == "00:00" && recordVideoBtn->isChecked()) {
        recordTimeLabel->setVisible(true);
    } else if (!recordVideoBtn->isChecked()) {
        recordTimeLabel->setVisible(false);
    }
    recordTimeLabel->setText(timeString);
}

void MainWindow::onFPSUpdated(const QString &fpsString) {
    fpsLabel->setText(fpsString);
}

void MainWindow::onGridPointAdded(int pointId, QPointF pixelCoords) {
    gridPointData[pointId] = pixelCoords;

    // Add row to trap table
    int row = trapTable->rowCount();
    trapTable->insertRow(row);

    trapTable->setItem(row, 0, new QTableWidgetItem(QString::number(pointId)));
    trapTable->setItem(row, 1, new QTableWidgetItem(QString::number((int)pixelCoords.x())));
    trapTable->setItem(row, 2, new QTableWidgetItem(QString::number((int)pixelCoords.y())));

    if (!suppressGridStatusMessages) {
        statusBar()->showMessage(QString("Point #%1 added at (%2, %3)").arg(pointId).arg((int)pixelCoords.x()).arg((int)pixelCoords.y()), 3000);
    }

    scheduleGsAutoRun();
}

void MainWindow::onGridPointMoved(int pointId, QPointF newPixelCoords) {
    if (gridPointData.contains(pointId)) {
        gridPointData[pointId] = newPixelCoords;

        // Update table row
        for (int row = 0; row < trapTable->rowCount(); ++row) {
            if (trapTable->item(row, 0)->text().toInt() == pointId) {
                trapTable->setItem(row, 1, new QTableWidgetItem(QString::number((int)newPixelCoords.x())));
                trapTable->setItem(row, 2, new QTableWidgetItem(QString::number((int)newPixelCoords.y())));
                break;
            }
        }

        statusBar()->showMessage(QString("Point #%1 moved to (%2, %3)").arg(pointId).arg((int)newPixelCoords.x()).arg((int)newPixelCoords.y()), 2000);
        scheduleGsAutoRun();
    }
}

void MainWindow::onGridPointRemoved(int pointId) {
    if (gridPointData.contains(pointId)) {
        gridPointData.remove(pointId);

        // Remove from table
        for (int row = 0; row < trapTable->rowCount(); ++row) {
            if (trapTable->item(row, 0)->text().toInt() == pointId) {
                trapTable->removeRow(row);
                break;
            }
        }

        statusBar()->showMessage(QString("Point #%1 removed").arg(pointId), 2000);
        scheduleGsAutoRun();
    }
}

void MainWindow::onGridPointSelected(int pointId) {
    selectedPointId = pointId;
    
    // Highlight the row in the table
    for (int row = 0; row < trapTable->rowCount(); ++row) {
        if (trapTable->item(row, 0)->text().toInt() == pointId) {
            trapTable->selectRow(row);
            break;
        }
    }
    
    statusBar()->showMessage(QString("Point #%1 selected (use arrow keys to move, Delete to remove)").arg(pointId), 3000);
}

void MainWindow::onPatternGenerated(const QVector<QPointF> &points, const QString &summary) {
    replaceGridWithPoints(points);
    statusBar()->showMessage(summary, 4000);
}

void MainWindow::replaceGridWithPoints(const QVector<QPointF> &points) {
    suppressGridStatusMessages = true;
    selectedPointId = -1;

    gridPointData.clear();
    trapTable->setRowCount(0);
    targetGridWidget->clearAllPoints();

    for (const QPointF &point : points) {
        targetGridWidget->addPoint(point);
    }

    suppressGridStatusMessages = false;
    scheduleGsAutoRun();
}

void MainWindow::toggleTheme() {
    isDarkMode = !isDarkMode;
    applyTheme(isDarkMode);

    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("UI/DarkMode", isDarkMode);
    settings.sync();
}

void MainWindow::applyTheme(bool dark) {
    const QString themeName = dark ? "theme.qss" : "light_theme.qss";
    const QStringList diskCandidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("resources/" + themeName),
        QDir(QCoreApplication::applicationDirPath()).filePath("../resources/" + themeName),
        QDir(QCoreApplication::applicationDirPath()).filePath("../../resources/" + themeName),
        QDir::current().filePath("resources/" + themeName)
    };
    QString styleSheet;
    for (const QString &candidate : diskCandidates) {
        QFile diskFile(candidate);
        if (diskFile.exists() && diskFile.open(QFile::ReadOnly | QFile::Text)) {
            styleSheet = QTextStream(&diskFile).readAll();
            diskFile.close();
            break;
        }
    }
    if (styleSheet.isEmpty()) {
        const QString qrcTheme = dark ? ":/theme.qss" : ":/light_theme.qss";
        QFile qrcFile(qrcTheme);
        if (qrcFile.open(QFile::ReadOnly | QFile::Text)) {
            styleSheet = QTextStream(&qrcFile).readAll();
            qrcFile.close();
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
        gridMaxMinBtn->setText("[Ã¢â€ â€œ]");  // Minimize symbol (down arrow)
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
        gridMaxMinBtn->setText("[Ã¢â€ â€˜]");  // Maximize symbol (up arrow)
        gridMaxMinBtn->setToolTip("Enlarge grid view");
    }
}

