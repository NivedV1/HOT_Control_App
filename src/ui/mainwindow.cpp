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
#include <QApplication>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setMinimumSize(800, 600);

    QString configPath = QCoreApplication::applicationDirPath() + "/hardware_config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    
    slmWidth = settings.value("Hardware/SLM_Width", 1920).toInt();
    slmHeight = settings.value("Hardware/SLM_Height", 1080).toInt();
    slmPixelSize = settings.value("Hardware/SLM_PixelSize", 8.0).toDouble();
    cameraBackend = settings.value("Hardware/CameraBackend", 0).toInt();
    
    camWidth = settings.value("Hardware/Cam_Width", 1920).toInt();
    camHeight = settings.value("Hardware/Cam_Height", 1080).toInt();
    camPixelSize = settings.value("Hardware/Cam_PixelSize", 5.0).toDouble();
    laserWavelength = settings.value("Optical/Wavelength", 1064.0).toDouble();
    fourierFocalLength = settings.value("Optical/FocalLength", 100.0).toDouble();

    isDarkMode = settings.value("UI/DarkMode", true).toBool();

    setupUI();
    applyTheme(isDarkMode); 
    
    camManager = new CameraManager(cameraBackend, this);
    setupConnections();

    // --- NEW: Load the DLL dynamically at startup ---
    slmLibrary.setFileName(QCoreApplication::applicationDirPath() + "/Image_Control.dll");
    if (!slmLibrary.load()) {
        qWarning() << "Could not load Image_Control.dll! Ensure it is in the build folder.";
    }
}

MainWindow::~MainWindow() {
    if (camManager) {
        camManager->stopCamera();
        delete camManager;
        camManager = nullptr;
    }
    
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
    QGridLayout *mainLayout = new QGridLayout(centralWidget);
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
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);
    
    QMenu *viewMenu = menuBar()->addMenu("&View");
    QAction *themeAction = viewMenu->addAction("Toggle Light/Dark Theme");
    connect(themeAction, &QAction::triggered, this, &MainWindow::toggleTheme);
    
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *holoAction = toolsMenu->addAction("Create Hologram...");
    connect(holoAction, &QAction::triggered, this, &MainWindow::openHologramGenerator);
    
    menuBar()->addMenu("&Help");
}

void MainWindow::createMonitors(QGridLayout *layout) {
    QLabel *t1 = new QLabel("Target Pattern"); t1->setAlignment(Qt::AlignCenter);
    QLabel *t2 = new QLabel("Phase Mask"); t2->setAlignment(Qt::AlignCenter);
    QLabel *t3 = new QLabel("Live Camera Feed"); t3->setAlignment(Qt::AlignCenter);
    layout->addWidget(t1, 0, 0); layout->addWidget(t2, 0, 1); layout->addWidget(t3, 0, 2);

    targetView = new QGraphicsView();
    targetScene = new QGraphicsScene(this);
    targetView->setScene(targetScene);
    targetView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); 
    targetView->setMinimumSize(300, 200); 
    layout->addWidget(targetView, 1, 0);

    phaseMaskLabel = new QLabel();
    phaseMaskLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); 
    phaseMaskLabel->setMinimumSize(300, 200); 
    phaseMaskLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    layout->addWidget(phaseMaskLabel, 1, 1);

    cameraFeedLabel = new QLabel("Camera Feed (Offline)");
    cameraFeedLabel->setAlignment(Qt::AlignCenter);
    cameraFeedLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    cameraFeedLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); 
    cameraFeedLabel->setMinimumSize(300, 200); 
    layout->addWidget(cameraFeedLabel, 1, 2);

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
    layout->addLayout(rightCol, 3, 2);
}

void MainWindow::setupConnections() {
    for (const QString &camName : camManager->getCameraNames()) {
        camSelect->addItem(camName);
    }

    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(saveMaskBtn, &QPushButton::clicked, this, &MainWindow::savePhaseMask);

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

    // SLM loading and sending
    connect(loadPhaseBtn, &QPushButton::clicked, this, &MainWindow::loadPhasePattern);
    connect(sendSlmBtn, &QPushButton::clicked, this, &MainWindow::sendToSLM);
    connect(clearSlmBtn, &QPushButton::clicked, this, &MainWindow::clearSLM);
}

