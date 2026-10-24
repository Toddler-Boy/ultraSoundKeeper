# Changelog

## 1.0.0 (2026-10-24)

First release. Started from Sound Keeper 1.3.6, rewritten from the ground up.

- Tray icon with a right-click menu: stream mode, frequency, volume, outputs, sleep conditions, pause, start with Windows
- Settings are stored in the registry and take effect at once; the exe name means nothing
- Silence is the default stream, Fluctuate is one click away
- Event-driven WASAPI with the engine's smallest buffer instead of a timer and a one second buffer
- Volume ramps over 10 ms between values, frequency changes keep the phase, the noise source runs continuously
- Windows 11, 64-bit only, no C runtime, about 19 KB
