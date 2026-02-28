#include "mainwindow.h"
#include "settingsdialog.h"
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
#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setMinimumSize(1200, 800);

    setupUI();
    applyDarkTheme();
    
    // Initialize backend logic
    camManager = new CameraManager(cameraFeedWidget, this);
    setupConnections();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    createMenus();

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QGridLayout *mainLayout = new QGridLayout(centralWidget);
    mainLayout->setSpacing(10); 

    // Build the Grid using our helper functions
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
    menuBar()->addMenu("&Tools");
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

    cameraFeedWidget = new QVideoWidget();
    cameraFeedWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(cameraFeedWidget, 1, 2);

    // Row 2: Toolbars
    QHBoxLayout *targetTools = new QHBoxLayout();
    targetTools->addSpacerItem(new QSpacerItem(10, 28, QSizePolicy::Minimum, QSizePolicy::Fixed));
    layout->addLayout(targetTools, 2, 0);

    QHBoxLayout *phaseTools = new QHBoxLayout();
    resolutionLabel = new QLabel(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
    phaseTools->addWidget(resolutionLabel);
    phaseTools->addStretch();
    phaseTools->addWidget(new QPushButton("Save Mask"));
    layout->addLayout(phaseTools, 2, 1);

    QHBoxLayout *camTools = new QHBoxLayout();
    camTools->addWidget(new QLabel("FPS: 30  Exposure: 10ms"));
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
    camSelect = new QComboBox(); // Will be populated in setupConnections
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
    // UI Connections
    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    onTabChanged(0);

    // Camera Connections
    for (const auto &cam : camManager->getAvailableCameras()) {
        camSelect->addItem(cam.description());
    }
    
    connect(camSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), camManager, &CameraManager::changeCamera);
    connect(camStartBtn, &QPushButton::clicked, camManager, &CameraManager::startCamera);
    connect(camStopBtn, &QPushButton::clicked, camManager, &CameraManager::stopCamera);
    connect(captureImageBtn, &QPushButton::clicked, camManager, &CameraManager::captureImage);
    connect(recordVideoBtn, &QPushButton::toggled, camManager, &CameraManager::toggleRecording);
    
    // Connect backend signals back to UI
    connect(camManager, &CameraManager::statusMessage, this, [this](const QString &msg){
        statusBar()->showMessage(msg);
    });
    connect(camManager, &CameraManager::recordingTimeUpdated, this, &MainWindow::onRecordingTimeUpdated);

    if (camSelect->count() > 0) camManager->changeCamera(0);
}

// ==========================================
// SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    SettingsDialog dialog(slmWidth, slmHeight, slmPixelSize, this);
    if (dialog.exec() == QDialog::Accepted) {
        slmWidth = dialog.getWidth();
        slmHeight = dialog.getHeight();
        slmPixelSize = dialog.getPixelSize();
        resolutionLabel->setText(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
        statusBar()->showMessage("Hardware settings updated.", 3000);
    }
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

void MainWindow::applyDarkTheme() {
    QFile file(":/theme.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        this->setStyleSheet(QTextStream(&file).readAll());
        file.close();
    }
}