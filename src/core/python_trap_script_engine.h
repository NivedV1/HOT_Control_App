#ifndef PYTHON_TRAP_SCRIPT_ENGINE_H
#define PYTHON_TRAP_SCRIPT_ENGINE_H

#include <QPointF>
#include <QString>
#include <QVector>

struct PythonTrapScriptResult {
    bool success = false;
    QString errorMessage;
    QString warningMessage;
    QVector<QVector<QPointF>> frames;
};

class PythonTrapScriptEngine {
public:
    PythonTrapScriptEngine();
    ~PythonTrapScriptEngine();

    bool isReady() const;
    QString initError() const;

    PythonTrapScriptResult runScript(const QString &script,
                                     int requestedFrameCount,
                                     int cameraWidth,
                                     int cameraHeight,
                                     int maxPointsPerFrame) const;

private:
    bool initialized = false;
    QString initErrorMessage;
};

#endif // PYTHON_TRAP_SCRIPT_ENGINE_H
