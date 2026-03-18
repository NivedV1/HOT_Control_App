#include "computebenchmarkdialog.h"

#include "../core/algorithms/gs_algorithm.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
constexpr int kBackendCpu = static_cast<int>(GSAlgorithm::GSComputeBackend::CPU);
constexpr int kBackendOpenCl = static_cast<int>(GSAlgorithm::GSComputeBackend::OpenCL);
constexpr int kBackendCuda = static_cast<int>(GSAlgorithm::GSComputeBackend::CUDA);

constexpr int kWarmupRuns = 1;
constexpr int kTimedRuns = 5;

constexpr int kCanonicalSlmWidth = 1920;
constexpr int kCanonicalSlmHeight = 1080;
constexpr int kCanonicalCamWidth = 1920;
constexpr int kCanonicalCamHeight = 1080;
constexpr int kCanonicalIterations = 60;
constexpr double kCanonicalSlmPixelSizeUm = 8.0;
constexpr double kCanonicalCamPixelSizeUm = 5.0;
constexpr double kCanonicalWavelengthNm = 1064.0;
constexpr double kCanonicalFocalLengthMm = 100.0;
constexpr double kCanonicalTargetRadiusPx = 60.0;
constexpr int kCanonicalTargetSpots = 18;
constexpr double kTwoPi = 6.28318530717958647692;

QString formatDouble(double value) {
    return QString::number(value, 'f', 3);
}

class BenchmarkWorker : public QObject {
    Q_OBJECT

public:
    struct Request {
        QVector<int> backendIds;
        int openClPlatformIndex = 0;
        int openClDeviceIndex = 0;
        int cudaDeviceIndex = 0;
        int warmupRuns = kWarmupRuns;
        int timedRuns = kTimedRuns;
        QString modeLabel;
    };

    explicit BenchmarkWorker(QObject *parent = nullptr);

public slots:
    void run(const Request &request);
    void cancel();

signals:
    void runStarted(int totalSteps, const QString &modeLabel);
    void progress(const QString &statusText, int completedSteps);
    void backendStatus(int backendId, const QString &status, const QString &notes);
    void timedRunResult(int backendId,
                        int runIndex,
                        qint64 elapsedMs,
                        double msPerIteration,
                        const QImage &phaseMaskPreview,
                        bool strictPass,
                        int backendUsedId,
                        const QString &backendInfo,
                        bool fallbackOccurred,
                        const QString &fallbackReason,
                        const QString &error);
    void backendDone(int backendId,
                     bool passed,
                     const QString &status,
                     const QString &requestedBackend,
                     const QString &usedBackend,
                     const QString &backendInfo,
                     const QString &notes,
                     double meanMs,
                     double minMs,
                     double maxMs,
                     double stddevMs,
                     double meanMsPerIter,
                     int completedTimedRuns,
                     bool hadFailure);
    void runFinished(bool cancelled, const QString &message);

private:
    struct CanonicalWorkload {
        GSAlgorithm::GSConfig config;
        QVector<float> sourceAmplitude;
        QVector<GSAlgorithm::GSTargetPoint> targets;
    };

    CanonicalWorkload buildCanonicalWorkload(const Request &request) const;
    static QString backendName(int backendId);
    static QString backendUsedName(int backendUsedId);
    static int expectedBackendUsed(int requestedBackendId);
    static QString strictValidationError(int requestedBackendId, const GSAlgorithm::GSResult &result);

    std::atomic_bool cancelRequested {false};
};

BenchmarkWorker::BenchmarkWorker(QObject *parent)
    : QObject(parent) {
}

