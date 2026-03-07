#include "mainwindow.h"
#include "settingsdialog.h"
#include "hologramdialog.h"
#include "components/targetgridwidget.h"
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
    setWindowIcon(QIcon(":/favicon.ico"));
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

    // Load SLM DLL safely
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
    
    // --- Correction Map loaded via File Menu ---
    QAction *corrAction = fileMenu->addAction("Load SLM Correction Mask...");
    connect(corrAction, &QAction::triggered, this, &MainWindow::loadCorrectionFile);
    
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
    gridMaxMinBtn->setText("[↑]");  // Maximize symbol (up arrow)
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
    QLabel *manualLabel = new QLabel("Click on the grid to add target points.\nSelect a point and use arrow keys to move it.\nPress Delete to remove selected point.\nCoordinates centered at (0,0) - range from -X/2 to +X/2 and -Y/2 to +Y/2.");
    manualLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    manualLayout->addWidget(manualLabel);
    QHBoxLayout *manualBtns = new QHBoxLayout();
    manualBtns->addWidget(new QPushButton("Add Trap to SLM"));
    manualBtns->addWidget(new QPushButton("Clear All Points"));
    manualLayout->addLayout(manualBtns);
    manualLayout->addStretch();
    
    targetModeTabs->addTab(manualTab, "Manual");
    targetModeTabs->addTab(new QWidget(), "Pattern");
    targetModeTabs->addTab(new QWidget(), "Image");
    targetModeTabs->addTab(new QWidget(), "Camera");

    trapListContainer = new QWidget();
    QVBoxLayout *trapListLayout = new QVBoxLayout(trapListContainer);
    trapListLayout->setContentsMargins(0, 5, 0, 0);
    trapTable = new QTableWidget(0, 5);
    trapTable->setHorizontalHeaderLabels({"#", "X (centered)", "Y (centered)", "Intensity", "Size"});
    trapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // Create buttons but store as members without displaying in trap list
    addTrapBtn = new QPushButton("Add Trap");
    removeTrapBtn = new QPushButton("Remove Trap");
    clearTrapsBtn = new QPushButton("Clear All");
    // Buttons are hidden since they're available in the Manual tab
    addTrapBtn->hide();
    removeTrapBtn->hide();
    clearTrapsBtn->hide();
    
    trapListLayout->addWidget(new QLabel("Active Traps:"));
    trapListLayout->addWidget(trapTable);

    leftCol->addWidget(targetModeTabs);
    leftCol->addWidget(trapListContainer);

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
    QComboBox *algoCombo = new QComboBox();
    algoCombo->addItems({"Gerchberg-Saxton", "Weighted GS"});
    algoForm->addRow("Algorithm:", algoCombo);
    algoForm->addRow("Iterations:", new QSpinBox());
    algoForm->addRow("Relaxation:", new QDoubleSpinBox());
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
    
    // Grid Widget connections
    connect(targetGridWidget, &TargetGridWidget::pointAdded, this, &MainWindow::onGridPointAdded);
    connect(targetGridWidget, &TargetGridWidget::pointMoved, this, &MainWindow::onGridPointMoved);
    connect(targetGridWidget, &TargetGridWidget::pointRemoved, this, &MainWindow::onGridPointRemoved);
    connect(targetGridWidget, &TargetGridWidget::pointSelected, this, &MainWindow::onGridPointSelected);
    
    // Trap table button connections
    connect(addTrapBtn, &QPushButton::clicked, this, [this]() {
        // Add current grid points as traps
        auto points = targetGridWidget->getAllPoints();
        for (const auto &point : points) {
            if (!gridPointData.contains(point.first)) {
                gridPointData[point.first] = point.second;
            }
        }
    });
    
    connect(removeTrapBtn, &QPushButton::clicked, this, [this]() {
        if (trapTable->currentRow() >= 0) {
            int row = trapTable->currentRow();
            bool ok;
            int pointId = trapTable->item(row, 0)->text().toInt(&ok);
            if (ok && gridPointData.contains(pointId)) {
                gridPointData.remove(pointId);
                targetGridWidget->removePoint(pointId);
            }
        }
    });
    
    connect(clearTrapsBtn, &QPushButton::clicked, this, [this]() {
        gridPointData.clear();
        targetGridWidget->clearAllPoints();
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
    connect(sendSlmBtn, &QPushButton::clicked, this, &MainWindow::sendToSLM);
    connect(clearSlmBtn, &QPushButton::clicked, this, &MainWindow::clearSLM);
    connect(previewCorrectionCb, &QCheckBox::toggled, this, &MainWindow::updatePhasePreview);
}

// ==========================================
// CORE SLOTS
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
        
        // Update grid resolution dynamically
        targetGridWidget->setGridResolution(camWidth, camHeight);
        targetGridWidget->centerView();
        targetGridWidget->clearAllPoints();
        
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
    connect(&dialog, &HologramDialog::maskReadyToLoad, this, &MainWindow::receiveHologram);
    connect(&dialog, &HologramDialog::sendToSLMRequested, this, &MainWindow::sendHologramToSLM);
    dialog.exec();
}

