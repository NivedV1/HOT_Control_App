#include "python_trap_script_engine.h"
#include "patterngenerator.h"

#include <QtGlobal>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <string>

#define PY_SSIZE_T_CLEAN
#ifdef _DEBUG
#define HOT_RESTORE_MSVC_DEBUG 1
#undef _DEBUG
#endif
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#ifdef HOT_RESTORE_MSVC_DEBUG
#define _DEBUG 1
#undef HOT_RESTORE_MSVC_DEBUG
#endif

namespace {
thread_local int gHelperCameraWidth = 1920;
thread_local int gHelperCameraHeight = 1080;

QString macroOrEmpty(const char *value) {
    return value ? QString::fromUtf8(value) : QString();
}

bool appendSearchPathIfExists(QStringList &paths, const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    paths.append(QDir::toNativeSeparators(path));
    return true;
}

bool initializePythonRuntime(QString &errorOut) {
    if (Py_IsInitialized()) {
        return true;
    }

#if defined(HOT_EMBED_PYTHON_HOME)
    const QString pythonHome = macroOrEmpty(HOT_EMBED_PYTHON_HOME);
#else
    const QString pythonHome;
#endif
#if defined(HOT_EMBED_PYTHON_LIB)
    const QString pythonLib = macroOrEmpty(HOT_EMBED_PYTHON_LIB);
#else
    const QString pythonLib;
#endif
#if defined(HOT_EMBED_PYTHON_SITE_PACKAGES)
    const QString pythonSitePackages = macroOrEmpty(HOT_EMBED_PYTHON_SITE_PACKAGES);
#else
    const QString pythonSitePackages;
#endif
#if defined(HOT_EMBED_PYTHON_DLLS)
    const QString pythonDlls = macroOrEmpty(HOT_EMBED_PYTHON_DLLS);
#else
    const QString pythonDlls;
#endif
#if defined(HOT_EMBED_PYTHON_ZIP)
    const QString pythonZip = macroOrEmpty(HOT_EMBED_PYTHON_ZIP);
#else
    const QString pythonZip;
#endif

    PyStatus status;
    PyConfig config;
    PyConfig_InitPythonConfig(&config);

    const std::wstring programName = L"HOT_Control_App";
    status = PyConfig_SetString(&config, &config.program_name, programName.c_str());
    if (PyStatus_Exception(status)) {
        errorOut = QString::fromUtf8(status.err_msg ? status.err_msg : "Failed to set Python program name.");
        PyConfig_Clear(&config);
        return false;
    }

    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString appPythonHome = appDir;
    const QString appPythonLib = QDir(appDir).filePath("Lib");
    const QString appPythonSitePackages = QDir(appPythonLib).filePath("site-packages");
    const QString appPythonDlls = QDir(appDir).filePath("DLLs");
    const QString appPythonZip = QDir(appDir).filePath("python312.zip");

    const bool appBundlePresent = QFileInfo::exists(appPythonLib) || QFileInfo::exists(appPythonZip);
    const QString pythonHomeSelected = appBundlePresent ? appPythonHome : pythonHome;

    const std::wstring homeWs = QDir::toNativeSeparators(pythonHomeSelected).toStdWString();
    status = PyConfig_SetString(&config, &config.home, homeWs.c_str());
    if (PyStatus_Exception(status)) {
        errorOut = QString::fromUtf8(status.err_msg ? status.err_msg : "Failed to set Python home.");
        PyConfig_Clear(&config);
        return false;
    }

    QStringList modulePaths;
    if (appBundlePresent) {
        appendSearchPathIfExists(modulePaths, appPythonZip);
        appendSearchPathIfExists(modulePaths, appPythonDlls);
        appendSearchPathIfExists(modulePaths, appPythonLib);
        appendSearchPathIfExists(modulePaths, appPythonSitePackages);
        appendSearchPathIfExists(modulePaths, appPythonHome);
    } else {
        appendSearchPathIfExists(modulePaths, pythonZip);
        appendSearchPathIfExists(modulePaths, pythonDlls);
        appendSearchPathIfExists(modulePaths, pythonLib);
        appendSearchPathIfExists(modulePaths, pythonSitePackages);
        appendSearchPathIfExists(modulePaths, pythonHome);
    }

    config.module_search_paths_set = 1;
    for (const QString &path : modulePaths) {
        const std::wstring w = path.toStdWString();
        status = PyWideStringList_Append(&config.module_search_paths, w.c_str());
        if (PyStatus_Exception(status)) {
            errorOut = QString::fromUtf8(status.err_msg ? status.err_msg : "Failed to configure Python module search paths.");
            PyConfig_Clear(&config);
            return false;
        }
    }

    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        errorOut = QString::fromUtf8(status.err_msg ? status.err_msg : "Failed to initialize embedded Python runtime.");
        PyConfig_Clear(&config);
        return false;
    }

