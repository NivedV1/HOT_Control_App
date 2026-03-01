#ifndef HOLOGRAMDIALOG_H
#define HOLOGRAMDIALOG_H

#include <QDialog>

class HologramDialog : public QDialog {
    Q_OBJECT
public:
    // We pass the SLM resolution into the constructor so the window knows the target size
    explicit HologramDialog(int slmWidth, int slmHeight, QWidget *parent = nullptr);

private:
    int targetWidth;
    int targetHeight;
};

#endif // HOLOGRAMDIALOG_H  