void MainWindow::savePhaseMask() {
    // construct the image we actually want to write rather than relying on the scaled
    // widget pixmap (which may be resized to fit the label)
    QImage saveImg = composeFullResolutionMask();
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

void MainWindow::onGridPointAdded(int pointId, QPointF pixelCoords) {
    gridPointData[pointId] = pixelCoords;
    
    // Add row to trap table
    int row = trapTable->rowCount();
    trapTable->insertRow(row);
    
    trapTable->setItem(row, 0, new QTableWidgetItem(QString::number(pointId)));
    trapTable->setItem(row, 1, new QTableWidgetItem(QString::number((int)pixelCoords.x())));
    trapTable->setItem(row, 2, new QTableWidgetItem(QString::number((int)pixelCoords.y())));
    trapTable->setItem(row, 3, new QTableWidgetItem("1.0"));   // Default intensity
    trapTable->setItem(row, 4, new QTableWidgetItem("5.0"));   // Default size
    
    statusBar()->showMessage(QString("Point #%1 added at (%2, %3)").arg(pointId).arg((int)pixelCoords.x()).arg((int)pixelCoords.y()), 3000);
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

// ==========================================
// DYNAMIC SLM LOGIC & PREVIEW
// ==========================================

// Combine the currently loaded hologram and correction into a single full-resolution
// image.  Performs identical modulo-256 addition used in sendToSLM()/updatePhasePreview.
// Returns a null QImage if nothing is available.
QImage MainWindow::composeFullResolutionMask() const {
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

    if (!correctionMask.isNull()) {
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
        previewCorrectionCb->setChecked(true);  // Auto-enable preview display
        
        statusBar()->showMessage("Correction Mask loaded. Automatically sending to SLM...", 5000);
        updatePhasePreview(); 
        
        // --- FIX: ALWAYS force a hardware update the moment it is loaded ---
        sendToSLM(); 
    }
}

void MainWindow::sendToSLM() {
    // If absolutely nothing is loaded, abort.
    if (currentMask.isNull() && correctionMask.isNull()) {
        QMessageBox::warning(this, "Error", "No mask or correction loaded to send!");
        return;
    }

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

    QImage finalMask = composeFullResolutionMask();
    if (finalMask.isNull()) {
        // This should not happen due to earlier checks, but be defensive
        QMessageBox::warning(this, "Error", "Unable to compose final mask for SLM.");
        return;
    }

    // Push to Hardware
    winSettings(2, slmWindowID, 0, 0);
    
    int width = finalMask.width();
    int height = finalMask.height();
    uint8_t* rawData = finalMask.bits();

    winArrayToDisplay(rawData, width, height, slmWindowID, width * height);

    // Update Status Bar intelligently
    if (!correctionMask.isNull() && currentMask.isNull()) {
        statusBar()->showMessage("Background Correction Mask sent to SLM.", 5000);
    } else if (!correctionMask.isNull()) {
        statusBar()->showMessage("Phase Mask + Correction sent to SLM hardware.", 5000);
    } else {
        statusBar()->showMessage("Phase Mask sent to SLM hardware.", 5000);
    }
}

void MainWindow::clearSLM() {
    currentMask = QImage(); // Wipe target mask from memory
    updatePhasePreview();
    
    if (!slmLibrary.isLoaded()) return;

    if (!correctionMask.isNull()) {
        // --- FIX: Rely on the smarter sendToSLM() to handle the background mask ---
        sendToSLM(); 
        phaseMaskLabel->setText("Cleared (Correction Active)");
        statusBar()->showMessage("Target cleared. SLM is maintaining hardware flatness correction.", 5000);
    } else {
        // No correction loaded at all, completely terminate display
        auto winTerm = (Window_Term_Func)slmLibrary.resolve("Window_Term");
        if (winTerm) winTerm(slmWindowID); 
        
        phaseMaskLabel->clear();
        phaseMaskLabel->setText("SLM Offline");
        statusBar()->showMessage("SLM display completely terminated.", 3000);
    }
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
        gridMaxMinBtn->setText("[↓]");  // Minimize symbol (down arrow)
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
        gridMaxMinBtn->setText("[↑]");  // Maximize symbol (up arrow)
        gridMaxMinBtn->setToolTip("Enlarge grid view");
    }
}