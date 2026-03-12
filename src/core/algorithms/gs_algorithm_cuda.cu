#include "gs_algorithm_cuda.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <QImage>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

struct CudaKey {
    int deviceIndex = -1;
    int width = 0;
    int height = 0;

    bool operator<(const CudaKey &other) const {
        if (deviceIndex != other.deviceIndex) {
            return deviceIndex < other.deviceIndex;
        }
        if (width != other.width) {
            return width < other.width;
        }
        return height < other.height;
    }
};

struct CudaResources {
    int deviceIndex = -1;
    int width = 0;
    int height = 0;
    int pixelCount = 0;
    cufftHandle fftPlan = 0;
    float *dSourceAmplitude = nullptr;
    float *dTargetAmplitude = nullptr;
    float *dPhase = nullptr;
    cufftComplex *dComplexA = nullptr;
    cufftComplex *dComplexB = nullptr;

    ~CudaResources() {
        if (deviceIndex >= 0) {
            cudaSetDevice(deviceIndex);
        }
        if (fftPlan != 0) {
            cufftDestroy(fftPlan);
            fftPlan = 0;
        }
        if (dSourceAmplitude) {
            cudaFree(dSourceAmplitude);
            dSourceAmplitude = nullptr;
        }
        if (dTargetAmplitude) {
            cudaFree(dTargetAmplitude);
            dTargetAmplitude = nullptr;
        }
        if (dPhase) {
            cudaFree(dPhase);
            dPhase = nullptr;
        }
        if (dComplexA) {
            cudaFree(dComplexA);
            dComplexA = nullptr;
        }
        if (dComplexB) {
            cudaFree(dComplexB);
            dComplexB = nullptr;
        }
    }
};

std::mutex gCudaCacheMutex;
std::map<CudaKey, std::shared_ptr<CudaResources>> gCudaCache;

QString cufftErrorToString(cufftResult status) {
    switch (status) {
    case CUFFT_SUCCESS:
        return "CUFFT_SUCCESS";
    case CUFFT_INVALID_PLAN:
        return "CUFFT_INVALID_PLAN";
    case CUFFT_ALLOC_FAILED:
        return "CUFFT_ALLOC_FAILED";
    case CUFFT_INVALID_TYPE:
        return "CUFFT_INVALID_TYPE";
    case CUFFT_INVALID_VALUE:
        return "CUFFT_INVALID_VALUE";
    case CUFFT_INTERNAL_ERROR:
        return "CUFFT_INTERNAL_ERROR";
    case CUFFT_EXEC_FAILED:
        return "CUFFT_EXEC_FAILED";
    case CUFFT_SETUP_FAILED:
        return "CUFFT_SETUP_FAILED";
    case CUFFT_INVALID_SIZE:
        return "CUFFT_INVALID_SIZE";
    case CUFFT_UNALIGNED_DATA:
        return "CUFFT_UNALIGNED_DATA";
    case CUFFT_INCOMPLETE_PARAMETER_LIST:
        return "CUFFT_INCOMPLETE_PARAMETER_LIST";
    case CUFFT_INVALID_DEVICE:
        return "CUFFT_INVALID_DEVICE";
    case CUFFT_PARSE_ERROR:
        return "CUFFT_PARSE_ERROR";
    case CUFFT_NO_WORKSPACE:
        return "CUFFT_NO_WORKSPACE";
    case CUFFT_NOT_IMPLEMENTED:
        return "CUFFT_NOT_IMPLEMENTED";
    case CUFFT_NOT_SUPPORTED:
        return "CUFFT_NOT_SUPPORTED";
    default:
        return QString("CUFFT_ERROR_%1").arg(static_cast<int>(status));
    }
}

QString cudaErrorToString(cudaError_t status) {
    return QString("%1 (%2)")
        .arg(QString::fromUtf8(cudaGetErrorString(status)))
        .arg(static_cast<int>(status));
}

