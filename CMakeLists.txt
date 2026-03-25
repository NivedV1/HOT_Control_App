cmake_minimum_required(VERSION 3.16)

project(HOT_Control_App VERSION 1.0 LANGUAGES CXX)
option(HOT_TARGET_GT730 "Build for GT 730 / Kepler (CUDA sm_35)" ON)

option(HOT_ENABLE_TEMP_GS_PROFILING "Enable temporary GS runtime profiling/logging instrumentation" ON)
option(HOT_ENABLE_OPENCL_GS "Enable OpenCV OpenCL GS backend" ON)
option(HOT_ENABLE_CUDA_GS "Enable CUDA GS backend" ON)
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
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
if (MSVC)
    if (DEFINED Python3_LIBRARY_RELEASE AND EXISTS "${Python3_LIBRARY_RELEASE}")
        set(HOT_PYTHON_LINK_LIB "${Python3_LIBRARY_RELEASE}")
    elseif (DEFINED _Python3_LIBRARY_RELEASE AND EXISTS "${_Python3_LIBRARY_RELEASE}")
        set(HOT_PYTHON_LINK_LIB "${_Python3_LIBRARY_RELEASE}")
    else()
        set(HOT_PYTHON_LINK_LIB ${Python3_LIBRARIES})
    endif()
else()
    set(HOT_PYTHON_LINK_LIB ${Python3_LIBRARIES})
endif()

if (WIN32)
    if (DEFINED Python3_RUNTIME_LIBRARY_RELEASE AND EXISTS "${Python3_RUNTIME_LIBRARY_RELEASE}")
        set(HOT_PYTHON_RUNTIME_DLL "${Python3_RUNTIME_LIBRARY_RELEASE}")
    elseif (DEFINED _Python3_RUNTIME_LIBRARY_RELEASE AND EXISTS "${_Python3_RUNTIME_LIBRARY_RELEASE}")
        set(HOT_PYTHON_RUNTIME_DLL "${_Python3_RUNTIME_LIBRARY_RELEASE}")
    else()
        get_filename_component(HOT_PYTHON_BIN_DIR "${Python3_EXECUTABLE}" DIRECTORY)
        if (EXISTS "${HOT_PYTHON_BIN_DIR}/python312.dll")
            set(HOT_PYTHON_RUNTIME_DLL "${HOT_PYTHON_BIN_DIR}/python312.dll")
        endif()
    endif()

    get_filename_component(HOT_PYTHON_HOME_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}" HOT_PYTHON_HOME_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/Lib" HOT_PYTHON_LIB_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/Lib/site-packages" HOT_PYTHON_SITE_PACKAGES_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/Lib/site-packages/numpy" HOT_PYTHON_NUMPY_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/Lib/site-packages/numpy.libs" HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/DLLs" HOT_PYTHON_DLLS_DIR_CMAKE)
    file(TO_CMAKE_PATH "${HOT_PYTHON_HOME_DIR}/python312.zip" HOT_PYTHON_ZIP_PATH_CMAKE)

    # Fallback: detect NumPy from interpreter (covers user-local site-packages installs).
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import importlib.util, pathlib; s=importlib.util.find_spec('numpy'); print(pathlib.Path(s.origin).resolve().parent if s and s.origin else '')"
        OUTPUT_VARIABLE HOT_PYTHON_NUMPY_DIR_DETECTED
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (HOT_PYTHON_NUMPY_DIR_DETECTED)
        file(TO_CMAKE_PATH "${HOT_PYTHON_NUMPY_DIR_DETECTED}" HOT_PYTHON_NUMPY_DIR_DETECTED_CMAKE)
        if (NOT EXISTS "${HOT_PYTHON_NUMPY_DIR_CMAKE}" AND EXISTS "${HOT_PYTHON_NUMPY_DIR_DETECTED_CMAKE}")
            set(HOT_PYTHON_NUMPY_DIR_CMAKE "${HOT_PYTHON_NUMPY_DIR_DETECTED_CMAKE}")
        endif()

        get_filename_component(HOT_PYTHON_NUMPY_PARENT_DIR "${HOT_PYTHON_NUMPY_DIR_DETECTED_CMAKE}" DIRECTORY)
        file(TO_CMAKE_PATH "${HOT_PYTHON_NUMPY_PARENT_DIR}/numpy.libs" HOT_PYTHON_NUMPY_LIBS_DETECTED_CMAKE)
        if (NOT EXISTS "${HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE}" AND EXISTS "${HOT_PYTHON_NUMPY_LIBS_DETECTED_CMAKE}")
            set(HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE "${HOT_PYTHON_NUMPY_LIBS_DETECTED_CMAKE}")
        endif()
    endif()