void BenchmarkWorker::run(const Request &request) {
    cancelRequested.store(false);
    const int totalSteps = request.backendIds.size() * (request.warmupRuns + request.timedRuns);
    emit runStarted(totalSteps, request.modeLabel);
    emit progress("Preparing canonical benchmark workload...", 0);

    CanonicalWorkload workload = buildCanonicalWorkload(request);
    int completedSteps = 0;

    for (int backendId : request.backendIds) {
        if (cancelRequested.load()) {
            break;
        }

        const QString requestedName = backendName(backendId);
        emit backendStatus(backendId,
                           QString("Running %1 benchmark...").arg(requestedName),
                           QString("Warmup: %1, Timed: %2").arg(request.warmupRuns).arg(request.timedRuns));

        QVector<double> timedElapsedMs;
        QString usedBackend;
        QString backendInfo;
        QString notes;
        bool passed = true;
        bool hadFailure = false;
        int completedTimedRuns = 0;

        auto runSingle = [&](bool timedRun, int runIndex) -> bool {
            GSAlgorithm::GSConfig config = workload.config;
            config.computeBackend = static_cast<GSAlgorithm::GSComputeBackend>(backendId);

            QElapsedTimer timer;
            timer.start();
            const GSAlgorithm::GSResult result =
                GSAlgorithm::runGerchbergSaxton(config, workload.sourceAmplitude, workload.targets);
            const qint64 elapsedMs = timer.elapsed();

            ++completedSteps;
            const QString phaseLabel = timedRun ? "timed" : "warmup";
            emit progress(QString("%1 %2 run %3/%4")
                              .arg(requestedName)
                              .arg(phaseLabel)
                              .arg(runIndex)
                              .arg(timedRun ? request.timedRuns : request.warmupRuns),
                          completedSteps);

            usedBackend = backendUsedName(static_cast<int>(result.backendUsed));
            backendInfo = result.backendInfo;
            const QString strictError = strictValidationError(backendId, result);
            const bool strictPass = strictError.isEmpty();

            if (timedRun) {
                const double msPerIteration = config.iterations > 0
                    ? static_cast<double>(elapsedMs) / static_cast<double>(config.iterations)
                    : 0.0;
                const QString runError = strictPass ? result.error : strictError;
                emit timedRunResult(backendId,
                                    runIndex,
                                    elapsedMs,
                                    msPerIteration,
                                    result.phaseMask8Bit,
                                    strictPass,
                                    static_cast<int>(result.backendUsed),
                                    result.backendInfo,
                                    result.fallbackOccurred,
                                    result.fallbackReason,
                                    runError);
            }

            if (!strictPass) {
                hadFailure = true;
                passed = false;
                notes = strictError;
                return false;
            }

            if (timedRun) {
                timedElapsedMs.append(static_cast<double>(elapsedMs));
                ++completedTimedRuns;
            }
            return true;
        };

        for (int warmupIdx = 1; warmupIdx <= request.warmupRuns; ++warmupIdx) {
            if (cancelRequested.load()) {
                break;
            }
            if (!runSingle(false, warmupIdx)) {
                break;
            }
        }

        if (passed) {
            for (int timedIdx = 1; timedIdx <= request.timedRuns; ++timedIdx) {
                if (cancelRequested.load()) {
                    break;
                }
                if (!runSingle(true, timedIdx)) {
                    break;
                }
            }
        }

        double meanMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double stddevMs = 0.0;
        double meanMsPerIter = 0.0;

        if (!timedElapsedMs.isEmpty()) {
            minMs = *std::min_element(timedElapsedMs.cbegin(), timedElapsedMs.cend());
            maxMs = *std::max_element(timedElapsedMs.cbegin(), timedElapsedMs.cend());

            double sum = 0.0;
            for (double v : timedElapsedMs) {
                sum += v;
            }
            meanMs = sum / static_cast<double>(timedElapsedMs.size());

            double variance = 0.0;
            for (double v : timedElapsedMs) {
                const double diff = v - meanMs;
                variance += diff * diff;
            }
            variance /= static_cast<double>(timedElapsedMs.size());
            stddevMs = std::sqrt(std::max(0.0, variance));
            meanMsPerIter = static_cast<double>(kCanonicalIterations) > 0.0
                ? meanMs / static_cast<double>(kCanonicalIterations)
                : 0.0;
        }

        QString status;
        if (cancelRequested.load()) {
            passed = false;
            status = "Cancelled";
            if (notes.isEmpty()) {
                notes = "Cancelled by user.";
            }
        } else if (!passed) {
            status = "Failed/Unavailable";
        } else if (completedTimedRuns < request.timedRuns) {
            passed = false;
            status = "Incomplete";
            if (notes.isEmpty()) {
                notes = QString("Completed %1/%2 timed runs.")
                            .arg(completedTimedRuns)
                            .arg(request.timedRuns);
            }
        } else {
            status = QString("Passed (%1/%2)").arg(completedTimedRuns).arg(request.timedRuns);
            if (notes.isEmpty()) {
                notes = "Strict backend validation passed.";
            }
        }

        emit backendDone(backendId,
                         passed,
                         status,
                         requestedName,
                         usedBackend,
                         backendInfo,
                         notes,
                         meanMs,
                         minMs,
                         maxMs,
                         stddevMs,
                         meanMsPerIter,
                         completedTimedRuns,
                         hadFailure);

        if (cancelRequested.load()) {
            break;
        }
    }

    const bool cancelled = cancelRequested.load();
    emit runFinished(cancelled, cancelled ? "Benchmark cancelled." : "Benchmark completed.");
}

void BenchmarkWorker::cancel() {
    cancelRequested.store(true);
}

