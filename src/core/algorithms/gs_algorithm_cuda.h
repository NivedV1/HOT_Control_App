#ifndef GS_ALGORITHM_CUDA_H
#define GS_ALGORITHM_CUDA_H

#include <vector>
#include <string>
#include <cstdint>

namespace GSAlgorithm::CudaBackend {

struct GSCudaDeviceInfoNative {
    int deviceIndex = -1;
    std::string displayName;
    bool isCompatible = false;
};

struct GSCudaConfigNative {
    int slmWidth = 0;
    int slmHeight = 0;
    int iterations = 20;
    int cudaDeviceIndex = 0;
};

struct GSCudaResultNative {
    bool success = false;
    std::string error;
    std::string backendInfo;
    std::vector<float> phaseOut; // Raw phase data in radians
};

std::vector<GSCudaDeviceInfoNative> enumerateCudaDevicesNative();

GSCudaResultNative runGerchbergSaxtonCudaNative(const GSCudaConfigNative &config,
                                                const std::vector<float> &sourceAmplitude,
                                                const std::vector<float> &targetAmplitude,
                                                const std::vector<float> &initialPhaseRad);

} // namespace GSAlgorithm::CudaBackend

#endif // GS_ALGORITHM_CUDA_H