endif()

# Find OpenCV
set(OpenCV_DIR "C:/opencv/build")
find_package(OpenCV REQUIRED)

# Enable CUDA GS backend if requested
set(HOT_ENABLE_CUDA_GS_AVAILABLE OFF)
if (HOT_ENABLE_CUDA_GS)
    if (CUDA_TOOLKIT_ROOT_DIR)
        set(CUDAToolkit_ROOT "${CUDA_TOOLKIT_ROOT_DIR}")
    endif()
    find_package(CUDAToolkit QUIET)
    if (CUDAToolkit_FOUND)
        set(HOT_CUDA_ARCH_DEFAULT 75)
        set(HOT_ENABLE_CUDA_GS_SUPPORTED ON)

        if (MSVC AND CUDAToolkit_BIN_DIR)
            get_filename_component(HOT_CUDA_TOOLKIT_ROOT "${CUDAToolkit_BIN_DIR}" DIRECTORY)
            set(CMAKE_VS_PLATFORM_TOOLSET_CUDA "${CUDAToolkit_VERSION_MAJOR}.${CUDAToolkit_VERSION_MINOR}" CACHE STRING "CUDA toolset version used by the Visual Studio generator" FORCE)
            set(CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR "${HOT_CUDA_TOOLKIT_ROOT}" CACHE PATH "CUDA toolkit used by the Visual Studio generator" FORCE)
            message(STATUS "Using Visual Studio CUDA toolkit ${CMAKE_VS_PLATFORM_TOOLSET_CUDA} from ${CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR}")
        endif()

        if (MSVC AND CUDAToolkit_VERSION_MAJOR EQUAL 11 AND MSVC_VERSION GREATER_EQUAL 1930)
            message(WARNING "Note: CUDA ${CUDAToolkit_VERSION} does not support the active MSVC toolset (${MSVC_VERSION}) well enough for this build. Disabling CUDA GS backend for this configuration. Use a VS 2019 compatible toolset, an older MSVC toolset, or build with HOT_ENABLE_CUDA_GS=OFF.")
            set(HOT_ENABLE_CUDA_GS_SUPPORTED OFF)
        endif()

        if (HOT_TARGET_GT730)
            if (CUDAToolkit_VERSION_MAJOR GREATER_EQUAL 12)
                message(WARNING "Note: CUDA ${CUDAToolkit_VERSION} detected. HOT_TARGET_GT730=ON requires sm_35 support, which was removed in CUDA 12.0. Disabling CUDA GS backend for this build. Use CUDA 11.x for GT730 CUDA support, or set HOT_ENABLE_CUDA_GS=OFF.")
                set(HOT_ENABLE_CUDA_GS_SUPPORTED OFF)
            else()
                set(HOT_CUDA_ARCH_DEFAULT 35)
            endif()
        endif()

        if (HOT_ENABLE_CUDA_GS_SUPPORTED)
            if (HOT_TARGET_GT730)
                # GT730 path requires Kepler architecture.
                set(CMAKE_CUDA_ARCHITECTURES 35 CACHE STRING "CUDA architecture" FORCE)
            elseif ((NOT CMAKE_CUDA_ARCHITECTURES) OR (CMAKE_CUDA_ARCHITECTURES STREQUAL "35"))
                # If GT730 mode is off, avoid carrying a stale sm_35 cache value.
                set(CMAKE_CUDA_ARCHITECTURES 75 CACHE STRING "CUDA architecture" FORCE)
            endif()

            if (HOT_TARGET_GT730)
                message(STATUS "HOT_TARGET_GT730=ON: using CMAKE_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES}")
            endif()

            enable_language(CUDA)
            set(HOT_ENABLE_CUDA_GS_AVAILABLE ON)
        endif()
    else()
        message(STATUS "HOT_ENABLE_CUDA_GS=ON but CUDA toolkit not found. Building without CUDA GS backend.")
    endif()
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
    src/core/algorithms/rme_algorithm.cpp
    src/hardware/slm_controller.cpp
    src/hardware/camera_stream.cpp
    src/ui/components/targetgridwidget.cpp
    src/ui/components/pythonsyntaxhighlighter.cpp
    src/ui/components/pythoncodeeditor.cpp
    src/core/patterngenerator.cpp
    src/core/python_trap_script_engine.cpp
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
    src/ui/components/pythonsyntaxhighlighter.h
    src/ui/components/pythoncodeeditor.h
    src/ui/components/patternpresetswidget.h
    src/core/patterngenerator.h
    src/core/python_trap_script_engine.h
    src/core/trap_manager.h
    src/core/algorithms/phase_algorithm.h
    src/core/algorithms/gs_algorithm.h
    src/core/algorithms/gs_algorithm_cuda.h
    src/core/algorithms/rme_algorithm.h
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