    PyConfig_Clear(&config);
    return true;
}

QString pythonObjectToQString(PyObject *obj) {
    if (!obj) {
        return QString();
    }
    PyObject *textObj = PyObject_Str(obj);
    if (!textObj) {
        return QString();
    }
    const char *utf8 = PyUnicode_AsUTF8(textObj);
    QString out = utf8 ? QString::fromUtf8(utf8) : QString();
    Py_DECREF(textObj);
    return out;
}

QString fetchPythonExceptionText() {
    PyObject *typeObj = nullptr;
    PyObject *valueObj = nullptr;
    PyObject *tracebackObj = nullptr;
    PyErr_Fetch(&typeObj, &valueObj, &tracebackObj);
    PyErr_NormalizeException(&typeObj, &valueObj, &tracebackObj);

    QString message;
    PyObject *tracebackModule = PyImport_ImportModule("traceback");
    if (tracebackModule) {
        PyObject *formatException = PyObject_GetAttrString(tracebackModule, "format_exception");
        if (formatException && PyCallable_Check(formatException)) {
            PyObject *args = PyTuple_Pack(
                3,
                typeObj ? typeObj : Py_None,
                valueObj ? valueObj : Py_None,
                tracebackObj ? tracebackObj : Py_None);
            PyObject *formatted = args ? PyObject_CallObject(formatException, args) : nullptr;
            if (formatted) {
                PyObject *empty = PyUnicode_FromString("");
                PyObject *joined = empty ? PyUnicode_Join(empty, formatted) : nullptr;
                if (joined) {
                    const char *utf8 = PyUnicode_AsUTF8(joined);
                    if (utf8) {
                        message = QString::fromUtf8(utf8).trimmed();
                    }
                    Py_DECREF(joined);
                }
                Py_XDECREF(empty);
                Py_DECREF(formatted);
            }
            Py_XDECREF(args);
        }
        Py_XDECREF(formatException);
        Py_DECREF(tracebackModule);
    }

    if (message.isEmpty()) {
        message = pythonObjectToQString(valueObj).trimmed();
    }
    if (message.isEmpty()) {
        message = "Unknown Python error.";
    }

    Py_XDECREF(typeObj);
    Py_XDECREF(valueObj);
    Py_XDECREF(tracebackObj);
    return message;
}

bool convertPointObject(PyObject *pointObj, double &xOut, double &yOut) {
    if (!pointObj || !PySequence_Check(pointObj)) {
        return false;
    }

    const Py_ssize_t pointSize = PySequence_Size(pointObj);
    if (pointSize < 2) {
        return false;
    }

    PyObject *xObj = PySequence_GetItem(pointObj, 0);
    PyObject *yObj = PySequence_GetItem(pointObj, 1);
    if (!xObj || !yObj || (!PyNumber_Check(xObj)) || (!PyNumber_Check(yObj))) {
        Py_XDECREF(xObj);
        Py_XDECREF(yObj);
        return false;
    }

    const double x = PyFloat_AsDouble(xObj);
    const double y = PyFloat_AsDouble(yObj);
    const bool hasError = PyErr_Occurred() != nullptr;

    Py_DECREF(xObj);
    Py_DECREF(yObj);

    if (hasError) {
        PyErr_Clear();
        return false;
    }

    xOut = x;
    yOut = y;
    return true;
}

