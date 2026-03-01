#include "hologramdialog.h"
#include <QVBoxLayout>

HologramDialog::HologramDialog(int slmWidth, int slmHeight, QWidget *parent)
    : QDialog(parent), targetWidth(slmWidth), targetHeight(slmHeight) {
    
    setWindowTitle("Hologram Generator");
    // Set a decent default starting size for the popup
    resize(800, 600); 

    // We create the main layout, but leave it completely empty for now!
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
}