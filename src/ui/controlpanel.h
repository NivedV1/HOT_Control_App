#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>

class ControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ControlPanel(QWidget *parent = nullptr);

    // Getters for settings
    QTableWidget* getTrapTable() const { return trapTable; }
    int getIterations() const { return iterationSpin->value(); }
    QString getAlgorithm() const { return algorithmCombo->currentText(); }

private:
    void setupUI();

    QComboBox *algorithmCombo;
    QSpinBox *iterationSpin;
    QTableWidget *trapTable;
};

#endif // CONTROLPANEL_H