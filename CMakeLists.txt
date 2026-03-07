cmake_minimum_required(VERSION 3.16)

project(HOT_Control_App VERSION 1.0 LANGUAGES CXX)

# Set C++ standard (C++17 is recommended for modern Qt and scientific computing)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automated build tools
set(CMAKE_AUTOMOC ON) 
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON) 

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets) 

# Find OpenCV
set(OpenCV_DIR "C:/opencv/build")
find_package(OpenCV REQUIRED)

# Define all the source files 
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/settingsdialog.cpp 
    src/camera/cameramanager.cpp 
    src/core/trap_manager.cpp
    src/ui/hologramdialog.cpp
    src/core/algorithms/gs_algorithm.cpp
    src/hardware/slm_controller.cpp
    src/hardware/camera_stream.cpp
    src/ui/components/targetgridwidget.cpp
)

# Define header files
set(HEADERS
    src/ui/mainwindow.h
    src/ui/settingsdialog.h 
    src/camera/cameramanager.h 
    src/ui/hologramdialog.h
    src/ui/components/traptable.h
    src/ui/components/patternview.h
    src/ui/components/targetgridwidget.h
    src/core/trap_manager.h
    src/core/algorithms/phase_algorithm.h
    src/core/algorithms/gs_algorithm.h
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
    ${OpenCV_LIBS} 
)

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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hardware/Image_Control.dll"
        "C:/opencv/build/x64/vc16/bin/opencv_world4120.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()