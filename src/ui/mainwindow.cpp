#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSlider>
#include <QFile>
#include <QTextStream>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QDialog>
#include <QDialogButtonBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setMinimumSize(1200, 800);

    setupUI();
    applyDarkTheme();
}

MainWindow::~MainWindow() {
    if (camera) {
        camera->stop();
        delete camera;
    }
}

void MainWindow::setupUI() {
    // Top Menu Bar
    QMenu *fileMenu = menuBar()->addMenu("&File");
    
    QAction *settingsAction = fileMenu->addAction("Hardware Settings...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);
    
    menuBar()->addMenu("&Tools");
    menuBar()->addMenu("&Help");

    // Main Central Widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // THE MASTER LAYOUT: Row-based Grid Alignment
    QGridLayout *mainLayout = new QGridLayout(centralWidget);
    mainLayout->setSpacing(10); 

    // ==========================================
    // ROW 0: Titles
    // ==========================================
    QLabel *targetTitle = new QLabel("Target Pattern");
    targetTitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(targetTitle, 0, 0);

    QLabel *phaseTitle = new QLabel("Phase Mask");
    phaseTitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(phaseTitle, 0, 1);

    QLabel *cameraTitle = new QLabel("Live Camera Feed");
    cameraTitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(cameraTitle, 0, 2);

    // ==========================================
    // ROW 1: The Three Monitors
    // ==========================================
    targetView = new QGraphicsView();
    targetScene = new QGraphicsScene(this);
    targetView->setScene(targetScene);
    targetView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(targetView, 1, 0);

    phaseMaskLabel = new QLabel();
    phaseMaskLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    phaseMaskLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    mainLayout->addWidget(phaseMaskLabel, 1, 1);

    cameraFeedWidget = new QVideoWidget();
    cameraFeedWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(cameraFeedWidget, 1, 2);

    // ==========================================
    // ROW 2: Monitor Toolbars
    // ==========================================
    QHBoxLayout *targetTools = new QHBoxLayout();
    targetTools->addSpacerItem(new QSpacerItem(10, 28, QSizePolicy::Minimum, QSizePolicy::Fixed));
    mainLayout->addLayout(targetTools, 2, 0);

    QHBoxLayout *phaseTools = new QHBoxLayout();
    resolutionLabel = new QLabel(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
    phaseTools->addWidget(resolutionLabel);
    phaseTools->addStretch();
    phaseTools->addWidget(new QPushButton("Save Mask"));
    mainLayout->addLayout(phaseTools, 2, 1);

    QHBoxLayout *camTools = new QHBoxLayout();
    camTools->addWidget(new QLabel("FPS: 30  Exposure: 10ms"));
    camTools->addStretch();
    camTools->addWidget(new QCheckBox("Overlay Target"));
    mainLayout->addLayout(camTools, 2, 2);

    // ==========================================
    // ROW 3: The Controls
    // ==========================================

    // 3, 0: Target Tabs & Trap List
    QVBoxLayout *targetControlsLayout = new QVBoxLayout();
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

    targetControlsLayout->addWidget(targetModeTabs);
    targetControlsLayout->addWidget(trapListContainer);
    mainLayout->addLayout(targetControlsLayout, 3, 0);

    // 3, 1: Algorithm Settings
    QVBoxLayout *middleControlsLayout = new QVBoxLayout();
    QGroupBox *algoGroup = new QGroupBox("Algorithm Settings");
    QFormLayout *algoForm = new QFormLayout();
    QComboBox *algoCombo = new QComboBox();
    algoCombo->addItems({"Gerchberg-Saxton", "Weighted GS"});
    algoForm->addRow("Algorithm:", algoCombo);
    algoForm->addRow("Iterations:", new QSpinBox());
    algoForm->addRow("Relaxation:", new QDoubleSpinBox());
    algoGroup->setLayout(algoForm);

    middleControlsLayout->addWidget(algoGroup);
    middleControlsLayout->addStretch();
    mainLayout->addLayout(middleControlsLayout, 3, 1);

    // 3, 2: Camera & SLM Controls
    QVBoxLayout *rightControlsLayout = new QVBoxLayout();
    
    QGroupBox *camGroup = new QGroupBox("Camera Control");
    QFormLayout *camForm = new QFormLayout();
    
    camSelect = new QComboBox();
    availableCameras = QMediaDevices::videoInputs();
    for (const QCameraDevice &camDevice : availableCameras) {
        camSelect->addItem(camDevice.description());
    }
    camForm->addRow("Camera:", camSelect);
    
    // Capture Options
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
    
    QHBoxLayout *camButtons = new QHBoxLayout();
    camStartBtn = new QPushButton("Start Feed");
    camStopBtn = new QPushButton("Stop Feed");
    camButtons->addWidget(camStartBtn);
    camButtons->addWidget(camStopBtn);
    camForm->addRow(camButtons);
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

    rightControlsLayout->addWidget(camGroup);
    rightControlsLayout->addWidget(slmGroup);
    rightControlsLayout->addStretch();
    mainLayout->addLayout(rightControlsLayout, 3, 2);

    // ==========================================
    // GRID PROPORTIONS & CONNECTIONS
    // ==========================================
    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(1, 1);
    mainLayout->setColumnStretch(2, 1);

    mainLayout->setRowStretch(0, 0); 
    mainLayout->setRowStretch(1, 3); 
    mainLayout->setRowStretch(2, 0); 
    mainLayout->setRowStretch(3, 2); 

    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    onTabChanged(0);

    // Camera & Recording Connections
    captureSession = new QMediaCaptureSession(this);
    captureSession->setVideoOutput(cameraFeedWidget);

    imageCapture = new QImageCapture(this);
    captureSession->setImageCapture(imageCapture);

    mediaRecorder = new QMediaRecorder(this);
    captureSession->setRecorder(mediaRecorder);

    connect(camStartBtn, &QPushButton::clicked, this, &MainWindow::startCamera);
    connect(camStopBtn, &QPushButton::clicked, this, &MainWindow::stopCamera);
    connect(camSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::changeCamera);
    
    connect(captureImageBtn, &QPushButton::clicked, this, &MainWindow::captureImage);
    connect(recordVideoBtn, &QPushButton::toggled, this, &MainWindow::toggleRecording);
    connect(mediaRecorder, &QMediaRecorder::durationChanged, this, &MainWindow::updateRecordTime);

    if (!availableCameras.isEmpty()) {
        changeCamera(0);
    }

    QStatusBar *statusBar = this->statusBar();
    statusBar->addWidget(new QLabel(" SLM: Connected | Camera: Active | Algorithm: GS "));
}

// ==========================================
// LOGIC SLOTS
// ==========================================

void MainWindow::openSettingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Hardware Settings");
    dialog.setMinimumWidth(300);
    
    QFormLayout form(&dialog);
    
    QSpinBox *widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(100, 8000);
    widthSpin->setValue(slmWidth);
    
    QSpinBox *heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(100, 8000);
    heightSpin->setValue(slmHeight);
    
    QDoubleSpinBox *pixelSpin = new QDoubleSpinBox(&dialog);
    pixelSpin->setRange(0.1, 100.0);
    pixelSpin->setDecimals(2);
    pixelSpin->setSuffix(" µm");
    pixelSpin->setValue(slmPixelSize);
    
    form.addRow("SLM Width (pixels):", widthSpin);
    form.addRow("SLM Height (pixels):", heightSpin);
    form.addRow("SLM Pixel Size:", pixelSpin);
    
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        slmWidth = widthSpin->value();
        slmHeight = heightSpin->value();
        slmPixelSize = pixelSpin->value();
        
        resolutionLabel->setText(QString("Resolution: %1 x %2").arg(slmWidth).arg(slmHeight));
        this->statusBar()->showMessage("Hardware settings updated.", 3000);
    }
}

