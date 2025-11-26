#ifndef CONFIG_H
#define CONFIG_H

#include <QtGlobal>

namespace Config {

constexpr quint16 CONTROL_PORT      = 5000;
constexpr quint16 AUDIO_PORT_SEND   = 6000;
constexpr quint16 AUDIO_PORT_RECV   = 6001;
constexpr quint16 VIDEO_PORT_SEND   = 7000;
constexpr quint16 VIDEO_PORT_RECV   = 7001;
constexpr quint16 SCREEN_PORT_SEND  = 7100;
constexpr quint16 SCREEN_PORT_RECV  = 7101;

constexpr const char *DEFAULT_ROOM_ID = "default";

// Video sending interval (camera). Slightly reduced from 25 FPS
// to ease CPU and bandwidth pressure while keeping motion smooth.
constexpr int VIDEO_SEND_INTERVAL_MS = 66; // ~15 FPS

// Screen sharing capture / send parameters.
// Keep FPS modest so that CPU and bandwidth usage remain bounded.
constexpr int SCREEN_SHARE_FPS = 6; // ~5–8 FPS range
constexpr int SCREEN_SHARE_MAX_WIDTH  = 1280;
constexpr int SCREEN_SHARE_MAX_HEIGHT = 720;
constexpr int SCREEN_SHARE_JPEG_QUALITY = 50; // lower quality to reduce bitrate

// Approximate bandwidth cap for screen sharing (bytes per second).
// The goal is to keep this around 1–2 MB/s so that audio and camera
// video retain enough headroom on typical LAN links.
constexpr qint64 SCREEN_SHARE_MAX_BYTES_PER_SEC   = 1500000; // ~1.5 MB/s
constexpr qint64 SCREEN_SHARE_BW_WINDOW_MS        = 1000;    // sliding window size

} // namespace Config

#endif // CONFIG_H