PyObject *pointsToPythonList(const QVector<QPointF> &points) {
    PyObject *frameList = PyList_New(points.size());
    if (!frameList) {
        return nullptr;
    }

    for (int i = 0; i < points.size(); ++i) {
        const QPointF &p = points.at(i);
        PyObject *pair = Py_BuildValue("[dd]", p.x(), p.y());
        if (!pair) {
            Py_DECREF(frameList);
            return nullptr;
        }
        PyList_SET_ITEM(frameList, i, pair);
    }
    return frameList;
}

QVector<QPointF> generatePatternPoints(const PatternGenerator::PatternRequest &request) {
    return PatternGenerator::generate(request);
}

PyObject *pyPatternCircle(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "radius", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 12;
    double radius = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|idddd", const_cast<char **>(kwlist),
                                     &pointCount, &radius, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    if (radius <= 0.0) {
        radius = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.25;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Circle;
    request.pointCount = qMax(3, pointCount);
    request.radius = radius;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternTriangle(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "scale", "rotation_deg", "x_shift", "y_shift", "symmetric", nullptr};
    int pointCount = 9;
    double scale = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;
    int symmetric = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iddddp", const_cast<char **>(kwlist),
                                     &pointCount, &scale, &rotationDeg, &xShift, &yShift, &symmetric)) {
        return nullptr;
    }

    if (scale <= 0.0) {
        scale = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.25;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Triangle;
    request.pointCount = qMax(3, pointCount);
    request.scale = scale;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;
    request.symmetric = (symmetric != 0);

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternSquare(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "size", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 12;
    double size = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|idddd", const_cast<char **>(kwlist),
                                     &pointCount, &size, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    if (size <= 0.0) {
        size = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.35;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Square;
    request.pointCount = qMax(4, pointCount);
    request.size = size;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternGrid(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"rows", "cols", "row_spacing", "col_spacing", "rotation_deg", "x_shift", "y_shift", "center", nullptr};
    int rows = 4;
    int cols = 4;
    double rowSpacing = -1.0;
    double colSpacing = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;
    int center = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iidddddp", const_cast<char **>(kwlist),
                                     &rows, &cols, &rowSpacing, &colSpacing, &rotationDeg, &xShift, &yShift, &center)) {
        return nullptr;
    }

    const double defaultSpacing = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.08;
    if (rowSpacing <= 0.0) {
        rowSpacing = defaultSpacing;
    }
    if (colSpacing <= 0.0) {
        colSpacing = defaultSpacing;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Grid;
    request.gridRows = qMax(1, rows);
    request.gridCols = qMax(1, cols);
    request.gridRowSpacing = rowSpacing;
    request.gridColSpacing = colSpacing;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;
    request.centerGridAtOrigin = (center != 0);
    request.pointCount = request.gridRows * request.gridCols;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternRectangle(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "width", "height", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 16;
    double width = -1.0;
    double height = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iddddd", const_cast<char **>(kwlist),
                                     &pointCount, &width, &height, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    const double base = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight));
    if (width <= 0.0) {
        width = base * 0.45;
    }
    if (height <= 0.0) {
        height = base * 0.25;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Rectangle;
    request.pointCount = qMax(4, pointCount);
    request.width = width;
    request.height = height;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternHexagon(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "radius", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 18;
    double radius = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|idddd", const_cast<char **>(kwlist),
                                     &pointCount, &radius, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    if (radius <= 0.0) {
        radius = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.25;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Hexagon;
    request.pointCount = qMax(6, pointCount);
    request.radius = radius;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternTwoSpots(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"distance", "rotation_deg", "x_shift", "y_shift", nullptr};
    double distance = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|dddd", const_cast<char **>(kwlist),
                                     &distance, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    if (distance <= 0.0) {
        distance = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight)) * 0.18;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::TwoSpots;
    request.distance = distance;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;
    request.pointCount = 2;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternStar(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "star_points", "outer_radius", "inner_radius", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 20;
    int starPoints = 5;
    double outerRadius = -1.0;
    double innerRadius = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iiddddd", const_cast<char **>(kwlist),
                                     &pointCount, &starPoints, &outerRadius, &innerRadius, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    const double base = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight));
    if (outerRadius <= 0.0) {
        outerRadius = base * 0.30;
    }
    if (innerRadius <= 0.0) {
        innerRadius = outerRadius * 0.45;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::Star;
    request.pointCount = qMax(6, pointCount);
    request.starPoints = qMax(3, starPoints);
    request.radius = outerRadius;
    request.innerRadius = innerRadius;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

PyObject *pyPatternPlanetMoon(PyObject *, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {"point_count", "planet_radius", "moon_radius", "distance", "rotation_deg", "x_shift", "y_shift", nullptr};
    int pointCount = 20;
    double planetRadius = -1.0;
    double moonRadius = -1.0;
    double distance = -1.0;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|idddddd", const_cast<char **>(kwlist),
                                     &pointCount, &planetRadius, &moonRadius, &distance, &rotationDeg, &xShift, &yShift)) {
        return nullptr;
    }

    const double base = static_cast<double>(qMin(gHelperCameraWidth, gHelperCameraHeight));
    if (planetRadius <= 0.0) {
        planetRadius = base * 0.18;
    }
    if (moonRadius <= 0.0) {
        moonRadius = base * 0.08;
    }
    if (distance <= 0.0) {
        distance = base * 0.35;
    }

    PatternGenerator::PatternRequest request;
    request.preset = PatternGenerator::Preset::PlanetAndMoon;
    request.pointCount = qMax(2, pointCount);
    request.radius = planetRadius;
    request.moonRadius = moonRadius;
    request.distance = distance;
    request.rotationDeg = rotationDeg;
    request.xShift = xShift;
    request.yShift = yShift;

    return pointsToPythonList(generatePatternPoints(request));
}

static PyMethodDef kPatternMethods[] = {
    {"circle", reinterpret_cast<PyCFunction>(pyPatternCircle), METH_VARARGS | METH_KEYWORDS,
     "circle(point_count=12, radius=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"triangle", reinterpret_cast<PyCFunction>(pyPatternTriangle), METH_VARARGS | METH_KEYWORDS,
     "triangle(point_count=9, scale=None, rotation_deg=0, x_shift=0, y_shift=0, symmetric=True) -> [[x,y], ...]"},
    {"square", reinterpret_cast<PyCFunction>(pyPatternSquare), METH_VARARGS | METH_KEYWORDS,
     "square(point_count=12, size=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"rectangle", reinterpret_cast<PyCFunction>(pyPatternRectangle), METH_VARARGS | METH_KEYWORDS,
     "rectangle(point_count=16, width=None, height=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"hexagon", reinterpret_cast<PyCFunction>(pyPatternHexagon), METH_VARARGS | METH_KEYWORDS,
     "hexagon(point_count=18, radius=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"two_spots", reinterpret_cast<PyCFunction>(pyPatternTwoSpots), METH_VARARGS | METH_KEYWORDS,
     "two_spots(distance=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"star", reinterpret_cast<PyCFunction>(pyPatternStar), METH_VARARGS | METH_KEYWORDS,
     "star(point_count=20, star_points=5, outer_radius=None, inner_radius=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"planet_moon", reinterpret_cast<PyCFunction>(pyPatternPlanetMoon), METH_VARARGS | METH_KEYWORDS,
     "planet_moon(point_count=20, planet_radius=None, moon_radius=None, distance=None, rotation_deg=0, x_shift=0, y_shift=0) -> [[x,y], ...]"},
    {"grid", reinterpret_cast<PyCFunction>(pyPatternGrid), METH_VARARGS | METH_KEYWORDS,
     "grid(rows=4, cols=4, row_spacing=None, col_spacing=None, rotation_deg=0, x_shift=0, y_shift=0, center=True) -> [[x,y], ...]"},
    {nullptr, nullptr, 0, nullptr}
};

bool injectHotHelpers(PyObject *globals, QString &errorOut) {
    PyObject *patternModule = PyModule_New("pattern");
    if (!patternModule) {
        errorOut = "Failed to create hot.pattern helper module.";
        return false;
    }

    if (PyModule_AddFunctions(patternModule, kPatternMethods) != 0) {
        Py_DECREF(patternModule);
        errorOut = "Failed to register hot.pattern helper functions.";
        return false;
    }

    PyObject *hotModule = PyModule_New("hot");
    if (!hotModule) {
        Py_DECREF(patternModule);
        errorOut = "Failed to create hot helper module.";
        return false;
    }

    if (PyModule_AddObject(hotModule, "pattern", patternModule) != 0) {
        Py_DECREF(patternModule);
        Py_DECREF(hotModule);
        errorOut = "Failed to attach pattern helpers to hot.";
        return false;
    }

    if (PyDict_SetItemString(globals, "hot", hotModule) != 0) {
        Py_DECREF(hotModule);
        errorOut = "Failed to inject hot helpers into script globals.";
        return false;
    }

    Py_DECREF(hotModule);
    return true;
}

bool parseFrameObject(PyObject *frameObj,
                      int frameNumberOneBased,
                      int safeMaxPoints,
                      double halfWidth,
                      double halfHeight,
                      QVector<QPointF> &outFrame,
                      QString &errorOut,
                      QStringList &warnings,
                      int &clampedPoints) {
    if (!frameObj || !PySequence_Check(frameObj)) {
        errorOut = QString("Frame %1 is not a sequence.").arg(frameNumberOneBased);
        return false;
    }

    const Py_ssize_t pointCount = PySequence_Size(frameObj);
    if (pointCount <= 0) {
        errorOut = QString("Frame %1 has no trap points.").arg(frameNumberOneBased);
        return false;
    }

    const int usedPoints = qMin(static_cast<int>(pointCount), safeMaxPoints);
    if (pointCount > safeMaxPoints) {
        warnings << QString("Frame %1 returned %2 points; using first %3.")
                        .arg(frameNumberOneBased)
                        .arg(pointCount)
                        .arg(safeMaxPoints);
    }

    outFrame.clear();
    outFrame.reserve(usedPoints);

    for (int pointIndex = 0; pointIndex < usedPoints; ++pointIndex) {
        PyObject *pointObj = PySequence_GetItem(frameObj, pointIndex);
        if (!pointObj) {
            errorOut = QString("Point %1 in frame %2 could not be read.")
                           .arg(pointIndex + 1)
                           .arg(frameNumberOneBased);
            return false;
        }

        double x = 0.0;
        double y = 0.0;
        const bool ok = convertPointObject(pointObj, x, y);
        Py_DECREF(pointObj);
        if (!ok) {
            errorOut = QString("Point %1 in frame %2 must be [x, y] numeric coordinates.")
                           .arg(pointIndex + 1)
                           .arg(frameNumberOneBased);
            return false;
        }

        const double clampedX = qBound(-halfWidth, x, halfWidth);
        const double clampedY = qBound(-halfHeight, y, halfHeight);
        if (!qFuzzyCompare(clampedX + 1.0, x + 1.0) || !qFuzzyCompare(clampedY + 1.0, y + 1.0)) {
            ++clampedPoints;
        }

        outFrame.append(QPointF(clampedX, clampedY));
    }

    return true;
}

bool parseFramesObject(PyObject *framesObj,
                       int safeRequestedFrames,
                       int safeMaxPoints,
                       double halfWidth,
                       double halfHeight,
                       QVector<QVector<QPointF>> &outFrames,
                       QString &errorOut,
                       QStringList &warnings,
                       int &clampedPoints) {
    if (!framesObj || !PySequence_Check(framesObj)) {
        errorOut = "build_frames must return a sequence of frames.";
        return false;
    }

    const Py_ssize_t totalFrames = PySequence_Size(framesObj);
    if (totalFrames <= 0) {
        errorOut = "build_frames returned no frames.";
        return false;
    }

    const int usedFrames = qMin(static_cast<int>(totalFrames), safeRequestedFrames);
    if (totalFrames > safeRequestedFrames) {
        warnings << QString("Script returned %1 frames; using first %2.")
                        .arg(totalFrames)
                        .arg(safeRequestedFrames);
    }

    outFrames.clear();
    outFrames.reserve(usedFrames);

    for (int i = 0; i < usedFrames; ++i) {
        PyObject *frameObj = PySequence_GetItem(framesObj, i);
        if (!frameObj) {
            errorOut = QString("Frame %1 could not be read.").arg(i + 1);
            return false;
        }

        QVector<QPointF> frame;
        const bool ok = parseFrameObject(frameObj,
                                         i + 1,
                                         safeMaxPoints,
                                         halfWidth,
                                         halfHeight,
                                         frame,
                                         errorOut,
                                         warnings,
                                         clampedPoints);
        Py_DECREF(frameObj);
        if (!ok) {
            return false;
        }
        outFrames.append(frame);
    }

    return true;
}
} // namespace

