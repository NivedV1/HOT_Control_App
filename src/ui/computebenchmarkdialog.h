#ifndef COMPUTEBENCHMARKDIALOG_H
#define COMPUTEBENCHMARKDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QtGlobal>
#include <QVector>

class QLabel;
class QImage;
class QObject;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

class ComputeBenchmarkDialog : public QDialog {
    Q_OBJECT

public:
    explicit ComputeBenchmarkDialog(int openClPlatformIndex,
                                    int openClDeviceIndex,
                                    int cudaDeviceIndex,
                                    QWidget *parent = nullptr);
    ~ComputeBenchmarkDialog() override;

    void setDeviceSelectionIndices(int openClPlatformIndex,
                                   int openClDeviceIndex,
                                   int cudaDeviceIndex);

private slots:
    void startCpuBenchmark();
    void startOpenClBenchmark();
    void startCudaBenchmark();
    void startAutoBenchmark();
    void requestCancel();
    void saveCsv();

    void onWorkerRunStarted(int totalSteps, const QString &modeLabel);
    void onWorkerProgress(const QString &statusText, int completedSteps);
    void onWorkerBackendStatus(int backendId, const QString &status, const QString &notes);
    void onWorkerTimedRun(int backendId,
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
    void onWorkerBackendDone(int backendId,
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
    void onWorkerFinished(bool cancelled, const QString &message);

private:
    struct TimedRunRecord {
        int runIndex = 0;
        qint64 elapsedMs = 0;
        double msPerIteration = 0.0;
        bool strictPass = false;
        QString backendUsed;
        QString backendInfo;
        bool fallbackOccurred = false;
        QString fallbackReason;
        QString error;
    };

    struct BackendSummaryRecord {
        bool hasData = false;
        bool passed = false;
        bool hadFailure = false;
        QString status;
        QString requestedBackend;
        QString usedBackend;
        QString backendInfo;
        QString notes;
        double meanMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double stddevMs = 0.0;
        double meanMsPerIter = 0.0;
        int completedTimedRuns = 0;
        int warmupRuns = 1;
        int timedRunsRequested = 5;
        QVector<TimedRunRecord> timedRuns;
        QDateTime updatedAt;
    };

    enum class RunMode {
        None = 0,
        SingleBackend = 1,
        AutoAll = 2
    };

    void buildUi();
    void initializeTable();
    void setRunningState(bool running);
    void startBenchmarks(const QVector<int> &backendIds, RunMode runMode, const QString &modeLabel);
    void resetBackendRow(int backendId);
    int rowForBackend(int backendId) const;
    QString backendName(int backendId) const;
    QString backendUsedName(int backendUsedId) const;
    QString csvEscape(const QString &value) const;
    void refreshCanonicalInfoText();
    void updateRowFromSummary(int backendId);
    void updatePreviewLabelText(const QString &text);
    void clearThread();

    int openClPlatformIndex = 0;
    int openClDeviceIndex = 0;
    int cudaDeviceIndex = 0;

    QLabel *canonicalInfoLabel = nullptr;
    QTableWidget *resultTable = nullptr;
    QLabel *previewInfoLabel = nullptr;
    QLabel *previewImageLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QProgressBar *progressBar = nullptr;

    QPushButton *cpuTestBtn = nullptr;
    QPushButton *openClTestBtn = nullptr;
    QPushButton *cudaTestBtn = nullptr;
    QPushButton *autoTestBtn = nullptr;
    QPushButton *cancelBtn = nullptr;
    QPushButton *saveCsvBtn = nullptr;
    QPushButton *closeBtn = nullptr;

    QThread *workerThread = nullptr;
    QObject *workerObject = nullptr;
    bool isRunning = false;
    int totalSteps = 0;
    RunMode currentRunMode = RunMode::None;
    QString currentRunModeLabel = "None";
    QDateTime sessionStartedAt;

    QHash<int, BackendSummaryRecord> backendResults;
};

#endif // COMPUTEBENCHMARKDIALOG_H
