#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTableWidget>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Slot to hide/show the table
    void onTabChanged(int index);

private:
    void setupUI();
    void applyDarkTheme();

    // UI Widgets we need access to later
    QGraphicsView *targetView;
    QGraphicsScene *targetScene;
    QTabWidget *targetModeTabs;
    QWidget *trapListContainer;
    QTableWidget *trapTable;

    QLabel *phaseMaskLabel;
    QLabel *cameraFeedLabel;
};

#endif // MAINWINDOW_H  