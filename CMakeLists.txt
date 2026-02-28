cmake_minimum_required(VERSION 3.16)

project(HOT_Control_App VERSION 1.0 LANGUAGES CXX)

# Set C++ standard (C++17 is recommended for modern Qt and scientific computing)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automated build tools
set(CMAKE_AUTOMOC ON) # Automatically handles Q_OBJECT macros
set(CMAKE_AUTORCC ON) # Automatically compiles .qrc resource files
set(CMAKE_AUTOUIC ON) # Automatically handles .ui files (if you use Qt Designer later)

# Find Qt6 (Change to Qt5 if you are using an older version)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# (Optional - Uncomment later) Find OpenCV for your live camera feed
# find_package(OpenCV REQUIRED)

# Define all the source files we created
set(SOURCES
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/monitorpanel.cpp
    src/ui/controlpanel.cpp
    src/core/trap_manager.cpp
    src/core/algorithms/gs_algorithm.cpp
    src/hardware/slm_controller.cpp
    src/hardware/camera_stream.cpp
)

# Define header files (optional in CMake, but helps them show up in IDEs like Qt Creator/VS)
set(HEADERS
    src/ui/mainwindow.h
    src/ui/monitorpanel.h
    src/ui/controlpanel.h
    src/ui/components/traptable.h
    src/ui/components/patternview.h
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
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

# Tell CMake where to look for #include files so you don't need long relative paths
target_include_directories(${PROJECT_NAME} PRIVATE 
    src/ui
    src/ui/components
    src/core
    src/core/algorithms
    src/hardware
)

# Link the Qt libraries to your executable
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets
    # ${OpenCV_LIBS} # Uncomment when you add OpenCV
)

if (WIN32)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${_qt_bin_dir}/windeployqt.exe"
                --no-translations
                "$<TARGET_FILE:${PROJECT_NAME}>"
    )
endif()