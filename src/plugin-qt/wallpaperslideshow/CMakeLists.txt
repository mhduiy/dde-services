# SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(BIN_NAME "plugin-qt-wallpaperslideshow")

project(${BIN_NAME})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus WaylandClient)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Gui Core)
find_package(TreelandProtocols REQUIRED)

qt_add_dbus_adaptor(ADAPTER_SOURCES
                     org.deepin.dde.WallpaperSlideshow.xml
                     wallpaperslideshow.h
                     WallpaperSlideshow)

add_library(${BIN_NAME} MODULE
    ${SRCS}
    ${ADAPTER_SOURCES}
)

qt6_generate_wayland_protocol_client_sources(${BIN_NAME} FILES
    ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-wallpaper-manager-unstable-v1.xml
)

target_compile_definitions(${BIN_NAME} PRIVATE Enable_Treeland)

target_include_directories(${BIN_NAME} PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::DBus
)

target_link_libraries(${BIN_NAME} PRIVATE
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::DBus
  Qt${QT_VERSION_MAJOR}::WaylandClient
  Qt${QT_VERSION_MAJOR}::WaylandClientPrivate
  Dtk${DTK_VERSION_MAJOR}::Core
  Dtk${DTK_VERSION_MAJOR}::Gui
)

install(TARGETS ${BIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-qt-wallpaperslideshow.json DESTINATION share/deepin-service-manager/user/)