BenchmarkWorker::CanonicalWorkload BenchmarkWorker::buildCanonicalWorkload(const Request &request) const {
    CanonicalWorkload workload;
    workload.config.slmWidth = kCanonicalSlmWidth;
    workload.config.slmHeight = kCanonicalSlmHeight;
    workload.config.slmPixelSizeUm = kCanonicalSlmPixelSizeUm;
    workload.config.camWidth = kCanonicalCamWidth;
    workload.config.camHeight = kCanonicalCamHeight;
    workload.config.camPixelSizeUm = kCanonicalCamPixelSizeUm;
    workload.config.wavelengthNm = kCanonicalWavelengthNm;
    workload.config.focalLengthMm = kCanonicalFocalLengthMm;
    workload.config.iterations = kCanonicalIterations;
    workload.config.startingPhaseMask = GSAlgorithm::GSStartingPhaseMask::Checkerboard;
    workload.config.computeBackend = GSAlgorithm::GSComputeBackend::CPU;
    workload.config.openClPlatformIndex = request.openClPlatformIndex;
    workload.config.openClDeviceIndex = request.openClDeviceIndex;
    workload.config.cudaDeviceIndex = request.cudaDeviceIndex;

    const double beamWaistPx = static_cast<double>(std::min(kCanonicalSlmWidth, kCanonicalSlmHeight)) / 6.0;
    workload.sourceAmplitude =
        GSAlgorithm::buildGaussianSourceAmplitude(kCanonicalSlmWidth, kCanonicalSlmHeight, beamWaistPx);

    workload.targets.reserve(kCanonicalTargetSpots);
    for (int i = 0; i < kCanonicalTargetSpots; ++i) {
        const double angle = (kTwoPi * static_cast<double>(i)) / static_cast<double>(kCanonicalTargetSpots);
        GSAlgorithm::GSTargetPoint point;
        point.xCamPx = kCanonicalTargetRadiusPx * std::cos(angle);
        point.yCamPx = kCanonicalTargetRadiusPx * std::sin(angle);
        workload.targets.append(point);
    }

    return workload;
}

QString BenchmarkWorker::backendName(int backendId) {
    switch (backendId) {
    case kBackendOpenCl:
        return "OpenCL";
    case kBackendCuda:
        return "CUDA";
    case kBackendCpu:
    default:
        return "CPU";
    }
}

QString BenchmarkWorker::backendUsedName(int backendUsedId) {
    switch (backendUsedId) {
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::OpenCL):
        return "OpenCL";
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CUDA):
        return "CUDA";
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CPU):
    default:
        return "CPU";
    }
}

int BenchmarkWorker::expectedBackendUsed(int requestedBackendId) {
    switch (requestedBackendId) {
    case kBackendOpenCl:
        return static_cast<int>(GSAlgorithm::GSComputeBackendUsed::OpenCL);
    case kBackendCuda:
        return static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CUDA);
    case kBackendCpu:
    default:
        return static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CPU);
    }
}

QString BenchmarkWorker::strictValidationError(int requestedBackendId, const GSAlgorithm::GSResult &result) {
    if (!result.success) {
        return result.error.isEmpty()
            ? QString("Execution failed without an error message.")
            : QString("Execution failed: %1").arg(result.error);
    }

    const int expectedUsed = expectedBackendUsed(requestedBackendId);
    const int usedBackendId = static_cast<int>(result.backendUsed);

    if (usedBackendId != expectedUsed) {
        QString mismatch = QString("Strict backend mismatch: requested %1, used %2.")
                               .arg(backendName(requestedBackendId))
                               .arg(backendUsedName(usedBackendId));
        if (!result.fallbackReason.isEmpty()) {
            mismatch += QString(" Fallback reason: %1").arg(result.fallbackReason);
        }
        return mismatch;
    }

    if (result.fallbackOccurred) {
        const QString reason = result.fallbackReason.isEmpty()
            ? QString("No fallback reason was provided.")
            : result.fallbackReason;
        return QString("Fallback occurred: %1").arg(reason);
    }

    return QString();
}

} // namespace

ComputeBenchmarkDialog::ComputeBenchmarkDialog(int openClPlatformIndex,
                                               int openClDeviceIndex,
                                               int cudaDeviceIndex,
                                               QWidget *parent)
    : QDialog(parent),
      openClPlatformIndex(openClPlatformIndex),
      openClDeviceIndex(openClDeviceIndex),
      cudaDeviceIndex(cudaDeviceIndex) {
    setWindowTitle("Benchmark Compute");
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(1240, 820);

    buildUi();
    initializeTable();
    refreshCanonicalInfoText();
    setRunningState(false);
}

ComputeBenchmarkDialog::~ComputeBenchmarkDialog() {
    if (isRunning) {
        requestCancel();
    }

    if (workerThread) {
        workerThread->quit();
        workerThread->wait();
        delete workerThread;
        workerThread = nullptr;
        workerObject = nullptr;
    }
}

void ComputeBenchmarkDialog::setDeviceSelectionIndices(int newOpenClPlatformIndex,
                                                       int newOpenClDeviceIndex,
                                                       int newCudaDeviceIndex) {
    openClPlatformIndex = newOpenClPlatformIndex;
    openClDeviceIndex = newOpenClDeviceIndex;
    cudaDeviceIndex = newCudaDeviceIndex;
    refreshCanonicalInfoText();
}

