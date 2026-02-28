#ifndef MONITORPANEL_H
#define MONITORPANEL_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QComboBox>

class MonitorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MonitorPanel(QWidget *parent = nullptr);

    // Getters so MainWindow can access these later to update them
    QGraphicsScene* getTargetScene() const { return targetScene; }
    QLabel* getPhaseMaskLabel() const { return phaseMaskLabel; }
    QLabel* getCameraLabel() const { return cameraFeedLabel; }

private:
    void setupUI();

    QGraphicsView *targetView;
    QGraphicsScene *targetScene;
    QLabel *phaseMaskLabel;
    QLabel *cameraFeedLabel;
    QComboBox *patternCombo;
};

#endif // MONITORPANEL_H