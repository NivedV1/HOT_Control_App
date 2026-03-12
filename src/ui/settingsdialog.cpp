#include "settingsdialog.h"
#include "components/arrowspinbox.h"
#include "../core/algorithms/gs_algorithm.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QStandardItemModel>
#include <QStringList>
#include <QVBoxLayout>
#include <QtGlobal>

SettingsDialog::SettingsDialog(int slmW, int slmH, double slmPix, int backend,
                               int camW, int camH, double camPix,
                               double wave, double focal, int slmOutputMode,
                               bool autoRunGsEnabled,
                               bool autoSendSlmEnabled,
                               int startingPhaseMaskMode,
                               int gsComputeBackendMode,
                               int openClPlatformIndex,
                               int openClDeviceIndex,
                               int cudaDeviceIndex,
                               QWidget *parent)
    : QDialog(parent) {

    setWindowTitle("Hardware Settings");
    setMinimumWidth(430);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- 1. SLM SETTINGS ---
    QGroupBox *slmGroup = new QGroupBox("SLM Parameters");
    QFormLayout *slmForm = new QFormLayout();

    widthSpin = new ArrowSpinBox(this); widthSpin->setRange(100, 8000); widthSpin->setValue(slmW);
    heightSpin = new ArrowSpinBox(this); heightSpin->setRange(100, 8000); heightSpin->setValue(slmH);
    pixelSpin = new ArrowDoubleSpinBox(this); pixelSpin->setRange(0.1, 100.0); pixelSpin->setDecimals(2);
    pixelSpin->setSuffix(" um"); pixelSpin->setValue(slmPix);

    slmOutputModeCombo = new QComboBox(this);
    slmOutputModeCombo->addItems({"DLL", "Direct Screen"});
    slmOutputModeCombo->setCurrentIndex((slmOutputMode == 1) ? 1 : 0);

    autoRunGsCheck = new QCheckBox(this);
    autoRunGsCheck->setChecked(autoRunGsEnabled);

    autoSendSlmCheck = new QCheckBox(this);
    autoSendSlmCheck->setChecked(autoSendSlmEnabled);

    startingPhaseMaskCombo = new QComboBox(this);
    startingPhaseMaskCombo->addItem("Binary Grating", 1);
    startingPhaseMaskCombo->addItem("Checkerboard Pattern", 0);
    startingPhaseMaskCombo->addItem("Random Phase", 2);
    const int safeMode = qBound(0, startingPhaseMaskMode, 2);
    const int safeIndex = startingPhaseMaskCombo->findData(safeMode);
    startingPhaseMaskCombo->setCurrentIndex((safeIndex >= 0) ? safeIndex : 0);

    computeBackendCombo = new QComboBox(this);
#if HOT_ENABLE_CUDA_GS
    computeBackendCombo->addItem("Auto (CUDA -> OpenCL -> CPU)", 0);
#else
    computeBackendCombo->addItem("Auto (OpenCL -> CPU)", 0);
#endif
    computeBackendCombo->addItem("CPU", 1);
    computeBackendCombo->addItem("OpenCL", 2);
    computeBackendCombo->addItem("CUDA", 3);

#if !HOT_ENABLE_CUDA_GS
    const int cudaBackendRow = computeBackendCombo->findData(3);
    if (cudaBackendRow >= 0) {
        QStandardItemModel *model = qobject_cast<QStandardItemModel *>(computeBackendCombo->model());
        if (model) {
            QStandardItem *item = model->item(cudaBackendRow);
            if (item) {
                item->setText("CUDA (unavailable in this build)");
                item->setEnabled(false);
            }
        }
    }
#endif

    int requestedBackendMode = gsComputeBackendMode;
#if !HOT_ENABLE_CUDA_GS
    if (requestedBackendMode == 3) {
        requestedBackendMode = 0;
    }
#endif
    const int safeBackendIndex = computeBackendCombo->findData(requestedBackendMode);
    computeBackendCombo->setCurrentIndex((safeBackendIndex >= 0) ? safeBackendIndex : 0);

    openClDeviceCombo = new QComboBox(this);
    const QVector<GSAlgorithm::GSOpenClDeviceInfo> openClDevices = GSAlgorithm::enumerateOpenClDevices();
    if (openClDevices.isEmpty()) {
        openClDeviceCombo->addItem("No OpenCL devices detected", "-1:-1");
    } else {
        for (const GSAlgorithm::GSOpenClDeviceInfo &device : openClDevices) {
            const QString key = QString("%1:%2").arg(device.platformIndex).arg(device.deviceIndex);
            openClDeviceCombo->addItem(device.displayName, key);
        }
    }

    const QString targetDeviceKey = QString("%1:%2").arg(openClPlatformIndex).arg(openClDeviceIndex);
    const int selectedDeviceIdx = openClDeviceCombo->findData(targetDeviceKey);
    if (selectedDeviceIdx >= 0) {
        openClDeviceCombo->setCurrentIndex(selectedDeviceIdx);
    } else {
        openClDeviceCombo->setCurrentIndex(0);
    }

    cudaDeviceCombo = new QComboBox(this);
#if HOT_ENABLE_CUDA_GS
    const QVector<GSAlgorithm::GSCudaDeviceInfo> cudaDevices = GSAlgorithm::enumerateCudaDevices();
    if (cudaDevices.isEmpty()) {
        cudaDeviceCombo->addItem("No CUDA devices detected", -1);
    } else {
        for (const GSAlgorithm::GSCudaDeviceInfo &device : cudaDevices) {
            QString display = device.displayName;
            if (!device.isCompatible) {
                display += " [Incompatible]";
            }
            cudaDeviceCombo->addItem(display, device.deviceIndex);
        }
    }
#else
    cudaDeviceCombo->addItem("CUDA backend unavailable in this build", -1);
#endif

    const int cudaSelectedIdx = cudaDeviceCombo->findData(cudaDeviceIndex);
    if (cudaSelectedIdx >= 0) {
        cudaDeviceCombo->setCurrentIndex(cudaSelectedIdx);
    } else {
        cudaDeviceCombo->setCurrentIndex(0);
    }

    slmForm->addRow("SLM Width (pixels):", widthSpin);
    slmForm->addRow("SLM Height (pixels):", heightSpin);
    slmForm->addRow("SLM Pixel Size:", pixelSpin);
    slmForm->addRow("SLM Output Mode:", slmOutputModeCombo);
    slmForm->addRow("Auto-run GS:", autoRunGsCheck);
    slmForm->addRow("Auto-send SLM:", autoSendSlmCheck);
    slmForm->addRow("Starting Phase Mask:", startingPhaseMaskCombo);
    slmForm->addRow("GS Compute Backend:", computeBackendCombo);
    slmForm->addRow("OpenCL Device:", openClDeviceCombo);
    slmForm->addRow("CUDA Device:", cudaDeviceCombo);
    slmGroup->setLayout(slmForm);

    auto refreshDeviceEnabledState = [this]() {
        const int backendMode = computeBackendCombo->currentData().toInt();
        const bool backendNeedsOpenCl = (backendMode == 0 || backendMode == 2);
        const bool backendNeedsCuda = (backendMode == 0 || backendMode == 3);
        const bool hasValidDevice = openClDeviceCombo->currentData().toString() != "-1:-1";
        const bool hasValidCudaDevice = cudaDeviceCombo->currentData().toInt() >= 0;
        openClDeviceCombo->setEnabled(backendNeedsOpenCl && hasValidDevice);
        cudaDeviceCombo->setEnabled(backendNeedsCuda && hasValidCudaDevice);
    };
    connect(computeBackendCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [refreshDeviceEnabledState](int index) {
                Q_UNUSED(index);
                refreshDeviceEnabledState();
            });
    refreshDeviceEnabledState();

    // --- 2. CAMERA SETTINGS ---
    QGroupBox *camGroup = new QGroupBox("Camera Parameters");
    QFormLayout *camForm = new QFormLayout();

    cameraBackendCombo = new QComboBox(this);
    cameraBackendCombo->addItems({"Qt Native (WMF)", "OpenCV (DirectShow)"});
    cameraBackendCombo->setCurrentIndex(backend);

    camWidthSpin = new ArrowSpinBox(this); camWidthSpin->setRange(100, 8000); camWidthSpin->setValue(camW);
    camHeightSpin = new ArrowSpinBox(this); camHeightSpin->setRange(100, 8000); camHeightSpin->setValue(camH);
    camPixelSpin = new ArrowDoubleSpinBox(this); camPixelSpin->setRange(0.1, 100.0); camPixelSpin->setDecimals(2);
    camPixelSpin->setSuffix(" um"); camPixelSpin->setValue(camPix);

    camForm->addRow("Camera Engine:", cameraBackendCombo);
    camForm->addRow("Camera Width (px):", camWidthSpin);
    camForm->addRow("Camera Height (px):", camHeightSpin);
    camForm->addRow("Camera Pixel Size:", camPixelSpin);
    camGroup->setLayout(camForm);

    // --- 3. OPTICAL SETUP ---
    QGroupBox *opticsGroup = new QGroupBox("Optical Setup");
    QFormLayout *opticsForm = new QFormLayout();

    waveSpin = new ArrowDoubleSpinBox(this); waveSpin->setRange(100.0, 2000.0); waveSpin->setDecimals(1);
    waveSpin->setSuffix(" nm"); waveSpin->setValue(wave);

    focalSpin = new ArrowDoubleSpinBox(this); focalSpin->setRange(1.0, 1000.0); focalSpin->setDecimals(1);
    focalSpin->setSuffix(" mm"); focalSpin->setValue(focal);

    opticsForm->addRow("Laser Wavelength:", waveSpin);
    opticsForm->addRow("Fourier Lens Focal Length:", focalSpin);
    opticsGroup->setLayout(opticsForm);

    QLabel *monitorHint = new QLabel("Monitor target is selected from Tools > Select Monitor.");
    monitorHint->setWordWrap(true);

    // Assemble the dialog
    mainLayout->addWidget(slmGroup);
    mainLayout->addWidget(camGroup);
    mainLayout->addWidget(opticsGroup);
    mainLayout->addWidget(monitorHint);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// SLM Getters
int SettingsDialog::getWidth() const { return widthSpin->value(); }
int SettingsDialog::getHeight() const { return heightSpin->value(); }
double SettingsDialog::getPixelSize() const { return pixelSpin->value(); }

// Camera Getters
int SettingsDialog::getCameraBackend() const { return cameraBackendCombo->currentIndex(); }
int SettingsDialog::getCamWidth() const { return camWidthSpin->value(); }
int SettingsDialog::getCamHeight() const { return camHeightSpin->value(); }
double SettingsDialog::getCamPixelSize() const { return camPixelSpin->value(); }

// Optical Getters
double SettingsDialog::getWavelength() const { return waveSpin->value(); }
double SettingsDialog::getFocalLength() const { return focalSpin->value(); }

// SLM Output Getter
int SettingsDialog::getSlmOutputMode() const { return slmOutputModeCombo->currentIndex(); }

// Algorithm behavior getters
bool SettingsDialog::getAutoRunGsEnabled() const { return autoRunGsCheck->isChecked(); }
bool SettingsDialog::getAutoSendSlmEnabled() const { return autoSendSlmCheck->isChecked(); }
int SettingsDialog::getStartingPhaseMaskMode() const { return startingPhaseMaskCombo->currentData().toInt(); }
int SettingsDialog::getGsComputeBackendMode() const { return computeBackendCombo->currentData().toInt(); }

int SettingsDialog::getOpenClPlatformIndex() const {
    const QString packed = openClDeviceCombo->currentData().toString();
    const QStringList parts = packed.split(':');
    if (parts.size() != 2) {
        return -1;
    }
    bool ok = false;
    const int value = parts.at(0).toInt(&ok);
    return ok ? value : -1;
}

int SettingsDialog::getOpenClDeviceIndex() const {
    const QString packed = openClDeviceCombo->currentData().toString();
    const QStringList parts = packed.split(':');
    if (parts.size() != 2) {
        return -1;
    }
    bool ok = false;
    const int value = parts.at(1).toInt(&ok);
    return ok ? value : -1;
}

int SettingsDialog::getCudaDeviceIndex() const {
    bool ok = false;
    const int value = cudaDeviceCombo->currentData().toInt(&ok);
    return ok ? value : -1;
}