// ==========================================
// SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    SettingsDialog dialog(slmWidth, slmHeight, slmPixelSize, cameraBackend, 
                          camWidth, camHeight, camPixelSize, 
                          laserWavelength, fourierFocalLength, this);
    
    if (dialog.exec() == QDialog::Accepted) {
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
        
        QString configPath = QCoreApplication::applicationDirPath() + "/hardware_config.ini";
        QSettings settings(configPath, QSettings::IniFormat);
        
        settings.setValue("Hardware/SLM_Width", slmWidth);
        settings.setValue("Hardware/SLM_Height", slmHeight);
        settings.setValue("Hardware/SLM_PixelSize", slmPixelSize);
        settings.setValue("Hardware/CameraBackend", cameraBackend);
        
        settings.setValue("Hardware/Cam_Width", camWidth);
        settings.setValue("Hardware/Cam_Height", camHeight);
        settings.setValue("Hardware/Cam_PixelSize", camPixelSize);
        settings.setValue("Optical/Wavelength", laserWavelength);
        settings.setValue("Optical/FocalLength", fourierFocalLength);
        
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
    
    // --- NEW: Connect the Dialog's signal to the MainWindow's slot ---
    connect(&dialog, &HologramDialog::maskReadyToLoad, this, &MainWindow::receiveHologram);
    
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

void MainWindow::toggleTheme() {
    isDarkMode = !isDarkMode;
    applyTheme(isDarkMode);
    
    QString configPath = QCoreApplication::applicationDirPath() + "/hardware_config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("UI/DarkMode", isDarkMode);
    settings.sync();
}

void MainWindow::applyTheme(bool dark) {
    QString themeFile = dark ? ":/theme.qss" : ":/light_theme.qss";
    
    QFile file(themeFile);
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(QTextStream(&file).readAll());
        file.close();
    } else {
        qDebug() << "Failed to load theme file:" << themeFile;
    }
}

// ==========================================
// NEW: DYNAMIC SLM LOGIC
// ==========================================

void MainWindow::loadPhasePattern() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Phase Mask", "", "Images (*.png *.bmp *.jpg)");
    if (!fileName.isEmpty()) {
        currentMask = QImage(fileName).convertToFormat(QImage::Format_Grayscale8);
        
        phaseMaskLabel->setPixmap(QPixmap::fromImage(currentMask).scaled(
            phaseMaskLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            
        statusBar()->showMessage("Mask loaded: " + fileName, 3000);
    }
}

void MainWindow::sendToSLM() {
    if (currentMask.isNull()) {
        QMessageBox::warning(this, "Error", "No mask loaded to send!");
        return;
    }

    // Guard to ensure DLL loaded properly
    if (!slmLibrary.isLoaded()) {
        QMessageBox::critical(this, "DLL Error", "Image_Control.dll is not loaded.");
        return;
    }

    // 1. Resolve the functions from the DLL dynamically
    auto winSettings = (Window_Settings_Func)slmLibrary.resolve("Window_Settings");
    auto winArrayToDisplay = (Window_Array_to_Display_Func)slmLibrary.resolve("Window_Array_to_Display");

    if (!winSettings || !winArrayToDisplay) {
        QMessageBox::critical(this, "DLL Error", "Could not find SLM functions inside the DLL.");
        return;
    }

    // 2. Open the SLM Window on monitor index 2 
    winSettings(2, slmWindowID, 0, 0); 

    // 3. Prepare the 1D Array for the SDK 
    int width = currentMask.width();
    int height = currentMask.height();
    uint8_t* rawData = currentMask.bits(); 

    // 4. Push data to SLM 
    winArrayToDisplay(rawData, width, height, slmWindowID, width * height);

    statusBar()->showMessage("Phase Mask sent to SLM hardware.", 5000);
}

void MainWindow::clearSLM() {
    if (slmLibrary.isLoaded()) {
        auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
        if (winTerm) {
            winTerm(slmWindowID); 
        }
    }
    phaseMaskLabel->clear();
    phaseMaskLabel->setText("SLM Offline");
    statusBar()->showMessage("SLM display terminated.", 3000);
}

// --- NEW: Function to receive the image and prepare it for the SLM ---
void MainWindow::receiveHologram(const QImage &mask) {
    // 1. Store it in the master variable used by the SendToSLM function
    currentMask = mask;
    
    // 2. Display it on the Phase Mask monitor
    phaseMaskLabel->setPixmap(QPixmap::fromImage(currentMask).scaled(
        phaseMaskLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        
    // 3. Update the status bar
    statusBar()->showMessage("Generated Hologram loaded successfully. Ready to send to SLM.", 5000);
}