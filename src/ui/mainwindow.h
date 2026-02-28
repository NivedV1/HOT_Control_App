#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "monitorpanel.h"
#include "controlpanel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void applyDarkTheme();

    MonitorPanel *monitorPanel;
    ControlPanel *controlPanel;
};

#endif // MAINWINDOW_H