void ComputeBenchmarkDialog::buildUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *canonicalGroup = new QGroupBox("Canonical Workload (Immutable)");
    QVBoxLayout *canonicalLayout = new QVBoxLayout(canonicalGroup);
    canonicalInfoLabel = new QLabel();
    canonicalInfoLabel->setWordWrap(true);
    canonicalInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    canonicalLayout->addWidget(canonicalInfoLabel);
    mainLayout->addWidget(canonicalGroup);

    QHBoxLayout *contentLayout = new QHBoxLayout();

    QGroupBox *tableGroup = new QGroupBox("Backend Results");
    QVBoxLayout *tableLayout = new QVBoxLayout(tableGroup);
    resultTable = new QTableWidget(3, 11, this);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionMode(QAbstractItemView::NoSelection);
    resultTable->verticalHeader()->setVisible(false);
    resultTable->setHorizontalHeaderLabels(
        {"Backend", "Status", "Timed Runs", "Mean (ms)", "Min (ms)", "Max (ms)", "StdDev (ms)",
         "Mean ms/iter", "Used Backend", "Notes", "Action"});
    resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    resultTable->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Stretch);
    resultTable->setAlternatingRowColors(true);
    tableLayout->addWidget(resultTable);
    contentLayout->addWidget(tableGroup, 3);

    QGroupBox *previewGroup = new QGroupBox("Latest Phase Mask Preview");
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);
    previewInfoLabel = new QLabel("No benchmark run yet.");
    previewInfoLabel->setWordWrap(true);
    previewImageLabel = new QLabel();
    previewImageLabel->setAlignment(Qt::AlignCenter);
    previewImageLabel->setMinimumSize(420, 320);
    previewImageLabel->setStyleSheet("background-color: black; border: 1px solid #555;");
    updatePreviewLabelText("Preview appears after each timed run.");
    previewLayout->addWidget(previewInfoLabel);
    previewLayout->addWidget(previewImageLabel, 1);
    contentLayout->addWidget(previewGroup, 2);

    mainLayout->addLayout(contentLayout, 1);

    progressBar = new QProgressBar();
    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    statusLabel = new QLabel("Ready. Choose a backend test or run Auto Test.");
    statusLabel->setWordWrap(true);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(statusLabel);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    autoTestBtn = new QPushButton("Auto Test");
    cancelBtn = new QPushButton("Cancel");
    saveCsvBtn = new QPushButton("Save CSV...");
    closeBtn = new QPushButton("Close");
    buttonLayout->addWidget(autoTestBtn);
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveCsvBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    mainLayout->addLayout(buttonLayout);

    connect(autoTestBtn, &QPushButton::clicked, this, &ComputeBenchmarkDialog::startAutoBenchmark);
    connect(cancelBtn, &QPushButton::clicked, this, &ComputeBenchmarkDialog::requestCancel);
    connect(saveCsvBtn, &QPushButton::clicked, this, &ComputeBenchmarkDialog::saveCsv);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

void ComputeBenchmarkDialog::initializeTable() {
    const QVector<int> backendIds = {kBackendCpu, kBackendOpenCl, kBackendCuda};
    for (int row = 0; row < backendIds.size(); ++row) {
        const int backendId = backendIds[row];

        QTableWidgetItem *backendItem = new QTableWidgetItem(backendName(backendId));
        backendItem->setFlags(backendItem->flags() & ~Qt::ItemIsEditable);
        resultTable->setItem(row, 0, backendItem);

        for (int col = 1; col < 10; ++col) {
            QTableWidgetItem *item = new QTableWidgetItem();
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            resultTable->setItem(row, col, item);
        }

        QPushButton *testButton = new QPushButton("Test");
        resultTable->setCellWidget(row, 10, testButton);
        if (backendId == kBackendCpu) {
            cpuTestBtn = testButton;
            connect(testButton, &QPushButton::clicked, this, &ComputeBenchmarkDialog::startCpuBenchmark);
        } else if (backendId == kBackendOpenCl) {
            openClTestBtn = testButton;
            connect(testButton, &QPushButton::clicked, this, &ComputeBenchmarkDialog::startOpenClBenchmark);
        } else {
            cudaTestBtn = testButton;
            connect(testButton, &QPushButton::clicked, this, &ComputeBenchmarkDialog::startCudaBenchmark);
        }

        resetBackendRow(backendId);
    }
}

void ComputeBenchmarkDialog::setRunningState(bool running) {
    isRunning = running;

    if (cpuTestBtn) {
        cpuTestBtn->setEnabled(!running);
    }
    if (openClTestBtn) {
        openClTestBtn->setEnabled(!running);
    }
    if (cudaTestBtn) {
        cudaTestBtn->setEnabled(!running);
    }
    if (autoTestBtn) {
        autoTestBtn->setEnabled(!running);
    }
    if (cancelBtn) {
        cancelBtn->setEnabled(running);
    }
    if (closeBtn) {
        closeBtn->setEnabled(!running);
    }

    bool hasAnyResults = false;
    for (auto it = backendResults.cbegin(); it != backendResults.cend(); ++it) {
        if (it.value().hasData) {
            hasAnyResults = true;
            break;
        }
    }
    if (saveCsvBtn) {
        saveCsvBtn->setEnabled(hasAnyResults);
    }
}

void ComputeBenchmarkDialog::startCpuBenchmark() {
    startBenchmarks({kBackendCpu}, RunMode::SingleBackend, "Single backend (CPU)");
}

void ComputeBenchmarkDialog::startOpenClBenchmark() {
    startBenchmarks({kBackendOpenCl}, RunMode::SingleBackend, "Single backend (OpenCL)");
}

void ComputeBenchmarkDialog::startCudaBenchmark() {
    startBenchmarks({kBackendCuda}, RunMode::SingleBackend, "Single backend (CUDA)");
}

