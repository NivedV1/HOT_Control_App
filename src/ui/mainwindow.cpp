#include "mainwindow.h"
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Holographic Optical Tweezer Control");
    resize(1200, 800);

    // Create the central widget and main layout
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Instantiate our custom panels
    monitorPanel = new MonitorPanel(this);
    controlPanel = new ControlPanel(this);

    // Add panels to the layout with stretch factors (2:1 ratio)
    mainLayout->addWidget(monitorPanel, 2); 
    mainLayout->addWidget(controlPanel, 1); 

    applyDarkTheme();
}

MainWindow::~MainWindow() {
    // Qt automatically deletes child widgets, but we define the destructor anyway
}

void MainWindow::applyDarkTheme() {
    // We will eventually move this to resources/theme.qss, but keeping it here for quick testing
    this->setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #2b2b2b;
            color: #e0e0e0;
            font-family: Arial, sans-serif;
            font-size: 10pt;
        }
        QGroupBox {
            border: 1px solid #555;
            border-radius: 4px;
            margin-top: 1ex;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top center;
            padding: 0 3px;
        }
        QPushButton {
            background-color: #3c3f41;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px;
        }
        QPushButton:hover { background-color: #4b4d4f; }
        QPushButton:pressed { background-color: #2d2f30; }
        QComboBox, QSpinBox, QDoubleSpinBox {
            background-color: #3c3f41;
            border: 1px solid #555;
            padding: 3px;
        }
        QTableWidget {
            background-color: #1e1e1e;
            alternate-background-color: #2a2a2a;
            gridline-color: #444;
        }
        QHeaderView::section {
            background-color: #3c3f41;
            border: 1px solid #555;
        }
    )");
}