bool checkCuda(cudaError_t status, const char *context, QString &error) {
    if (status == cudaSuccess) {
        return true;
    }
    error = QString("%1 failed: %2").arg(context).arg(cudaErrorToString(status));
    return false;
}

bool checkCufft(cufftResult status, const char *context, QString &error) {
    if (status == CUFFT_SUCCESS) {
        return true;
    }
    error = QString("%1 failed: %2").arg(context).arg(cufftErrorToString(status));
    return false;
}

QString formatCudaDeviceName(int deviceIndex, const cudaDeviceProp &prop) {
    const double memoryGiB = static_cast<double>(prop.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0);
    return QString("[D%1] %2 (CC %3.%4, %5 GiB)")
        .arg(deviceIndex)
        .arg(QString::fromUtf8(prop.name))
        .arg(prop.major)
        .arg(prop.minor)
        .arg(QString::number(memoryGiB, 'f', 2));
}

bool selectCudaDevice(int requestedIndex,
                      int &selectedIndex,
                      QString &selectedName,
                      QString &error) {
    int deviceCount = 0;
    if (!checkCuda(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount", error)) {
        return false;
    }
    if (deviceCount <= 0) {
        error = "No CUDA devices detected.";
        return false;
    }

    auto isCompatible = [](const cudaDeviceProp &prop) {
        return prop.major >= 3;
    };

    if (requestedIndex >= 0) {
        if (requestedIndex >= deviceCount) {
            error = QString("Selected CUDA device D%1 is unavailable. Device count: %2.")
                        .arg(requestedIndex)
                        .arg(deviceCount);
            return false;
        }
        cudaDeviceProp selectedProp {};
        if (!checkCuda(cudaGetDeviceProperties(&selectedProp, requestedIndex),
                       "cudaGetDeviceProperties(selected)",
                       error)) {
            return false;
        }
        if (!isCompatible(selectedProp)) {
            error = QString("Selected CUDA device D%1 is not compatible (CC %2.%3).")
                        .arg(requestedIndex)
                        .arg(selectedProp.major)
                        .arg(selectedProp.minor);
            return false;
        }

        selectedIndex = requestedIndex;
        selectedName = formatCudaDeviceName(requestedIndex, selectedProp);
        return true;
    }

    for (int i = 0; i < deviceCount; ++i) {
        cudaDeviceProp prop {};
        if (!checkCuda(cudaGetDeviceProperties(&prop, i),
                       "cudaGetDeviceProperties(auto)",
                       error)) {
            return false;
        }
        if (!isCompatible(prop)) {
            continue;
        }

        selectedIndex = i;
        selectedName = formatCudaDeviceName(i, prop);
        return true;
    }

    error = "No compatible CUDA device found (requires compute capability 3.0+).";
    return false;
}

std::shared_ptr<CudaResources> acquireCudaResources(int deviceIndex,
                                                    int width,
                                                    int height,
                                                    QString &error) {
    const CudaKey key {deviceIndex, width, height};

    {
        std::lock_guard<std::mutex> lock(gCudaCacheMutex);
        auto existing = gCudaCache.find(key);
        if (existing != gCudaCache.end() && existing->second) {
            return existing->second;
        }
    }

    std::shared_ptr<CudaResources> resources = std::make_shared<CudaResources>();
    resources->deviceIndex = deviceIndex;
    resources->width = width;
    resources->height = height;
    resources->pixelCount = width * height;

    if (!checkCuda(cudaSetDevice(deviceIndex), "cudaSetDevice", error)) {
        return nullptr;
    }

    const size_t floatBytes = static_cast<size_t>(resources->pixelCount) * sizeof(float);
    const size_t complexBytes = static_cast<size_t>(resources->pixelCount) * sizeof(cufftComplex);

    if (!checkCuda(cudaMalloc(reinterpret_cast<void **>(&resources->dSourceAmplitude), floatBytes),
                   "cudaMalloc(dSourceAmplitude)",
                   error)) {
        return nullptr;
    }
    if (!checkCuda(cudaMalloc(reinterpret_cast<void **>(&resources->dTargetAmplitude), floatBytes),
                   "cudaMalloc(dTargetAmplitude)",
                   error)) {
        return nullptr;
    }
    if (!checkCuda(cudaMalloc(reinterpret_cast<void **>(&resources->dPhase), floatBytes),
                   "cudaMalloc(dPhase)",
                   error)) {
        return nullptr;
    }
    if (!checkCuda(cudaMalloc(reinterpret_cast<void **>(&resources->dComplexA), complexBytes),
                   "cudaMalloc(dComplexA)",
                   error)) {
        return nullptr;
    }
    if (!checkCuda(cudaMalloc(reinterpret_cast<void **>(&resources->dComplexB), complexBytes),
                   "cudaMalloc(dComplexB)",
                   error)) {
        return nullptr;
    }

    if (!checkCufft(cufftPlan2d(&resources->fftPlan, height, width, CUFFT_C2C),
                    "cufftPlan2d",
                    error)) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(gCudaCacheMutex);
        gCudaCache[key] = resources;
    }
    return resources;
}

