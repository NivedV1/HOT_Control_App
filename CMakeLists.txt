cmake_minimum_required(VERSION 3.16)

project(HOT_Control_App VERSION 1.0 LANGUAGES CXX)

if(NOT DEFINED ENV{QTFRAMEWORK_BYPASS_LICENSE_CHECK})
    set(ENV{QTFRAMEWORK_BYPASS_LICENSE_CHECK} 1)
endif()

# Avoid CUDA architecture policy warnings on newer CMake versions.
if (POLICY CMP0104)
    cmake_policy(SET CMP0104 NEW)
endif()

option(HOT_ENABLE_TEMP_GS_PROFILING "Enable temporary GS runtime profiling/logging instrumentation" ON)
option(HOT_ENABLE_OPENCL_GS "Enable OpenCV OpenCL GS backend" ON)
option(HOT_ENABLE_VKFFT "Use VkFFT (OpenCL backend) for OpenCL GS FFTs when available" ON)
option(HOT_ENABLE_CLFFT "Use clFFT (OpenCL backend) for OpenCL GS FFTs when available" OFF)
set(CUDA_TOOLKIT_ROOT_DIR "" CACHE PATH "Optional CUDA toolkit root directory")
# Set C++ standard (C++17 is recommended for modern Qt and scientific computing)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automated build tools
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)

find_package(OpenCL REQUIRED)

# Find OpenCV
set(OpenCV_DIR "C:/opencv/build")
find_package(OpenCV REQUIRED)
set(HOT_ENABLE_VKFFT_AVAILABLE OFF)
set(HOT_ENABLE_CLFFT_AVAILABLE OFF)
if (HOT_ENABLE_VKFFT AND HOT_ENABLE_OPENCL_GS)
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/third_party/VkFFT/vkFFT.h")
        set(HOT_ENABLE_VKFFT_AVAILABLE ON)
        message(STATUS "Using bundled VkFFT (OpenCL backend)")
    else()
        message(STATUS "HOT_ENABLE_VKFFT=ON but third_party/VkFFT/vkFFT.h not found; falling back to OpenCV DFT")
    endif()
endif()
if (HOT_ENABLE_CLFFT AND HOT_ENABLE_OPENCL_GS)
    set(CLFFT_ROOT "" CACHE PATH "Root of clFFT installation (contains include/ and lib/)")
    find_path(CLFFT_INCLUDE_DIR NAMES clFFT.h
              HINTS ${CLFFT_ROOT}/include ${CLFFT_ROOT})
    find_library(CLFFT_LIBRARY NAMES clFFT
                 HINTS ${CLFFT_ROOT}/lib ${CLFFT_ROOT}/lib64 ${CLFFT_ROOT}/lib/import)
    if (CLFFT_INCLUDE_DIR AND CLFFT_LIBRARY)
        set(HOT_ENABLE_CLFFT_AVAILABLE ON)
        message(STATUS "clFFT found: ${CLFFT_LIBRARY}")
    else()
        message(STATUS "HOT_ENABLE_CLFFT=ON but clFFT not found; set CLFFT_ROOT")
    endif()
endif()
set(HOT_ENABLE_CUDA_GS_AVAILABLE ON)
if (HOT_ENABLE_CUDA_GS)
    if (CUDA_TOOLKIT_ROOT_DIR)
        set(CUDAToolkit_ROOT "${CUDA_TOOLKIT_ROOT_DIR}")
    endif()
    find_package(CUDAToolkit QUIET)
    if (CUDAToolkit_FOUND)
        enable_language(CUDA)
        set(HOT_ENABLE_CUDA_GS_AVAILABLE ON)
    else()
        message(STATUS "HOT_ENABLE_CUDA_GS=ON but CUDA toolkit not found. Building without CUDA GS backend.")
    endif()
endif()

if (HOT_ENABLE_CUDA_GS_AVAILABLE AND NOT CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "75" CACHE STRING "CUDA architectures for nvcc (e.g. 75;86)")
    message(STATUS "CMAKE_CUDA_ARCHITECTURES not set; defaulting to ${CMAKE_CUDA_ARCHITECTURES}")
endif()

# Define all the source files
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/computebenchmarkdialog.cpp
    src/ui/settingsdialog.cpp
    src/ui/sourceintensitydialog.cpp
    src/camera/cameramanager.cpp
    src/core/trap_manager.cpp
    src/ui/hologramdialog.cpp
    src/core/algorithms/gs_algorithm.cpp
    src/hardware/slm_controller.cpp
    src/hardware/camera_stream.cpp
    src/ui/components/targetgridwidget.cpp
    src/core/patterngenerator.cpp
    src/ui/components/patternpresetswidget.cpp
)

if (HOT_ENABLE_CUDA_GS_AVAILABLE)
    list(APPEND SOURCES
        src/core/algorithms/gs_algorithm_cuda.cu
    )
endif()

# Define header files
set(HEADERS
    src/ui/mainwindow.h
    src/ui/computebenchmarkdialog.h
    src/ui/settingsdialog.h
    src/ui/sourceintensitydialog.h
    src/camera/cameramanager.h
    src/ui/hologramdialog.h
    src/ui/components/traptable.h
    src/ui/components/patternview.h
    src/ui/components/targetgridwidget.h
    src/ui/components/patternpresetswidget.h
    src/core/patterngenerator.h
    src/core/trap_manager.h
    src/core/algorithms/phase_algorithm.h
    src/core/algorithms/gs_algorithm.h
    src/core/algorithms/gs_algorithm_cuda.h
    src/core/algorithms/wgs_algorithm.h
    src/hardware/slm_controller.h
    src/hardware/camera_stream.h
)

# Define resource files
set(RESOURCES
    resources/resources.qrc
    resources/app_icon.rc
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

if (HOT_ENABLE_TEMP_GS_PROFILING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_TEMP_GS_PROFILING=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_TEMP_GS_PROFILING=0)
endif()

if (HOT_ENABLE_OPENCL_GS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_OPENCL_GS=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_OPENCL_GS=0)
endif()

if (HOT_ENABLE_CUDA_GS_AVAILABLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_CUDA_GS=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_ENABLE_CUDA_GS=0)
endif()

if (HOT_ENABLE_VKFFT_AVAILABLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_USE_VKFFT=1 VKFFT_BACKEND=3)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/third_party/VkFFT)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_USE_VKFFT=0)
endif()
if (HOT_ENABLE_CLFFT_AVAILABLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_USE_CLFFT=1)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CLFFT_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CLFFT_LIBRARY})
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HOT_USE_CLFFT=0)
endif()

# Qt 6 on MSVC requires a correct __cplusplus value.
# Qt 6 on MSVC requires a correct __cplusplus value.
# Ensure both C++ and CUDA (via nvcc host compiler) get /Zc:__cplusplus.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/FS>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/permissive->
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/FS>
    )
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    src/ui
    src/ui/components
    src/core
    src/core/algorithms
    src/hardware
    src/camera
)

# Link the Qt libraries and OpenCV to your executable
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    OpenCL::OpenCL
    ${OpenCV_LIBS}
)

if (HOT_ENABLE_CUDA_GS_AVAILABLE)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        CUDA::cudart
        CUDA::cufft
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
    )
endif()

if (WIN32)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)

    # 1. Automatically deploy Qt DLL dependencies
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${_qt_bin_dir}/windeployqt.exe"
                --no-translations
                "$<TARGET_FILE:${PROJECT_NAME}>"
    )

# 2. Automatically copy the Hamamatsu DLL and OpenCV DLL next to the executable
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/opencv/build/x64/vc16/bin/opencv_world4120.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )

endif()
