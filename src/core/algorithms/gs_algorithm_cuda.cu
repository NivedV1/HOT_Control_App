#include "gs_algorithm_cuda.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>

namespace {

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

std::string cufftErrorToString(cufftResult status) {
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
#ifdef CUFFT_INCOMPLETE_PARAMETER_LIST
    case CUFFT_INCOMPLETE_PARAMETER_LIST:
        return "CUFFT_INCOMPLETE_PARAMETER_LIST";
#endif
    case CUFFT_INVALID_DEVICE:
        return "CUFFT_INVALID_DEVICE";
#ifdef CUFFT_PARSE_ERROR
    case CUFFT_PARSE_ERROR:
        return "CUFFT_PARSE_ERROR";
#endif
    case CUFFT_NO_WORKSPACE:
        return "CUFFT_NO_WORKSPACE";
    case CUFFT_NOT_IMPLEMENTED:
        return "CUFFT_NOT_IMPLEMENTED";
    case CUFFT_NOT_SUPPORTED:
        return "CUFFT_NOT_SUPPORTED";
    default:
        char buf[64];
        snprintf(buf, sizeof(buf), "CUFFT_ERROR_%d", static_cast<int>(status));
        return std::string(buf);
    }
}

std::string cudaErrorToString(cudaError_t status) {
    const char *errStr = cudaGetErrorString(status);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s (%d)", errStr ? errStr : "unknown error", static_cast<int>(status));
    return std::string(buf);
}

bool checkCuda(cudaError_t status, const char *context, std::string &error) {
    if (status == cudaSuccess) {
        return true;
    }
    error = std::string(context) + " failed: " + cudaErrorToString(status);
    return false;
}

bool checkCufft(cufftResult status, const char *context, std::string &error) {
    if (status == CUFFT_SUCCESS) {
        return true;
    }
    error = std::string(context) + " failed: " + cufftErrorToString(status);
    return false;
}

std::string formatCudaDeviceName(int deviceIndex, const cudaDeviceProp &prop) {
    const double memoryGiB = static_cast<double>(prop.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0);
    char buf[512];
    snprintf(buf, sizeof(buf), "[D%d] %s (CC %d.%d, %.2f GiB)",
             deviceIndex, prop.name, prop.major, prop.minor, memoryGiB);
    return std::string(buf);
}

bool selectCudaDevice(int requestedIndex,
                      int &selectedIndex,
                      std::string &selectedName,
                      std::string &error) {
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
            char buf[128];
            snprintf(buf, sizeof(buf), "Selected CUDA device D%d is unavailable. Device count: %d.", 
                     requestedIndex, deviceCount);
            error = std::string(buf);
            return false;
        }
        cudaDeviceProp selectedProp {};
        if (!checkCuda(cudaGetDeviceProperties(&selectedProp, requestedIndex),
                       "cudaGetDeviceProperties(selected)",
                       error)) {
            return false;
        }
        if (!isCompatible(selectedProp)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Selected CUDA device D%d is not compatible (CC %d.%d).", 
                     requestedIndex, selectedProp.major, selectedProp.minor);
            error = std::string(buf);
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
                                                    std::string &error) {
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
                       std::string &error) {
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

std::vector<GSCudaDeviceInfoNative> enumerateCudaDevicesNative() {
    std::vector<GSCudaDeviceInfoNative> out;

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

        GSCudaDeviceInfoNative info;
        info.deviceIndex = i;
        info.isCompatible = (prop.major >= 3);
        info.displayName = formatCudaDeviceName(i, prop);
        out.push_back(info);
    }

    return out;
}

GSCudaResultNative runGerchbergSaxtonCudaNative(const GSCudaConfigNative &config,
                                                const std::vector<float> &sourceAmplitude,
                                                const std::vector<float> &targetAmplitude,
                                                const std::vector<float> &initialPhaseRad) {
    GSCudaResultNative result;
    
    const int width = config.slmWidth;
    const int height = config.slmHeight;
    const int pixelCount = width * height;
    if (pixelCount <= 0) {
        result.error = "Invalid SLM dimensions for CUDA backend.";
        return result;
    }
    if (static_cast<int>(sourceAmplitude.size()) != pixelCount ||
        static_cast<int>(targetAmplitude.size()) != pixelCount ||
        static_cast<int>(initialPhaseRad.size()) != pixelCount) {
        result.error = "CUDA GS input buffers do not match SLM dimensions.";
        return result;
    }

    std::string selectError;
    int selectedDeviceIndex = -1;
    std::string selectedDeviceName;
    if (!selectCudaDevice(config.cudaDeviceIndex, selectedDeviceIndex, selectedDeviceName, selectError)) {
        result.error = selectError;
        return result;
    }

    result.backendInfo = "CUDA " + selectedDeviceName;

    std::string error;
    if (!checkCuda(cudaSetDevice(selectedDeviceIndex), "cudaSetDevice(selected)", error)) {
        result.error = error;
        return result;
    }

    std::shared_ptr<CudaResources> resources = acquireCudaResources(selectedDeviceIndex, width, height, error);
    if (!resources) {
        result.error = error.empty() ? "Failed to initialize CUDA GS resources." : error;
        return result;
    }

    const size_t floatBytes = static_cast<size_t>(pixelCount) * sizeof(float);
    if (!checkCuda(cudaMemcpy(resources->dSourceAmplitude,
                              sourceAmplitude.data(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(sourceAmplitude)",
                   error)) {
        result.error = error;
        return result;
    }
    if (!checkCuda(cudaMemcpy(resources->dTargetAmplitude,
                              targetAmplitude.data(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(targetAmplitude)",
                   error)) {
        result.error = error;
        return result;
    }
    if (!checkCuda(cudaMemcpy(resources->dPhase,
                              initialPhaseRad.data(),
                              floatBytes,
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(initialPhase)",
                   error)) {
        result.error = error;
        return result;
    }

    if (!runKernelSequence(resources, config.iterations, error)) {
        result.error = "CUDA GS kernel pipeline failed: " + error;
        return result;
    }

    result.phaseOut.assign(pixelCount, 0.0f);
    if (!checkCuda(cudaMemcpy(result.phaseOut.data(),
                              resources->dPhase,
                              floatBytes,
                              cudaMemcpyDeviceToHost),
                   "cudaMemcpy(finalPhase)",
                   error)) {
        result.error = error;
        return result;
    }

    result.success = true;
    return result;
}

} // namespace GSAlgorithm::CudaBackend
