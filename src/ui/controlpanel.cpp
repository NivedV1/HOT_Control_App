#include "controlpanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QHeaderView>
#include <QDoubleSpinBox>

ControlPanel::ControlPanel(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void ControlPanel::setupUI() {
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // --- 1. Algorithm Settings ---
    QGroupBox *algoGroup = new QGroupBox("Algorithm Settings");
    QFormLayout *algoForm = new QFormLayout();
    
    algorithmCombo = new QComboBox();
    algorithmCombo->addItems({"Gerchberg-Saxton", "Weighted GS", "Direct Search"});
    
    iterationSpin = new QSpinBox();
    iterationSpin->setRange(1, 1000);
    iterationSpin->setValue(50);
    
    QDoubleSpinBox *relaxSpin = new QDoubleSpinBox();
    relaxSpin->setRange(0.0, 2.0);
    relaxSpin->setValue(0.8);
    
    algoForm->addRow("Algorithm:", algorithmCombo);
    algoForm->addRow("Iterations:", iterationSpin);
    algoForm->addRow("Relaxation:", relaxSpin);
    algoGroup->setLayout(algoForm);

    // --- 2. SLM Control ---
    QGroupBox *slmGroup = new QGroupBox("SLM Control");
    QVBoxLayout *slmLayout = new QVBoxLayout();
    
    QLabel *slmStatus = new QLabel("SLM: Connected");
    slmStatus->setStyleSheet("color: #4CAF50; font-weight: bold;");
    
    slmLayout->addWidget(slmStatus);
    slmLayout->addWidget(new QPushButton("Send to SLM"));
    slmLayout->addWidget(new QPushButton("Clear SLM"));
    slmLayout->addWidget(new QPushButton("Load Correction File"));
    slmGroup->setLayout(slmLayout);

    // --- 3. Camera Control ---
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

    // --- 4. Trap List & Controls ---
    QGroupBox *trapGroup = new QGroupBox("Trap List & Controls");
    QVBoxLayout *trapLayout = new QVBoxLayout();
    
    trapTable = new QTableWidget(0, 5);
    trapTable->setHorizontalHeaderLabels({"#", "X (µm)", "Y (µm)", "Intensity", "Size"});
    trapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    QHBoxLayout *tableBtns = new QHBoxLayout();
    tableBtns->addWidget(new QPushButton("Add Trap"));
    tableBtns->addWidget(new QPushButton("Remove Trap"));
    
    trapLayout->addWidget(trapTable);
    trapLayout->addLayout(tableBtns);
    trapGroup->setLayout(trapLayout);

    // Add all group boxes to the bottom layout
    mainLayout->addWidget(algoGroup);
    mainLayout->addWidget(slmGroup);
    mainLayout->addWidget(camGroup);
    mainLayout->addWidget(trapGroup);
}