# Ensure MSVC reports the correct __cplusplus value (Qt requires it).
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/permissive->
    )

    if (HOT_ENABLE_CUDA_GS_AVAILABLE AND CUDAToolkit_VERSION_MAJOR EQUAL 11)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--allow-unsupported-compiler>
        )
    endif()

    # Nvcc sometimes drops the flag; append defensively at the directory level.
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler=/Zc:__cplusplus -Xcompiler=/permissive-")

    if (HOT_ENABLE_CUDA_GS_AVAILABLE AND CUDAToolkit_VERSION_MAJOR EQUAL 11)
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --allow-unsupported-compiler")
    endif()
endif()

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

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        HOT_EMBED_PYTHON_HOME="${HOT_PYTHON_HOME_DIR_CMAKE}"
        HOT_EMBED_PYTHON_LIB="${HOT_PYTHON_LIB_DIR_CMAKE}"
        HOT_EMBED_PYTHON_SITE_PACKAGES="${HOT_PYTHON_SITE_PACKAGES_DIR_CMAKE}"
        HOT_EMBED_PYTHON_DLLS="${HOT_PYTHON_DLLS_DIR_CMAKE}"
        HOT_EMBED_PYTHON_ZIP="${HOT_PYTHON_ZIP_PATH_CMAKE}"
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
    ${Python3_INCLUDE_DIRS}
)

# Link the Qt libraries and OpenCV to your executable
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${HOT_PYTHON_LINK_LIB}
    ${OpenCV_LIBS}
)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

if (HOT_ENABLE_CUDA_GS_AVAILABLE)
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

    # 3. Copy Python runtime DLL required by embedded CPython.
    if (DEFINED HOT_PYTHON_RUNTIME_DLL AND EXISTS "${HOT_PYTHON_RUNTIME_DLL}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOT_PYTHON_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
        message(STATUS "Python runtime DLL deploy enabled: ${HOT_PYTHON_RUNTIME_DLL}")
    else()
        message(WARNING "Python runtime DLL not found during configure. Embedded Python may fail at runtime unless python312.dll is on PATH.")
    endif()

    # 4. Copy CPython stdlib/runtime folders for app-local embedded use (no system Python required).
    if (DEFINED HOT_PYTHON_ZIP_PATH_CMAKE AND EXISTS "${HOT_PYTHON_ZIP_PATH_CMAKE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOT_PYTHON_ZIP_PATH_CMAKE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endif()
    if (DEFINED HOT_PYTHON_LIB_DIR_CMAKE AND EXISTS "${HOT_PYTHON_LIB_DIR_CMAKE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HOT_PYTHON_LIB_DIR_CMAKE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Lib"
        )
    endif()
    if (DEFINED HOT_PYTHON_DLLS_DIR_CMAKE AND EXISTS "${HOT_PYTHON_DLLS_DIR_CMAKE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HOT_PYTHON_DLLS_DIR_CMAKE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/DLLs"
        )
    endif()

    # 5. Copy NumPy package files if present in source Python.
    if (DEFINED HOT_PYTHON_NUMPY_DIR_CMAKE AND EXISTS "${HOT_PYTHON_NUMPY_DIR_CMAKE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Lib/site-packages"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HOT_PYTHON_NUMPY_DIR_CMAKE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Lib/site-packages/numpy"
        )
    else()
        message(WARNING "NumPy package not found in source Python environment. 'import numpy' may fail on target machines.")
    endif()
    if (DEFINED HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE AND EXISTS "${HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Lib/site-packages"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HOT_PYTHON_NUMPY_LIBS_DIR_CMAKE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Lib/site-packages/numpy.libs"
        )
    endif()

endif()
