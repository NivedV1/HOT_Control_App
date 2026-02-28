#include "monitorpanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QCheckBox>

MonitorPanel::MonitorPanel(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void MonitorPanel::setupUI() {
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // --- 1. Target Pattern ---
    QVBoxLayout *targetLayout = new QVBoxLayout();
    QLabel *targetTitle = new QLabel("Target Pattern");
    targetTitle->setAlignment(Qt::AlignCenter);
    
    targetView = new QGraphicsView();
    targetScene = new QGraphicsScene(this);
    targetView->setScene(targetScene);
    targetView->setMinimumSize(350, 250);
    
    QHBoxLayout *targetTools = new QHBoxLayout();
    targetTools->addWidget(new QPushButton("Add"));
    targetTools->addWidget(new QPushButton("Move"));
    targetTools->addWidget(new QPushButton("Delete"));
    targetTools->addWidget(new QPushButton("Clear All"));
    
    patternCombo = new QComboBox();
    patternCombo->addItems({"Manual", "Square Grid", "Hexagonal Grid"});
    targetTools->addWidget(patternCombo);

    targetLayout->addWidget(targetTitle);
    targetLayout->addWidget(targetView);
    targetLayout->addLayout(targetTools);

    // --- 2. Phase Mask ---
    QVBoxLayout *phaseLayout = new QVBoxLayout();
    QLabel *phaseTitle = new QLabel("Phase Mask");
    phaseTitle->setAlignment(Qt::AlignCenter);
    
    phaseMaskLabel = new QLabel();
    phaseMaskLabel->setMinimumSize(350, 250);
    phaseMaskLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    phaseMaskLabel->setStyleSheet("background-color: black; border: 1px solid #444;");
    
    QHBoxLayout *phaseTools = new QHBoxLayout();
    phaseTools->addWidget(new QLabel("Resolution: 1920 x 1080"));
    phaseTools->addStretch();
    phaseTools->addWidget(new QPushButton("Send to SLM"));
    phaseTools->addWidget(new QPushButton("Save Mask"));

    phaseLayout->addWidget(phaseTitle);
    phaseLayout->addWidget(phaseMaskLabel);
    phaseLayout->addLayout(phaseTools);

    // --- 3. Live Camera Feed ---
    QVBoxLayout *cameraLayout = new QVBoxLayout();
    QLabel *cameraTitle = new QLabel("Live Camera Feed");
    cameraTitle->setAlignment(Qt::AlignCenter);
    
    cameraFeedLabel = new QLabel();
    cameraFeedLabel->setMinimumSize(350, 250);
    cameraFeedLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cameraFeedLabel->setStyleSheet("background-color: #111; border: 1px solid #444;");
    
    QHBoxLayout *camTools = new QHBoxLayout();
    camTools->addWidget(new QLabel("FPS: 30  Exposure: 10ms"));
    camTools->addStretch();
    camTools->addWidget(new QCheckBox("Overlay Target"));

    cameraLayout->addWidget(cameraTitle);
    cameraLayout->addWidget(cameraFeedLabel);
    cameraLayout->addLayout(camTools);

    // Add all three columns to the main layout
    mainLayout->addLayout(targetLayout);
    mainLayout->addLayout(phaseLayout);
    mainLayout->addLayout(cameraLayout);
}