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

} // namespace Config

#endif // CONFIG_H