double wrapPhaseRad(double phase) {
    double wrapped = std::fmod(phase, kTwoPi);
    if (wrapped < 0.0) {
        wrapped += kTwoPi;
    }
    return wrapped;
}

void populateResultPhaseImage(const QVector<float> &phaseRad,
                              int width,
                              int height,
                              GSAlgorithm::GSResult &result) {
    result.wrappedPhaseRad.resize(width * height);
    result.phaseMask8Bit = QImage(width, height, QImage::Format_Grayscale8);

    for (int y = 0; y < height; ++y) {
        uchar *row = result.phaseMask8Bit.scanLine(y);
        const int rowBase = y * width;
        for (int x = 0; x < width; ++x) {
            const int idx = rowBase + x;
            const double wrapped = wrapPhaseRad(static_cast<double>(phaseRad[idx]));
            result.wrappedPhaseRad[idx] = wrapped;

            const double scaled = (wrapped / kTwoPi) * 255.0;
            const int gray = std::clamp(static_cast<int>(scaled), 0, 255);
            row[x] = static_cast<uchar>(gray);
        }
    }
}

__global__ void buildSlmFieldKernel(const float *sourceAmplitude,
                                    const float *phaseRad,
                                    cufftComplex *outField,
                                    int pixelCount) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }

    const float amp = sourceAmplitude[idx];
    const float phase = phaseRad[idx];
    float s = 0.0f;
    float c = 1.0f;
    sincosf(phase, &s, &c);
    outField[idx] = make_cuFloatComplex(amp * c, amp * s);
}

__global__ void rollComplexKernel(const cufftComplex *inData,
                                  cufftComplex *outData,
                                  int width,
                                  int height,
                                  int shiftX,
                                  int shiftY) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }

    int srcX = x - shiftX;
    int srcY = y - shiftY;

    srcX %= width;
    srcY %= height;
    if (srcX < 0) {
        srcX += width;
    }
    if (srcY < 0) {
        srcY += height;
    }

    outData[y * width + x] = inData[srcY * width + srcX];
}

__global__ void applyTargetConstraintKernel(const cufftComplex *focalField,
                                            const float *targetAmplitude,
                                            cufftComplex *outFocalField,
                                            int pixelCount) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }

    const cufftComplex val = focalField[idx];
    const float phase = atan2f(val.y, val.x);
    const float amp = targetAmplitude[idx];
    float s = 0.0f;
    float c = 1.0f;
    sincosf(phase, &s, &c);
    outFocalField[idx] = make_cuFloatComplex(amp * c, amp * s);
}