PythonTrapScriptEngine::PythonTrapScriptEngine() {
    if (Py_IsInitialized()) {
        initialized = true;
        return;
    }

    if (!initializePythonRuntime(initErrorMessage)) {
        initialized = false;
        if (initErrorMessage.isEmpty()) {
            initErrorMessage = "Failed to initialize embedded Python runtime.";
        }
        return;
    }
    if (!Py_IsInitialized()) {
        initialized = false;
        initErrorMessage = "Failed to initialize embedded Python runtime.";
        return;
    }

    initialized = true;
}

PythonTrapScriptEngine::~PythonTrapScriptEngine() {
    if (initialized && Py_IsInitialized()) {
        Py_Finalize();
    }
}

bool PythonTrapScriptEngine::isReady() const {
    return initialized && Py_IsInitialized();
}

QString PythonTrapScriptEngine::initError() const {
    return initErrorMessage;
}

PythonTrapScriptResult PythonTrapScriptEngine::runScript(const QString &script,
                                                         int requestedFrameCount,
                                                         int cameraWidth,
                                                         int cameraHeight,
                                                         int maxPointsPerFrame) const {
    PythonTrapScriptResult result;

    if (!isReady()) {
        result.errorMessage = initErrorMessage.isEmpty()
            ? QString("Embedded Python runtime is not available.")
            : initErrorMessage;
        return result;
    }

    if (script.trimmed().isEmpty()) {
        result.errorMessage = "Python script is empty.";
        return result;
    }

    const int safeRequestedFrames = qBound(1, requestedFrameCount, 1000);
    const int safeMaxPoints = qBound(1, maxPointsPerFrame, 2000);

    gHelperCameraWidth = qMax(1, cameraWidth);
    gHelperCameraHeight = qMax(1, cameraHeight);

    PyGILState_STATE gilState = PyGILState_Ensure();

    PyObject *globals = PyDict_New();
    PyObject *compiled = nullptr;
    PyObject *execValue = nullptr;
    PyObject *funcPattern = nullptr;
    PyObject *funcFrames = nullptr;
    PyObject *args = nullptr;
    PyObject *outputObj = nullptr;
    PyObject *widthObj = nullptr;
    PyObject *heightObj = nullptr;

    QStringList warnings;
    const double halfWidth = static_cast<double>(cameraWidth) / 2.0;
    const double halfHeight = static_cast<double>(cameraHeight) / 2.0;
    int clampedPoints = 0;

    if (!globals) {
        result.errorMessage = "Failed to create Python globals dictionary.";
        goto cleanup;
    }

    if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) != 0) {
        result.errorMessage = "Failed to populate Python builtins.";
        goto cleanup;
    }

    widthObj = PyLong_FromLong(cameraWidth);
    heightObj = PyLong_FromLong(cameraHeight);
    if (!widthObj || !heightObj ||
        PyDict_SetItemString(globals, "__hot_camera_width", widthObj) != 0 ||
        PyDict_SetItemString(globals, "__hot_camera_height", heightObj) != 0) {
        Py_XDECREF(widthObj);
        Py_XDECREF(heightObj);
        result.errorMessage = "Failed to set script camera context variables.";
        goto cleanup;
    }
    Py_DECREF(widthObj);
    Py_DECREF(heightObj);

    {
        QString helperError;
        if (!injectHotHelpers(globals, helperError)) {
            result.errorMessage = helperError;
            goto cleanup;
        }
    }

    {
        const QByteArray scriptBytes = script.toUtf8();
        compiled = Py_CompileString(scriptBytes.constData(), "<trap_script>", Py_file_input);
    }

    if (!compiled) {
        result.errorMessage = fetchPythonExceptionText();
        goto cleanup;
    }

    execValue = PyEval_EvalCode(compiled, globals, globals);
    if (!execValue) {
        result.errorMessage = fetchPythonExceptionText();
        goto cleanup;
    }

    funcPattern = PyDict_GetItemString(globals, "build_pattern");
    if (funcPattern && PyCallable_Check(funcPattern)) {
        args = Py_BuildValue("(ii)", cameraWidth, cameraHeight);
        if (!args) {
            result.errorMessage = "Failed to build arguments for build_pattern.";
            goto cleanup;
        }

        outputObj = PyObject_CallObject(funcPattern, args);
        if (!outputObj) {
            result.errorMessage = fetchPythonExceptionText();
            goto cleanup;
        }

        QVector<QPointF> staticFrame;
        if (!parseFrameObject(outputObj,
                              1,
                              safeMaxPoints,
                              halfWidth,
                              halfHeight,
                              staticFrame,
                              result.errorMessage,
                              warnings,
                              clampedPoints)) {
            goto cleanup;
        }

        result.frames.clear();
        result.frames.reserve(safeRequestedFrames);
        for (int i = 0; i < safeRequestedFrames; ++i) {
            result.frames.append(staticFrame);
        }
        warnings << QString("build_pattern returned a static frame; repeating it for %1 frame(s).")
                        .arg(safeRequestedFrames);
    } else {
        funcFrames = PyDict_GetItemString(globals, "build_frames");
        if (!funcFrames || !PyCallable_Check(funcFrames)) {
            result.errorMessage = "Script must define callable build_pattern(width, height) or build_frames(frame_count, width, height).";
            goto cleanup;
        }

        args = Py_BuildValue("(iii)", safeRequestedFrames, cameraWidth, cameraHeight);
        if (!args) {
            result.errorMessage = "Failed to build arguments for build_frames.";
            goto cleanup;
        }

        outputObj = PyObject_CallObject(funcFrames, args);
        if (!outputObj) {
            result.errorMessage = fetchPythonExceptionText();
            goto cleanup;
        }

        if (!parseFramesObject(outputObj,
                               safeRequestedFrames,
                               safeMaxPoints,
                               halfWidth,
                               halfHeight,
                               result.frames,
                               result.errorMessage,
                               warnings,
                               clampedPoints)) {
            goto cleanup;
        }

        if (result.frames.size() == 1 && safeRequestedFrames > 1) {
            const QVector<QPointF> staticFrame = result.frames.first();
            result.frames.reserve(safeRequestedFrames);
            for (int i = 1; i < safeRequestedFrames; ++i) {
                result.frames.append(staticFrame);
            }
            warnings << QString("Single frame detected in build_frames; treating as static and repeating to %1 frame(s).")
                            .arg(safeRequestedFrames);
        }
    }

    if (result.frames.isEmpty()) {
        result.errorMessage = "No usable frames were produced by script output.";
        goto cleanup;
    }

    if (clampedPoints > 0) {
        warnings << QString("Clamped %1 point(s) to camera bounds (+/-%2, +/-%3).")
                        .arg(clampedPoints)
                        .arg(halfWidth, 0, 'f', 1)
                        .arg(halfHeight, 0, 'f', 1);
    }

    if (!warnings.isEmpty()) {
        result.warningMessage = warnings.join(" ");
    }

    result.success = true;

cleanup:
    Py_XDECREF(heightObj);
    Py_XDECREF(widthObj);
    Py_XDECREF(outputObj);
    Py_XDECREF(args);
    Py_XDECREF(execValue);
    Py_XDECREF(compiled);
    Py_XDECREF(globals);

    PyGILState_Release(gilState);
    return result;
}