void ComputeBenchmarkDialog::startAutoBenchmark() {
    startBenchmarks({kBackendCpu, kBackendOpenCl, kBackendCuda}, RunMode::AutoAll, "Auto test (CPU + OpenCL + CUDA)");
}

void ComputeBenchmarkDialog::startBenchmarks(const QVector<int> &backendIds, RunMode runMode, const QString &modeLabel) {
    if (isRunning || backendIds.isEmpty()) {
        return;
    }

    currentRunMode = runMode;
    currentRunModeLabel = modeLabel;
    sessionStartedAt = QDateTime::currentDateTime();
    totalSteps = 0;

    for (int backendId : backendIds) {
        resetBackendRow(backendId);
    }

    updatePreviewLabelText("Preview appears after each timed run.");
    previewInfoLabel->setText("Benchmark in progress...");
    statusLabel->setText(QString("Starting %1...").arg(modeLabel));
    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    setRunningState(true);

    if (workerThread) {
        clearThread();
    }

    workerThread = new QThread();
    BenchmarkWorker *worker = new BenchmarkWorker();
    worker->moveToThread(workerThread);
    workerObject = worker;

    BenchmarkWorker::Request request;
    request.backendIds = backendIds;
    request.openClPlatformIndex = openClPlatformIndex;
    request.openClDeviceIndex = openClDeviceIndex;
    request.cudaDeviceIndex = cudaDeviceIndex;
    request.warmupRuns = kWarmupRuns;
    request.timedRuns = kTimedRuns;
    request.modeLabel = modeLabel;

    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &BenchmarkWorker::runStarted, this, &ComputeBenchmarkDialog::onWorkerRunStarted);
    connect(worker, &BenchmarkWorker::progress, this, &ComputeBenchmarkDialog::onWorkerProgress);
    connect(worker, &BenchmarkWorker::backendStatus, this, &ComputeBenchmarkDialog::onWorkerBackendStatus);
    connect(worker, &BenchmarkWorker::timedRunResult, this, &ComputeBenchmarkDialog::onWorkerTimedRun);
    connect(worker, &BenchmarkWorker::backendDone, this, &ComputeBenchmarkDialog::onWorkerBackendDone);
    connect(worker, &BenchmarkWorker::runFinished, this, &ComputeBenchmarkDialog::onWorkerFinished);

    connect(workerThread, &QThread::started, worker, [worker, request]() { worker->run(request); });
    workerThread->start();
}

void ComputeBenchmarkDialog::requestCancel() {
    if (!isRunning || !workerObject) {
        return;
    }

    statusLabel->setText("Cancellation requested. Waiting for current run to finish...");
    cancelBtn->setEnabled(false);
    QMetaObject::invokeMethod(workerObject, "cancel", Qt::QueuedConnection);
}

void ComputeBenchmarkDialog::onWorkerRunStarted(int workerTotalSteps, const QString &modeLabel) {
    totalSteps = std::max(1, workerTotalSteps);
    progressBar->setRange(0, totalSteps);
    progressBar->setValue(0);
    statusLabel->setText(QString("Running %1...").arg(modeLabel));
}

void ComputeBenchmarkDialog::onWorkerProgress(const QString &statusText, int completedSteps) {
    statusLabel->setText(statusText);
    progressBar->setValue(std::min(std::max(0, completedSteps), totalSteps));
}

void ComputeBenchmarkDialog::onWorkerBackendStatus(int backendId, const QString &status, const QString &notes) {
    BackendSummaryRecord &summary = backendResults[backendId];
    summary.hasData = true;
    summary.status = status;
    summary.requestedBackend = backendName(backendId);
    summary.notes = notes;
    summary.warmupRuns = kWarmupRuns;
    summary.timedRunsRequested = kTimedRuns;
    summary.updatedAt = QDateTime::currentDateTime();
    updateRowFromSummary(backendId);
}

void ComputeBenchmarkDialog::onWorkerTimedRun(int backendId,
                                              int runIndex,
                                              qint64 elapsedMs,
                                              double msPerIteration,
                                              const QImage &phaseMaskPreview,
                                              bool strictPass,
                                              int backendUsedId,
                                              const QString &backendInfo,
                                              bool fallbackOccurred,
                                              const QString &fallbackReason,
                                              const QString &error) {
    BackendSummaryRecord &summary = backendResults[backendId];
    summary.hasData = true;
    summary.usedBackend = backendUsedName(backendUsedId);
    summary.backendInfo = backendInfo;
    summary.updatedAt = QDateTime::currentDateTime();
    summary.warmupRuns = kWarmupRuns;
    summary.timedRunsRequested = kTimedRuns;

    TimedRunRecord runRecord;
    runRecord.runIndex = runIndex;
    runRecord.elapsedMs = elapsedMs;
    runRecord.msPerIteration = msPerIteration;
    runRecord.strictPass = strictPass;
    runRecord.backendUsed = summary.usedBackend;
    runRecord.backendInfo = backendInfo;
    runRecord.fallbackOccurred = fallbackOccurred;
    runRecord.fallbackReason = fallbackReason;
    runRecord.error = error;
    summary.timedRuns.append(runRecord);
    summary.completedTimedRuns = summary.timedRuns.size();

    if (!error.isEmpty()) {
        summary.notes = error;
    }

    if (!phaseMaskPreview.isNull()) {
        const QImage preview = phaseMaskPreview.convertToFormat(QImage::Format_Grayscale8);
        const QPixmap scaled = QPixmap::fromImage(preview).scaled(previewImageLabel->size(),
                                                                  Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation);
        previewImageLabel->setPixmap(scaled);
        previewImageLabel->setText(QString());
        previewInfoLabel->setText(QString("%1 | timed run %2 | %3 ms (%4 ms/iter)%5")
                                      .arg(backendName(backendId))
                                      .arg(runIndex)
                                      .arg(elapsedMs)
                                      .arg(formatDouble(msPerIteration))
                                      .arg(strictPass ? QString() : QString(" | strict check failed")));
    }

    updateRowFromSummary(backendId);
    if (saveCsvBtn) {
        saveCsvBtn->setEnabled(true);
    }
}