void MainWindow::onTabChanged(int index) {
    if (index == 0 || index == 3) {
        trapListContainer->setVisible(true);
    } else {
        trapListContainer->setVisible(false);
    }
}

void MainWindow::changeCamera(int index) {
    if (index < 0 || index >= availableCameras.size()) return;
    
    if (camera) {
        camera->stop();
        delete camera;
    }
    
    camera = new QCamera(availableCameras[index], this);
    captureSession->setCamera(camera);
}

void MainWindow::startCamera() {
    if (camera) camera->start();
}

void MainWindow::stopCamera() {
    if (camera) camera->stop();
}

void MainWindow::captureImage() {
    if (!camera || !camera->isActive()) return;

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QDir(defaultPath).filePath("HOT_Capture_" + timestamp + ".jpg");

    imageCapture->captureToFile(fileName);
    this->statusBar()->showMessage("Image saved to: " + fileName, 4000);
}

void MainWindow::toggleRecording(bool checked) {
    if (!camera || !camera->isActive()) {
        recordVideoBtn->setChecked(false); 
        return;
    }

    if (checked) {
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString fileName = QDir(defaultPath).filePath("HOT_Video_" + timestamp + ".mp4");

        mediaRecorder->setOutputLocation(QUrl::fromLocalFile(fileName));
        mediaRecorder->record();

        recordTimeLabel->setText("00:00");
        recordTimeLabel->setVisible(true);
        this->statusBar()->showMessage("Recording started...");
    } else {
        mediaRecorder->stop();
        recordTimeLabel->setVisible(false); 
        this->statusBar()->showMessage("Recording saved successfully.", 4000);
    }
}

void MainWindow::updateRecordTime(qint64 duration) {
    qint64 seconds = (duration / 1000) % 60;
    qint64 minutes = (duration / 60000) % 60;

    QString timeString = QString("%1:%2")
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0'));
                         
    recordTimeLabel->setText(timeString);
}

void MainWindow::applyDarkTheme() {
    QFile file(":/theme.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&file);
        this->setStyleSheet(stream.readAll());
        file.close();
    }
}