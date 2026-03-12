#ifndef GS_ALGORITHM_CUDA_H
#define GS_ALGORITHM_CUDA_H

#include "gs_algorithm.h"

namespace GSAlgorithm::CudaBackend {

QVector<GSCudaDeviceInfo> enumerateCudaDevicesNative();

GSResult runGerchbergSaxtonCudaNative(const GSConfig &config,
                                      const QVector<float> &sourceAmplitude,
                                      const QVector<float> &targetAmplitude,
                                      const QVector<float> &initialPhaseRad,
                                      const GSResult &baseResult);

} // namespace GSAlgorithm::CudaBackend

#endif // GS_ALGORITHM_CUDA_H