__global__ void extractPhaseKernel(const cufftComplex *slmField,
                                   float *phaseRad,
                                   int pixelCount) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pixelCount) {
        return;
    }

    const cufftComplex val = slmField[idx];
    phaseRad[idx] = atan2f(val.y, val.x);
}

bool runKernelSequence(const std::shared_ptr<CudaResources> &resources,
                       int iterations,
                       QString &error) {
    if (!resources) {
        error = "Internal CUDA resources are null.";
        return false;
    }

    const int width = resources->width;
    const int height = resources->height;
    const int pixelCount = resources->pixelCount;

    const int blockSize = 256;
    const int gridSize = (pixelCount + blockSize - 1) / blockSize;
    const dim3 block2d(16, 16);
    const dim3 grid2d((width + block2d.x - 1) / block2d.x,
                      (height + block2d.y - 1) / block2d.y);

    const int fftShiftX = width / 2;
    const int fftShiftY = height / 2;
    const int ifftShiftX = -(width / 2);
    const int ifftShiftY = -(height / 2);

    for (int iter = 0; iter < iterations; ++iter) {
        buildSlmFieldKernel<<<gridSize, blockSize>>>(resources->dSourceAmplitude,
                                                     resources->dPhase,
                                                     resources->dComplexA,
                                                     pixelCount);
        if (!checkCuda(cudaGetLastError(), "buildSlmFieldKernel launch", error)) {
            return false;
        }

        rollComplexKernel<<<grid2d, block2d>>>(resources->dComplexA,
                                               resources->dComplexB,
                                               width,
                                               height,
                                               ifftShiftX,
                                               ifftShiftY);
        if (!checkCuda(cudaGetLastError(), "ifftshift roll launch", error)) {
            return false;
        }

        if (!checkCufft(cufftExecC2C(resources->fftPlan,
                                     resources->dComplexB,
                                     resources->dComplexA,
                                     CUFFT_FORWARD),
                        "cufftExecC2C forward",
                        error)) {
            return false;
        }

        rollComplexKernel<<<grid2d, block2d>>>(resources->dComplexA,
                                               resources->dComplexB,
                                               width,
                                               height,
                                               fftShiftX,
                                               fftShiftY);
        if (!checkCuda(cudaGetLastError(), "fftshift roll launch", error)) {
            return false;
        }

        applyTargetConstraintKernel<<<gridSize, blockSize>>>(resources->dComplexB,
                                                             resources->dTargetAmplitude,
                                                             resources->dComplexA,
                                                             pixelCount);
        if (!checkCuda(cudaGetLastError(), "applyTargetConstraintKernel launch", error)) {
            return false;
        }

        rollComplexKernel<<<grid2d, block2d>>>(resources->dComplexA,
                                               resources->dComplexB,
                                               width,
                                               height,
                                               ifftShiftX,
                                               ifftShiftY);
        if (!checkCuda(cudaGetLastError(), "inverse ifftshift roll launch", error)) {
            return false;
        }

        if (!checkCufft(cufftExecC2C(resources->fftPlan,
                                     resources->dComplexB,
                                     resources->dComplexA,
                                     CUFFT_INVERSE),
                        "cufftExecC2C inverse",
                        error)) {
            return false;
        }

        rollComplexKernel<<<grid2d, block2d>>>(resources->dComplexA,
                                               resources->dComplexB,
                                               width,
                                               height,
                                               fftShiftX,
                                               fftShiftY);
        if (!checkCuda(cudaGetLastError(), "inverse fftshift roll launch", error)) {
            return false;
        }

        extractPhaseKernel<<<gridSize, blockSize>>>(resources->dComplexB,
                                                    resources->dPhase,
                                                    pixelCount);
        if (!checkCuda(cudaGetLastError(), "extractPhaseKernel launch", error)) {
            return false;
        }
    }

    if (!checkCuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize", error)) {
        return false;
    }
    return true;
}

} // namespace

