#include "mainwindow.h"
#include "settingsdialog.h"
#include "hologramdialog.h"
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setMinimumSize(800, 600);

    // Load saved hardware settings from the INI file
    QString configPath = QCoreApplication::applicationDirPath() + "/hardware_config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    
    slmWidth = settings.value("Hardware/SLM_Width", 1920).toInt();
    slmHeight = settings.value("Hardware/SLM_Height", 1080).toInt();
    slmPixelSize = settings.value("Hardware/SLM_PixelSize", 8.0).toDouble();
    cameraBackend = settings.value("Hardware/CameraBackend", 0).toInt();

    setupUI();
    applyDarkTheme();
    
    // Initialize backend logic with the chosen camera engine
    camManager = new CameraManager(cameraBackend, this);
    setupConnections();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    createMenus();

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QGridLayout *mainLayout = new QGridLayout(centralWidget);
    mainLayout->setSpacing(10); 

    // Build the Grid
    createMonitors(mainLayout);
    createControls(mainLayout);

    // Grid Layout constraints
    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(1, 1);
    mainLayout->setColumnStretch(2, 1);
    mainLayout->setRowStretch(0, 0); 
    mainLayout->setRowStretch(1, 3); 
    mainLayout->setRowStretch(2, 0); 
    mainLayout->setRowStretch(3, 2); 

    // Status Bar
    statusBar()->addWidget(new QLabel(" SLM: Connected | Algorithm: GS "));
}

void MainWindow::createMenus() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QAction *settingsAction = fileMenu->addAction("Hardware Settings...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);
    
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *holoAction = toolsMenu->addAction("Create Hologram...");
    connect(holoAction, &QAction::triggered, this, &MainWindow::openHologramGenerator);
    
    menuBar()->addMenu("&Help");
}

void MainWindow::createMonitors(QGridLayout *layout) {
    // Row 0: Titles
    QLabel *t1 = new QLabel("Target Pattern"); t1->setAlignment(Qt::AlignCenter);
    QLabel *t2 = new QLabel("Phase Mask"); t2->setAlignment(Qt::AlignCenter);
    QLabel *t3 = new QLabel("Live Camera Feed"); t3->setAlignment(Qt::AlignCenter);
    layout->addWidget(t1, 0, 0); layout->addWidget(t2, 0, 1); layout->addWidget(t3, 0, 2);

    // Row 1: Monitors
    targetView = new QGraphicsView();
    targetScene = new QGraphicsScene(this);
    targetView->setScene(targetScene);
    targetView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(targetView, 1, 0);

    phaseMaskLabel = new QLabel();
    phaseMaskLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    phaseMaskLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    layout->addWidget(phaseMaskLabel, 1, 1);

    // Changed to QLabel to support OpenCV's QImage output
    cameraFeedLabel = new QLabel("Camera Feed (Offline)");
    cameraFeedLabel->setAlignment(Qt::AlignCenter);
    cameraFeedLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    cameraFeedLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(cameraFeedLabel, 1, 2);

    // Row 2: Toolbars
    QHBoxLayout *targetTools = new QHBoxLayout();
    targetTools->addSpacerItem(new QSpacerItem(10, 28, QSizePolicy::Minimum, QSizePolicy::Fixed));
    layout->addLayout(targetTools, 2, 0);

    QHBoxLayout *phaseTools = new QHBoxLayout();
    resolutionLabel = new QLabel(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
    phaseTools->addWidget(resolutionLabel);
    phaseTools->addStretch();
    saveMaskBtn = new QPushButton("Save Mask");
    phaseTools->addWidget(saveMaskBtn);
    layout->addLayout(phaseTools, 2, 1);

    QHBoxLayout *camTools = new QHBoxLayout();
    fpsLabel = new QLabel("FPS: 0");
    camTools->addWidget(fpsLabel);
    camTools->addStretch();
    camTools->addWidget(new QCheckBox("Overlay Target"));
    layout->addLayout(camTools, 2, 2);
}

void MainWindow::createControls(QGridLayout *layout) {
    // 3, 0: Target Tabs & Trap List
    QVBoxLayout *leftCol = new QVBoxLayout();
    targetModeTabs = new QTabWidget();
    
    QWidget *manualTab = new QWidget();
    QHBoxLayout *manualLayout = new QHBoxLayout(manualTab);
    manualLayout->addWidget(new QPushButton("Add"));
    manualLayout->addWidget(new QPushButton("Move"));
    manualLayout->addWidget(new QPushButton("Delete"));
    manualLayout->addWidget(new QPushButton("Clear All"));
    
    targetModeTabs->addTab(manualTab, "Manual");
    targetModeTabs->addTab(new QWidget(), "Pattern");
    targetModeTabs->addTab(new QWidget(), "Image");
    targetModeTabs->addTab(new QWidget(), "Camera");

    trapListContainer = new QWidget();
    QVBoxLayout *trapListLayout = new QVBoxLayout(trapListContainer);
    trapListLayout->setContentsMargins(0, 5, 0, 0);
    trapTable = new QTableWidget(0, 5);
    trapTable->setHorizontalHeaderLabels({"#", "X (µm)", "Y (µm)", "Intensity", "Size"});
    trapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QHBoxLayout *tableBtns = new QHBoxLayout();
    tableBtns->addWidget(new QPushButton("Add Trap"));
    tableBtns->addWidget(new QPushButton("Remove Trap"));
    trapListLayout->addWidget(new QLabel("Active Traps:"));
    trapListLayout->addWidget(trapTable);
    trapListLayout->addLayout(tableBtns);

    leftCol->addWidget(targetModeTabs);
    leftCol->addWidget(trapListContainer);
    layout->addLayout(leftCol, 3, 0);

    // 3, 1: Algorithm Settings
    QVBoxLayout *midCol = new QVBoxLayout();
    QGroupBox *algoGroup = new QGroupBox("Algorithm Settings");
    QFormLayout *algoForm = new QFormLayout();
    QComboBox *algoCombo = new QComboBox();
    algoCombo->addItems({"Gerchberg-Saxton", "Weighted GS"});
    algoForm->addRow("Algorithm:", algoCombo);
    algoForm->addRow("Iterations:", new QSpinBox());
    algoForm->addRow("Relaxation:", new QDoubleSpinBox());
    algoGroup->setLayout(algoForm);
    midCol->addWidget(algoGroup);
    midCol->addStretch();
    layout->addLayout(midCol, 3, 1);

    // 3, 2: Camera & SLM Controls
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
    slmLayout->addWidget(new QPushButton("Load Existing Phase Pattern"));
    slmLayout->addWidget(new QPushButton("Load Correction File"));
    slmLayout->addWidget(new QPushButton("Clear SLM Pattern"));
    slmGroup->setLayout(slmLayout);

    rightCol->addWidget(camGroup);
    rightCol->addWidget(slmGroup);
    rightCol->addStretch();
    layout->addLayout(rightCol, 3, 2);
}

void MainWindow::setupConnections() {
    // Populate camera dropdown
    for (const QString &camName : camManager->getCameraNames()) {
        camSelect->addItem(camName);
    }

    // UI Connections
    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(saveMaskBtn, &QPushButton::clicked, this, &MainWindow::savePhaseMask);

    // Camera Connections
    connect(camSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), camManager, &CameraManager::changeCamera);
    connect(camStartBtn, &QPushButton::clicked, camManager, &CameraManager::startCamera);
    connect(camStopBtn, &QPushButton::clicked, camManager, &CameraManager::stopCamera);
    connect(captureImageBtn, &QPushButton::clicked, camManager, &CameraManager::captureImage);
    connect(recordVideoBtn, &QPushButton::toggled, camManager, &CameraManager::toggleRecording);
    
    // Connect backend signals back to UI
    connect(camManager, &CameraManager::frameReady, this, &MainWindow::updateCameraFeed);
    connect(camManager, &CameraManager::statusMessage, this, [this](const QString &msg){
        statusBar()->showMessage(msg);
    });
    connect(camManager, &CameraManager::recordingTimeUpdated, this, &MainWindow::onRecordingTimeUpdated);
    connect(camManager, &CameraManager::fpsUpdated, this, &MainWindow::onFPSUpdated);

    if (camSelect->count() > 0) camManager->changeCamera(0);
}

