# 🔬 Holographic Optical Tweezer (HOT) Control

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Qt](https://img.shields.io/badge/Qt-6.0%2B-41CD52.svg)
![OpenCV](https://img.shields.io/badge/OpenCV-4.x-red.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

A high-performance, cross-platform graphical interface for controlling Holographic Optical Tweezers. Built with modern C++ and Qt6, this software calculates phase masks in real-time for Spatial Light Modulators (SLMs) and provides a live, zero-latency camera feed for active particle tracking and manipulation.

---

## ✨ Key Features

* **Dual-Engine Camera Stream:** Seamlessly toggle between Qt Native (WMF) for standard webcams and OpenCV (DirectShow) for high-speed scientific lab cameras and virtual drivers (e.g., OBS).
* **Real-Time Phase Mask Generation:** Implementations for Gerchberg-Saxton (GS) and Weighted GS algorithms to convert 2D target patterns into phase holograms.
* **Mathematical Hologram Generator:** A standalone utility to instantly generate mathematically perfect Blazed Gratings (Prisms) and Binary Gratings at any angle or pitch.
* **Hardware Persistence:** Automatically saves and loads precise SLM dimensions and pixel pitches via an integrated `.ini` configuration file.
* **Live Capture Suite:** Built-in tools for high-res snapshot capture and video recording of the active optical trap workspace, complete with live FPS monitoring.
* **Dark Mode UI:** A professional, low-eye-strain dark theme designed specifically for laser lab environments.

---

## 📸 Screenshots

*(Replace these placeholders with actual screenshots of your application!)*

| Main Control Interface | Standalone Phase Generator |
|:---:|:---:|
| <img src="docs/main_ui_placeholder.png" width="400" alt="Main UI"> | <img src="docs/generator_placeholder.png" width="400" alt="Hologram Generator"> |
| *Live camera feed, target pattern grid, and SLM mask monitor.* | *Analytical generation of blazed and binary gratings.* |

---

## 🛠️ Prerequisites

To build this project from source, you will need the following tools installed on your system:

* **CMake** (v3.16 or higher)
* **C++17 Compatible Compiler** (MSVC 2019/2022, GCC, or Clang)
* **Qt 6.x** (Required Modules: `Core`, `Gui`, `Widgets`, `Multimedia`, `MultimediaWidgets`)
* **OpenCV 4.x** (For DirectShow camera support and advanced image matrix operations)

---