namespace GSAlgorithm::CudaBackend {

QVector<GSCudaDeviceInfo> enumerateCudaDevicesNative() {
    QVector<GSCudaDeviceInfo> out;

    int deviceCount = 0;
    cudaError_t countStatus = cudaGetDeviceCount(&deviceCount);
    if (countStatus != cudaSuccess || deviceCount <= 0) {
        cudaGetLastError();
        return out;
    }

    out.reserve(deviceCount);
    for (int i = 0; i < deviceCount; ++i) {
        cudaDeviceProp prop {};
        if (cudaGetDeviceProperties(&prop, i) != cudaSuccess) {
            cudaGetLastError();
            continue;
        }

        GSCudaDeviceInfo info;
        info.deviceIndex = i;
        info.isCompatible = (prop.major >= 3);
        info.displayName = formatCudaDeviceName(i, prop);
        out.append(info);
    }

    return out;
}

GSResult runGerchbergSaxtonCudaNative(const GSConfig &config,
                                      const QVector<float> &sourceAmplitude,
                                      const QVector<float> &targetAmplitude,
                                      const QVector<float> &initialPhaseRad,
                                      const GSResult &baseResult) {
    GSResult result = baseResult;
    result.backendUsed = GSComputeBackendUsed::CUDA;

    const int width = config.slmWidth;
    const int height = config.slmHeight;
    const int pixelCount = width * height;
    if (pixelCount <= 0) {
        result.error = "Invalid SLM dimensions for CUDA backend.";
        return result;
    }
    if (sourceAmplitude.size() != pixelCount ||
        targetAmplitude.size() != pixelCount ||
        initialPhaseRad.size() != pixelCount) {
        result.error = "CUDA GS input buffers do not match SLM dimensions.";
        return result;
    }

    QString selectError;
    int selectedDeviceIndex = -1;
    QString selectedDeviceName;
    if (!selectCudaDevice(config.cudaDeviceIndex, selectedDeviceIndex, selectedDeviceName, selectError)) {
        result.error = selectError;
        return result;
    }

    result.backendInfo = QString("CUDA %1").arg(selectedDeviceName);

    QString error;
    if (!checkCuda(cudaSetDevice(selectedDeviceIndex), "cudaSetDevice(selected)", error)) {
        result.error = error;
        return result;
    }

    std::shared_ptr<CudaResources> resources = acquireCudaResources(selectedDeviceIndex, width, height, error);
    if (!resources) {
        result.error = error.isEmpty() ? "Failed to initialize CUDA GS resources." : error;
        return result;
    }

    const size_t floatBytes = static_cast<size_t>(pixelCount) * sizeof(float);
    if (!checkCuda(cudaMemcpy(resources->dSourceAmplitude,
                              sourceAmplitude.constData(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(sourceAmplitude)",
                   error)) {
        result.error = error;
        return result;
    }
    if (!checkCuda(cudaMemcpy(resources->dTargetAmplitude,
                              targetAmplitude.constData(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(targetAmplitude)",
                   error)) {
        result.error = error;
        return result;
    }
    if (!checkCuda(cudaMemcpy(resources->dPhase,
                              initialPhaseRad.constData(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(initialPhase)",
                   error)) {
        result.error = error;
        return result;
    }

    if (!runKernelSequence(resources, config.iterations, error)) {
        result.error = QString("CUDA GS kernel pipeline failed: %1").arg(error);
        return result;
    }

    QVector<float> phaseOut(pixelCount, 0.0f);
    if (!checkCuda(cudaMemcpy(phaseOut.data(),
                              resources->dPhase,
                              floatBytes,
                              cudaMemcpyDeviceToHost),
                   "cudaMemcpy(finalPhase)",
                   error)) {
        result.error = error;
        return result;
    }

    populateResultPhaseImage(phaseOut, width, height, result);
    result.success = true;
    return result;
}

} // namespace GSAlgorithm::CudaBackend
