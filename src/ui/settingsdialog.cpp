#include "settingsdialog.h"
#include "components/arrowspinbox.h"
#include "../core/algorithms/gs_algorithm.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringList>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QtGlobal>
#include <QTabWidget>

SettingsDialog::SettingsDialog(int slmW, int slmH, double slmPix, int backend,
                               int camW, int camH, double camPix,
                               const QString &udpBindIp, int udpPort,
                               double wave, double focal, double cameraImagingMagnification, int slmOutputMode,
                               bool autoRunGsEnabled,
                               bool autoSendSlmEnabled,
                               int startingPhaseMaskMode,
                               int gsComputeBackendMode,
                               int openClPlatformIndex,
                               int openClDeviceIndex,
                               int cudaDeviceIndex,
                               int cameraRotation, bool flipX, bool saveCompressed,
                               bool saveFollowsTransforms, bool flipY,
                               QWidget *parent)
    : QDialog(parent) {

    setWindowTitle("Hardware Settings");
    setMinimumWidth(450);

    // Resize to fit primary screen height with a small buffer
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        const int availableHeight = screen->availableGeometry().height();
        resize(500, availableHeight - 60);
    } else {
        resize(500, 800);
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    QTabWidget *tabWidget = new QTabWidget(this);
    
    // --- HARDWARE TAB ---
    QWidget *hardwareTab = new QWidget();
    QVBoxLayout *hardwareLayout = new QVBoxLayout(hardwareTab);
    hardwareLayout->setContentsMargins(12, 12, 12, 12);
    hardwareLayout->setSpacing(8);

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
    cameraBackendCombo->addItems({"Qt Native (WMF)", "OpenCV (DirectShow)", "UDP Stream (Ethernet)"});
    cameraBackendCombo->setCurrentIndex(qBound(0, backend, 2));

    camWidthSpin = new ArrowSpinBox(this); camWidthSpin->setRange(100, 8000); camWidthSpin->setValue(camW);
    camHeightSpin = new ArrowSpinBox(this); camHeightSpin->setRange(100, 8000); camHeightSpin->setValue(camH);
    camPixelSpin = new ArrowDoubleSpinBox(this); camPixelSpin->setRange(0.1, 100.0); camPixelSpin->setDecimals(2);
    camPixelSpin->setSuffix(" um"); camPixelSpin->setValue(camPix);
    udpBindIpEdit = new QLineEdit(this);
    udpBindIpEdit->setPlaceholderText("0.0.0.0");
    udpBindIpEdit->setText(udpBindIp.isEmpty() ? QStringLiteral("0.0.0.0") : udpBindIp);
    udpPortSpin = new ArrowSpinBox(this); udpPortSpin->setRange(1, 65535); udpPortSpin->setValue(qBound(1, udpPort, 65535));

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

    cameraMagnificationSpin = new ArrowDoubleSpinBox(this);
    cameraMagnificationSpin->setRange(0.01, 1000.0);
    cameraMagnificationSpin->setDecimals(3);
    cameraMagnificationSpin->setValue(qMax(0.01, cameraImagingMagnification));
    cameraMagnificationSpin->setToolTip("Magnification between the trap plane and the camera sensor. "
                                        "Use this to calibrate spot placement without changing the real Fourier lens focal length.");

    opticsForm->addRow("Laser Wavelength:", waveSpin);
    opticsForm->addRow("Fourier Lens Focal Length (physical):", focalSpin);
    opticsForm->addRow("Camera Imaging Magnification:", cameraMagnificationSpin);
    opticsGroup->setLayout(opticsForm);

    QLabel *monitorHint = new QLabel("Monitor target is selected from Tools > Select Monitor.");
    monitorHint->setWordWrap(true);

    // Assemble the hardware tab
    hardwareLayout->addWidget(slmGroup);
    hardwareLayout->addWidget(camGroup);
    hardwareLayout->addWidget(opticsGroup);
    hardwareLayout->addWidget(monitorHint);
    hardwareLayout->addStretch();

    // --- UI SETTINGS TAB ---
    QWidget *uiTab = new QWidget();
    QFormLayout *uiLayout = new QFormLayout(uiTab);
    uiLayout->setContentsMargins(12, 12, 12, 12);
    uiLayout->setSpacing(8);

    cameraRotationCombo = new QComboBox(this);
    cameraRotationCombo->addItems({"0", "90", "180", "270"});
    int rotIndex = 0;
    if (cameraRotation == 90) rotIndex = 1;
    else if (cameraRotation == 180) rotIndex = 2;
    else if (cameraRotation == 270) rotIndex = 3;
    cameraRotationCombo->setCurrentIndex(rotIndex);

    flipXCheck = new QCheckBox(this);
    flipXCheck->setChecked(flipX);

    flipYCheck = new QCheckBox(this);
    flipYCheck->setChecked(flipY);

    saveCompressedCheck = new QCheckBox(this);
    saveCompressedCheck->setChecked(saveCompressed);

    saveFollowsTransformsCheck = new QCheckBox(this);
    saveFollowsTransformsCheck->setChecked(saveFollowsTransforms);

    QLabel *udpHeader = new QLabel("<b>UDP Stream Settings</b>", this);
    QLabel *captureHeader = new QLabel("<b>Capture Save Settings</b>", this);

    uiLayout->addRow(udpHeader);
    uiLayout->addRow("UDP Bind IP:", udpBindIpEdit);
    uiLayout->addRow("UDP Port:", udpPortSpin);
    uiLayout->addRow(captureHeader);
    uiLayout->addRow("Camera Rotation (deg):", cameraRotationCombo);
    uiLayout->addRow("Flip Camera X:", flipXCheck);
    uiLayout->addRow("Flip Camera Y:", flipYCheck);
    uiLayout->addRow("Save Compressed:", saveCompressedCheck);
    uiLayout->addRow("Save Using Rotation/Flip:", saveFollowsTransformsCheck);

    // Add Tabs
    tabWidget->addTab(hardwareTab, "Hardware");
    tabWidget->addTab(uiTab, "UI Settings");

    mainLayout->addWidget(tabWidget);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    mainLayout->addWidget(buttonBox);

    QPushButton *applyBtn = buttonBox->button(QDialogButtonBox::Apply);
    if (applyBtn) {
        connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::applyRequested);
    }

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// UI Settings Getters
int SettingsDialog::getCameraRotation() const { return cameraRotationCombo->currentText().toInt(); }
bool SettingsDialog::getFlipX() const { return flipXCheck->isChecked(); }
bool SettingsDialog::getFlipY() const { return flipYCheck->isChecked(); }
bool SettingsDialog::getSaveCompressed() const { return saveCompressedCheck->isChecked(); }
bool SettingsDialog::getSaveFollowsTransforms() const { return saveFollowsTransformsCheck->isChecked(); }

// SLM Getters
int SettingsDialog::getWidth() const { return widthSpin->value(); }
int SettingsDialog::getHeight() const { return heightSpin->value(); }
double SettingsDialog::getPixelSize() const { return pixelSpin->value(); }

// Camera Getters
int SettingsDialog::getCameraBackend() const { return cameraBackendCombo->currentIndex(); }
int SettingsDialog::getCamWidth() const { return camWidthSpin->value(); }
int SettingsDialog::getCamHeight() const { return camHeightSpin->value(); }
double SettingsDialog::getCamPixelSize() const { return camPixelSpin->value(); }
QString SettingsDialog::getUdpBindIp() const {
    const QString value = udpBindIpEdit->text().trimmed();
    return value.isEmpty() ? QStringLiteral("0.0.0.0") : value;
}
int SettingsDialog::getUdpPort() const { return udpPortSpin->value(); }

// Optical Getters
double SettingsDialog::getWavelength() const { return waveSpin->value(); }
double SettingsDialog::getFocalLength() const { return focalSpin->value(); }
double SettingsDialog::getCameraImagingMagnification() const { return cameraMagnificationSpin->value(); }

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
