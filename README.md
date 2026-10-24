# ultraSoundKeeper

A tiny Windows tray tool that keeps a digital audio output awake.

Many AV receivers and monitors drop the HDMI or SPDIF audio link as soon as nothing plays and show "no signal". When sound starts again they need a second or two to lock on, so every video, every game and every notification begins with a hole. ultraSoundKeeper keeps an inaudible stream open on the output, the link never drops, and sound starts when it starts.

## Features

- Keeps the default output, all outputs, digital outputs (HDMI and SPDIF) or analog outputs awake
- Stream modes: Silence, Fluctuate, Sine, White noise, Pink noise, Open only
- Frequency and volume for the modes that have one, from the menu
- Every change is heard at once, volume changes ramp over 10 ms, nothing pops or restarts
- Sleeps while the PC sleeps, optionally while the display is off or the session is locked
- Pause and resume from the menu
- Follows the light or dark taskbar theme and the display scaling
- Optional start with Windows (right-click menu)
- About 19 KB, no installer, no runtime, no settings files

## Modes

- **Silence** streams digital zeros. Enough for every receiver that keys on the link being present, and the default.
- **Fluctuate** streams zeros with the smallest possible step every few samples, for hardware that treats zeros as no audio. The frequency is the number of steps per second.
- **Sine** plays a tone at the chosen frequency and volume. 1 Hz is below hearing; 100 Hz at 10 % is for testing.
- **White noise** and **Pink noise** play noise at the chosen volume, for analog outputs that need a real signal.
- **Open only** opens the output without streaming anything. Rarely enough, but free.

Fluctuate feeds the Dolby Atmos encoder in Windows something it turns into a faint hum on some setups. That is why Silence is the default here.

## Requirements

- Windows 11, 64-bit

## Usage

Run `ultraSoundKeeper.exe` from the folder you want to keep it in. On first launch it adds itself to the Windows startup list; untick "Start with Windows" in the right-click menu to turn that off.

Right-click the icon for Mode, Frequency, Volume, Outputs, Sleep, Start with Windows, Pause and Exit. Settings live under `HKCU\Software\ultraSoundKeeper`.

Windows 11 puts new tray icons into the overflow menu (^). Drag the icon onto the taskbar once and Windows remembers its place.

Starting it a second time replaces the running instance, a running Sound Keeper included. `ultraSoundKeeper.exe kill` stops the running instance without starting a new one.

## Building

Needs Visual Studio 2026 with the Clang toolset and CMake 4.2 or newer.

```
cmake --preset vs
cmake --build --preset vs --config Release
```

The exe ends up in `Builds/vs/Release`.

## License

MIT, see [License.md](License.md).

## Credits

Started as a fork of [Sound Keeper](https://github.com/vrubleg/soundkeeper) by Evgeny Vrublevsky. The code is a rewrite, the device handling follows his.
