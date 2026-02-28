#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSlider>
#include <QFile>
#include <QTextStream>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    setMinimumSize(1200, 800); // Prevents window from shrinking

    setupUI();
    applyDarkTheme();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    // Top Menu Bar
    QMenu *fileMenu = menuBar()->addMenu("&File");
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
    // ROW 1: The Three Monitors (Guaranteed Identical Size!)
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

    cameraFeedLabel = new QLabel();
    cameraFeedLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cameraFeedLabel->setStyleSheet("background-color: #111; border: 1px solid #444;");
    mainLayout->addWidget(cameraFeedLabel, 1, 2);

    // ==========================================
    // ROW 2: Monitor Toolbars
    // ==========================================
    // Col 0: Invisible spacer to balance the toolbars in Col 1 & 2
    QHBoxLayout *targetTools = new QHBoxLayout();
    targetTools->addSpacerItem(new QSpacerItem(10, 28, QSizePolicy::Minimum, QSizePolicy::Fixed));
    mainLayout->addLayout(targetTools, 2, 0);

    // Col 1: Phase Tools
    QHBoxLayout *phaseTools = new QHBoxLayout();
    phaseTools->addWidget(new QLabel("Resolution: 1920 x 1080"));
    phaseTools->addStretch();
    phaseTools->addWidget(new QPushButton("Send to SLM"));
    phaseTools->addWidget(new QPushButton("Save Mask"));
    mainLayout->addLayout(phaseTools, 2, 1);

    // Col 2: Cam Tools
    QHBoxLayout *camTools = new QHBoxLayout();
    camTools->addWidget(new QLabel("FPS: 30  Exposure: 10ms"));
    camTools->addStretch();
    camTools->addWidget(new QCheckBox("Overlay Target"));
    mainLayout->addLayout(camTools, 2, 2);

    // ==========================================
    // ROW 3: The Controls (Tabs, SLM, Camera settings)
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

// ==========================================
    // 3, 1: Algorithm Settings (Middle Column)
    // ==========================================
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
    middleControlsLayout->addStretch(); // Pushes Algorithm settings to the top
    mainLayout->addLayout(middleControlsLayout, 3, 1);

    // ==========================================
    // 3, 2: Camera & SLM Controls (Right Column)
    // ==========================================
    QVBoxLayout *rightControlsLayout = new QVBoxLayout();
    
    // --- Camera Control ---
    QGroupBox *camGroup = new QGroupBox("Camera Control");
    QFormLayout *camForm = new QFormLayout();
    QComboBox *camSelect = new QComboBox();
    camSelect->addItem("Camera 1 (Default)");
    camForm->addRow("Camera:", camSelect);
    camForm->addRow("Exposure:", new QSlider(Qt::Horizontal));
    camForm->addRow("Gain:", new QSlider(Qt::Horizontal));
    QHBoxLayout *camButtons = new QHBoxLayout();
    camButtons->addWidget(new QPushButton("Start"));
    camButtons->addWidget(new QPushButton("Stop"));
    camForm->addRow(camButtons);
    camGroup->setLayout(camForm);

    // --- SLM Control ---
    QGroupBox *slmGroup = new QGroupBox("SLM Control");
    QVBoxLayout *slmLayout = new QVBoxLayout();
    QLabel *slmStatus = new QLabel("SLM: Connected");
    slmStatus->setStyleSheet("color: #4CAF50; font-weight: bold;");
    slmLayout->addWidget(slmStatus);
    
    // Updated SLM Buttons
    slmLayout->addWidget(new QPushButton("Load Existing Phase Pattern"));
    slmLayout->addWidget(new QPushButton("Load Correction File"));
    slmLayout->addWidget(new QPushButton("Clear SLM Pattern"));
    slmGroup->setLayout(slmLayout);

    // Stack them in the right column
    rightControlsLayout->addWidget(camGroup);
    rightControlsLayout->addWidget(slmGroup);
    rightControlsLayout->addStretch(); // Pushes both groups neatly to the top
    mainLayout->addLayout(rightControlsLayout, 3, 2);

    // ==========================================
    // GRID PROPORTIONS (The Layout Lock)
    // ==========================================
    // Force 3 equal columns horizontally
    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(1, 1);
    mainLayout->setColumnStretch(2, 1);

    // Give vertical stretching priority to the Monitors (Row 1) and the Controls (Row 3)
    mainLayout->setRowStretch(0, 0); // Titles: Don't stretch
    mainLayout->setRowStretch(1, 3); // Monitors: Stretch aggressively (Ratio 3)
    mainLayout->setRowStretch(2, 0); // Toolbars: Don't stretch
    mainLayout->setRowStretch(3, 2); // Controls & Trap List: Stretch moderately (Ratio 2)

    // Connections
    connect(targetModeTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    onTabChanged(0);

    // Bottom Status Bar
    QStatusBar *statusBar = this->statusBar();
    statusBar->addWidget(new QLabel(" SLM: Connected | Camera: Active | Algorithm: GS "));
}
// Logic to hide/show the trap list based on the active tab
void MainWindow::onTabChanged(int index) {
    // Index 0 is Manual, Index 3 is Camera
    if (index == 0 || index == 3) {
        trapListContainer->setVisible(true);
    } else {
        trapListContainer->setVisible(false);
    }
}

// Logic to load our CSS file
void MainWindow::applyDarkTheme() {
    QFile file(":/theme.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&file);
        this->setStyleSheet(stream.readAll());
        file.close();
    }
}