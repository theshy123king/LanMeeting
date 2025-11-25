cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOGEN_VERBOSE ON)

project(LanMeeting VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_COMPILER_ABI_FILE "${PROJECT_SOURCE_DIR}/cmake/CMakeCXXCompilerABI.cpp")

# Optional FFmpeg root (for H.264 support)
set(FFMPEG_ROOT "D:/dev/ffmpeg" CACHE PATH "Root directory of FFmpeg installation")
option(ENABLE_FFMPEG "Enable FFmpeg-based H.264 encoding/decoding" ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets)

set(PROJECT_SOURCES
        src/main.cpp
        src/ui/mainwindow.cpp
        src/ui/mainwindow.h
        src/ui/mainwindow.ui
        src/ui/ChatMessageWidget.cpp
        src/ui/ChatMessageWidget.h
        src/ui/ScreenShareWidget.cpp
        src/ui/ScreenShareWidget.h
        src/audio/AudioEngine.cpp
        src/audio/AudioEngine.h
        src/audio/AudioTransport.cpp
        src/audio/AudioTransport.h
        src/common/Logger.cpp
        src/common/Logger.h
        src/media/VideoEncoder.cpp
        src/media/VideoEncoder.h
        src/media/VideoDecoder.cpp
        src/media/VideoDecoder.h
        src/media/MediaTransport.cpp
        src/media/MediaTransport.h
        src/media/MediaEngine.cpp
        src/media/MediaEngine.h
        src/media/ScreenShareTransport.cpp
        src/media/ScreenShareTransport.h
        src/net/ControlServer.cpp
        src/net/ControlServer.h
        src/net/ControlClient.cpp
        src/net/ControlClient.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(LanMeeting
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET LanMeeting APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(LanMeeting SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(LanMeeting
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_compile_features(LanMeeting PRIVATE cxx_std_17)

target_link_libraries(LanMeeting PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::MultimediaWidgets
)

if(ENABLE_FFMPEG)
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

    include_directories(${FFMPEG_INCLUDE_DIR})
    link_directories(${FFMPEG_LIB_DIR})
    include_directories("D:/dev/ffmpeg/include")
    link_directories("D:/dev/ffmpeg/lib")

    # MinGW: explicitly look for import libraries lib*.dll.a
    find_library(FFMPEG_AVCODEC NAMES libavcodec.dll.a   HINTS ${FFMPEG_LIB_DIR})
    find_library(FFMPEG_AVFORMAT NAMES libavformat.dll.a HINTS ${FFMPEG_LIB_DIR})
    find_library(FFMPEG_AVUTIL   NAMES libavutil.dll.a   HINTS ${FFMPEG_LIB_DIR})
    find_library(FFMPEG_SWSCALE  NAMES libswscale.dll.a  HINTS ${FFMPEG_LIB_DIR})
    find_library(FFMPEG_SWRESAMPLE NAMES libswresample.dll.a HINTS ${FFMPEG_LIB_DIR})

    if(FFMPEG_AVCODEC AND FFMPEG_AVFORMAT AND FFMPEG_AVUTIL)
        set(FFMPEG_FOUND TRUE)
    else()
        set(FFMPEG_FOUND FALSE)
        message(WARNING "FFmpeg libraries not found in ${FFMPEG_LIB_DIR}. H.264 support will be disabled.")
    endif()

    if(FFMPEG_FOUND)
        target_link_libraries(LanMeeting PRIVATE
            ${FFMPEG_AVCODEC}
            ${FFMPEG_AVFORMAT}
            ${FFMPEG_AVUTIL}
        )
        if(FFMPEG_SWSCALE)
            target_link_libraries(LanMeeting PRIVATE ${FFMPEG_SWSCALE})
        endif()
        if(FFMPEG_SWRESAMPLE)
            target_link_libraries(LanMeeting PRIVATE ${FFMPEG_SWRESAMPLE})
        endif()
        target_compile_definitions(LanMeeting PRIVATE USE_FFMPEG_H264)

    endif()
endif()

target_include_directories(LanMeeting PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.LanMeeting)
endif()
set_target_properties(LanMeeting PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS LanMeeting
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(LanMeeting)
endif()