void ComputeBenchmarkDialog::onWorkerBackendDone(int backendId,
                                                 bool passed,
                                                 const QString &status,
                                                 const QString &requestedBackend,
                                                 const QString &usedBackend,
                                                 const QString &backendInfo,
                                                 const QString &notes,
                                                 double meanMs,
                                                 double minMs,
                                                 double maxMs,
                                                 double stddevMs,
                                                 double meanMsPerIter,
                                                 int completedTimedRuns,
                                                 bool hadFailure) {
    BackendSummaryRecord &summary = backendResults[backendId];
    summary.hasData = true;
    summary.passed = passed;
    summary.hadFailure = hadFailure;
    summary.status = status;
    summary.requestedBackend = requestedBackend;
    summary.usedBackend = usedBackend;
    summary.backendInfo = backendInfo;
    summary.notes = notes;
    summary.meanMs = meanMs;
    summary.minMs = minMs;
    summary.maxMs = maxMs;
    summary.stddevMs = stddevMs;
    summary.meanMsPerIter = meanMsPerIter;
    summary.completedTimedRuns = completedTimedRuns;
    summary.warmupRuns = kWarmupRuns;
    summary.timedRunsRequested = kTimedRuns;
    summary.updatedAt = QDateTime::currentDateTime();

    updateRowFromSummary(backendId);
    if (saveCsvBtn) {
        saveCsvBtn->setEnabled(true);
    }
}

void ComputeBenchmarkDialog::onWorkerFinished(bool cancelled, const QString &message) {
    if (!cancelled) {
        progressBar->setValue(totalSteps);
    }
    statusLabel->setText(message);
    setRunningState(false);
    clearThread();
}