// ==========================================
// SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    SettingsDialog dialog(slmWidth, slmHeight, slmPixelSize, cameraBackend, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        slmWidth = dialog.getWidth();
        slmHeight = dialog.getHeight();
        slmPixelSize = dialog.getPixelSize();
        
        bool backendChanged = (cameraBackend != dialog.getCameraBackend());
        cameraBackend = dialog.getCameraBackend(); 
        
        QString configPath = QCoreApplication::applicationDirPath() + "/hardware_config.ini";
        QSettings settings(configPath, QSettings::IniFormat);
        
        settings.setValue("Hardware/SLM_Width", slmWidth);
        settings.setValue("Hardware/SLM_Height", slmHeight);
        settings.setValue("Hardware/SLM_PixelSize", slmPixelSize);
        settings.setValue("Hardware/CameraBackend", cameraBackend);
        settings.sync();
        
        resolutionLabel->setText(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
        
        if (backendChanged) {
            QMessageBox::information(this, "Restart Required", 
                "You have changed the Camera Engine. Please restart the application for this to take effect.");
        } else {
            statusBar()->showMessage("Settings saved to: " + configPath, 5000);
        }
    }
}

void MainWindow::openHologramGenerator() {
    HologramDialog dialog(slmWidth, slmHeight, this);
    dialog.exec();
}

void MainWindow::savePhaseMask() {
    if (phaseMaskLabel->pixmap().isNull()) {
        QMessageBox::warning(this, "No Mask Found", "There is no phase mask currently generated to save.");
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultFileName = QDir(defaultPath).filePath("PhaseMask_" + timestamp + ".png");

    QString fileName = QFileDialog::getSaveFileName(this, 
                                                    "Save Phase Mask", 
                                                    defaultFileName, 
                                                    "Images (*.png *.bmp *.jpg)");

    if (!fileName.isEmpty()) {
        if (phaseMaskLabel->pixmap().save(fileName)) {
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

void MainWindow::onTabChanged(int index) {
    trapListContainer->setVisible(index == 0 || index == 3);
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

void MainWindow::applyDarkTheme() {
    QFile file(":/theme.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        this->setStyleSheet(QTextStream(&file).readAll());
        file.close();
    }
}