void ComputeBenchmarkDialog::saveCsv() {
    bool hasAnyResults = false;
    for (auto it = backendResults.cbegin(); it != backendResults.cend(); ++it) {
        if (it.value().hasData) {
            hasAnyResults = true;
            break;
        }
    }

    if (!hasAnyResults) {
        QMessageBox::information(this, "Save CSV", "No benchmark results are available to export yet.");
        return;
    }

    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultName =
        QDir(defaultDir).filePath("GS_Benchmark_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv");
    const QString fileName = QFileDialog::getSaveFileName(this, "Save Benchmark CSV", defaultName, "CSV Files (*.csv)");
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save CSV", "Failed to open output file for writing.");
        return;
    }

    QTextStream out(&file);
    out << "section,key,value\n";

    auto writeMeta = [&](const QString &key, const QString &value) {
        out << csvEscape("metadata") << "," << csvEscape(key) << "," << csvEscape(value) << "\n";
    };
    auto writeCanonical = [&](const QString &key, const QString &value) {
        out << csvEscape("canonical_workload") << "," << csvEscape(key) << "," << csvEscape(value) << "\n";
    };

    const QString appVersion = QCoreApplication::applicationVersion().isEmpty()
        ? "unknown"
        : QCoreApplication::applicationVersion();

    writeMeta("timestamp_local", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    writeMeta("session_started_local", sessionStartedAt.isValid() ? sessionStartedAt.toString(Qt::ISODateWithMs) : "<none>");
    writeMeta("run_mode", currentRunModeLabel);
    writeMeta("warmup_runs", QString::number(kWarmupRuns));
    writeMeta("timed_runs", QString::number(kTimedRuns));
    writeMeta("app_name", QCoreApplication::applicationName());
    writeMeta("app_version", appVersion);
    writeMeta("qt_version", QString::fromLatin1(QT_VERSION_STR));
    writeMeta("build_hot_enable_temp_gs_profiling", QString::number(HOT_ENABLE_TEMP_GS_PROFILING));
    writeMeta("build_hot_enable_opencl_gs", QString::number(HOT_ENABLE_OPENCL_GS));
    writeMeta("build_hot_enable_cuda_gs", QString::number(HOT_ENABLE_CUDA_GS));

    writeCanonical("algorithm", "Gerchberg-Saxton");
    writeCanonical("slm_width", QString::number(kCanonicalSlmWidth));
    writeCanonical("slm_height", QString::number(kCanonicalSlmHeight));
    writeCanonical("camera_width", QString::number(kCanonicalCamWidth));
    writeCanonical("camera_height", QString::number(kCanonicalCamHeight));
    writeCanonical("iterations", QString::number(kCanonicalIterations));
    writeCanonical("starting_phase_mask", "Checkerboard");
    writeCanonical("target_pattern", "Circle");
    writeCanonical("target_radius_px", QString::number(kCanonicalTargetRadiusPx, 'f', 1));
    writeCanonical("target_spots", QString::number(kCanonicalTargetSpots));
    writeCanonical("target_rotation_deg", "0");
    writeCanonical("target_shift_x_px", "0");
    writeCanonical("target_shift_y_px", "0");
    writeCanonical("source_mode", "Fixed Gaussian");
    writeCanonical("source_beam_waist_px", QString::number(static_cast<double>(std::min(kCanonicalSlmWidth, kCanonicalSlmHeight)) / 6.0, 'f', 3));
    writeCanonical("slm_pixel_size_um", QString::number(kCanonicalSlmPixelSizeUm, 'f', 4));
    writeCanonical("camera_pixel_size_um", QString::number(kCanonicalCamPixelSizeUm, 'f', 4));
    writeCanonical("wavelength_nm", QString::number(kCanonicalWavelengthNm, 'f', 4));
    writeCanonical("focal_length_mm", QString::number(kCanonicalFocalLengthMm, 'f', 4));
    writeCanonical("opencl_platform_index", QString::number(openClPlatformIndex));
    writeCanonical("opencl_device_index", QString::number(openClDeviceIndex));
    writeCanonical("cuda_device_index", QString::number(cudaDeviceIndex));

    out << "\n";
    out << "backend,status,requested_backend,used_backend,passed,warmup_runs,timed_runs_requested,timed_runs_completed,mean_ms,min_ms,max_ms,stddev_ms,mean_ms_per_iter,backend_info,notes,updated_at_local\n";

    const QVector<int> backendIds = {kBackendCpu, kBackendOpenCl, kBackendCuda};
    for (int backendId : backendIds) {
        const BackendSummaryRecord summary = backendResults.value(backendId);
        out << csvEscape(backendName(backendId)) << ","
            << csvEscape(summary.hasData ? summary.status : "Not Run") << ","
            << csvEscape(summary.hasData ? summary.requestedBackend : backendName(backendId)) << ","
            << csvEscape(summary.hasData ? summary.usedBackend : "<none>") << ","
            << csvEscape(summary.hasData ? (summary.passed ? "true" : "false") : "false") << ","
            << csvEscape(QString::number(summary.warmupRuns)) << ","
            << csvEscape(QString::number(summary.timedRunsRequested)) << ","
            << csvEscape(QString::number(summary.completedTimedRuns)) << ","
            << csvEscape(summary.completedTimedRuns > 0 ? formatDouble(summary.meanMs) : QString()) << ","
            << csvEscape(summary.completedTimedRuns > 0 ? formatDouble(summary.minMs) : QString()) << ","
            << csvEscape(summary.completedTimedRuns > 0 ? formatDouble(summary.maxMs) : QString()) << ","
            << csvEscape(summary.completedTimedRuns > 0 ? formatDouble(summary.stddevMs) : QString()) << ","
            << csvEscape(summary.completedTimedRuns > 0 ? formatDouble(summary.meanMsPerIter) : QString()) << ","
            << csvEscape(summary.backendInfo) << ","
            << csvEscape(summary.notes) << ","
            << csvEscape(summary.updatedAt.isValid() ? summary.updatedAt.toString(Qt::ISODateWithMs) : QString())
            << "\n";
    }

    out << "\n";
    out << "backend,run_index,elapsed_ms,ms_per_iteration,strict_pass,backend_used,backend_info,fallback_occurred,fallback_reason,error\n";
    for (int backendId : backendIds) {
        const BackendSummaryRecord summary = backendResults.value(backendId);
        for (const TimedRunRecord &run : summary.timedRuns) {
            out << csvEscape(backendName(backendId)) << ","
                << csvEscape(QString::number(run.runIndex)) << ","
                << csvEscape(QString::number(run.elapsedMs)) << ","
                << csvEscape(formatDouble(run.msPerIteration)) << ","
                << csvEscape(run.strictPass ? "true" : "false") << ","
                << csvEscape(run.backendUsed) << ","
                << csvEscape(run.backendInfo) << ","
                << csvEscape(run.fallbackOccurred ? "true" : "false") << ","
                << csvEscape(run.fallbackReason) << ","
                << csvEscape(run.error) << "\n";
        }
    }

    file.close();
    QMessageBox::information(this, "Save CSV", "Benchmark results were exported successfully.");
}

void ComputeBenchmarkDialog::resetBackendRow(int backendId) {
    backendResults.remove(backendId);
    const int row = rowForBackend(backendId);
    if (row < 0) {
        return;
    }

    auto setCellText = [&](int col, const QString &text) {
        QTableWidgetItem *item = resultTable->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            resultTable->setItem(row, col, item);
        }
        item->setText(text);
    };

    setCellText(1, "Idle");
    setCellText(2, "-");
    setCellText(3, "-");
    setCellText(4, "-");
    setCellText(5, "-");
    setCellText(6, "-");
    setCellText(7, "-");
    setCellText(8, "-");
    setCellText(9, "No result yet.");
}

int ComputeBenchmarkDialog::rowForBackend(int backendId) const {
    switch (backendId) {
    case kBackendCpu:
        return 0;
    case kBackendOpenCl:
        return 1;
    case kBackendCuda:
        return 2;
    default:
        return -1;
    }
}

QString ComputeBenchmarkDialog::backendName(int backendId) const {
    switch (backendId) {
    case kBackendOpenCl:
        return "OpenCL";
    case kBackendCuda:
        return "CUDA";
    case kBackendCpu:
    default:
        return "CPU";
    }
}

QString ComputeBenchmarkDialog::backendUsedName(int backendUsedId) const {
    switch (backendUsedId) {
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::OpenCL):
        return "OpenCL";
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CUDA):
        return "CUDA";
    case static_cast<int>(GSAlgorithm::GSComputeBackendUsed::CPU):
    default:
        return "CPU";
    }
}

QString ComputeBenchmarkDialog::csvEscape(const QString &value) const {
    QString escaped = value;
    escaped.replace('"', "\"\"");
    if (escaped.contains(',') || escaped.contains('\n') || escaped.contains('\r') || escaped.contains('"')) {
        return "\"" + escaped + "\"";
    }
    return escaped;
}

void ComputeBenchmarkDialog::refreshCanonicalInfoText() {
    const double beamWaistPx = static_cast<double>(std::min(kCanonicalSlmWidth, kCanonicalSlmHeight)) / 6.0;
    canonicalInfoLabel->setText(
        QString("Algorithm: Gerchberg-Saxton\n"
                "SLM: %1 x %2\n"
                "Camera: %3 x %4\n"
                "Iterations: %5\n"
                "Starting phase mask: Checkerboard\n"
                "Target pattern: Circle, radius %6 px, spots %7, rotation 0 deg, shift (0, 0)\n"
                "Source: Fixed Gaussian, beam waist %8 px\n"
                "Optics: SLM pixel %9 um, Camera pixel %10 um, Wavelength %11 nm, Focal length %12 mm\n"
                "Device selection from Hardware Settings: OpenCL P%13:D%14, CUDA D%15\n"
                "Run policy: 1 warmup + 5 timed runs per backend; strict backend validation enabled.")
            .arg(kCanonicalSlmWidth)
            .arg(kCanonicalSlmHeight)
            .arg(kCanonicalCamWidth)
            .arg(kCanonicalCamHeight)
            .arg(kCanonicalIterations)
            .arg(kCanonicalTargetRadiusPx, 0, 'f', 1)
            .arg(kCanonicalTargetSpots)
            .arg(beamWaistPx, 0, 'f', 3)
            .arg(kCanonicalSlmPixelSizeUm, 0, 'f', 4)
            .arg(kCanonicalCamPixelSizeUm, 0, 'f', 4)
            .arg(kCanonicalWavelengthNm, 0, 'f', 4)
            .arg(kCanonicalFocalLengthMm, 0, 'f', 4)
            .arg(openClPlatformIndex)
            .arg(openClDeviceIndex)
            .arg(cudaDeviceIndex));
}

void ComputeBenchmarkDialog::updateRowFromSummary(int backendId) {
    const int row = rowForBackend(backendId);
    if (row < 0) {
        return;
    }

    const BackendSummaryRecord summary = backendResults.value(backendId);
    auto setCellText = [&](int col, const QString &text) {
        QTableWidgetItem *item = resultTable->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            resultTable->setItem(row, col, item);
        }
        item->setText(text);
    };

    setCellText(1, summary.status.isEmpty() ? "Idle" : summary.status);
    setCellText(2, QString("%1/%2").arg(summary.completedTimedRuns).arg(summary.timedRunsRequested));
    setCellText(3, summary.completedTimedRuns > 0 ? formatDouble(summary.meanMs) : "-");
    setCellText(4, summary.completedTimedRuns > 0 ? formatDouble(summary.minMs) : "-");
    setCellText(5, summary.completedTimedRuns > 0 ? formatDouble(summary.maxMs) : "-");
    setCellText(6, summary.completedTimedRuns > 0 ? formatDouble(summary.stddevMs) : "-");
    setCellText(7, summary.completedTimedRuns > 0 ? formatDouble(summary.meanMsPerIter) : "-");
    setCellText(8, summary.usedBackend.isEmpty() ? "-" : summary.usedBackend);
    setCellText(9, summary.notes.isEmpty() ? "-" : summary.notes);
}

void ComputeBenchmarkDialog::updatePreviewLabelText(const QString &text) {
    previewImageLabel->setPixmap(QPixmap());
    previewImageLabel->setText(text);
}

void ComputeBenchmarkDialog::clearThread() {
    if (!workerThread) {
        workerObject = nullptr;
        return;
    }

    QThread *thread = workerThread;
    workerThread = nullptr;
    workerObject = nullptr;

    thread->quit();
    thread->wait();
    delete thread;
}

#include "computebenchmarkdialog.moc"
