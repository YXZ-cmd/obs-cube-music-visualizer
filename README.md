# Cube Music Visualizer for OBS

A native OBS source for **OBS Studio 32.2.2, Windows 64-bit**. It listens to one
audio source already inside OBS and renders square frequency blocks from the
right edge toward the left on a transparent canvas.

There is no Python window, browser source, microphone permission, Windows audio
loopback, VB-CABLE, color key, or background process.

## Install a compiled release

1. Close OBS.
2. Extract the release ZIP directly into your OBS installation folder, normally
   `C:\Program Files\obs-studio`.
3. Confirm the DLL is located at
   `C:\Program Files\obs-studio\obs-plugins\64bit\obs-cube-music-visualizer.dll`.
4. Start OBS again.

## Use it

1. Add your music to OBS as its own audio source. `Application Audio Capture`
   pointed at Spotify is ideal.
2. In **Sources**, click **+** and choose **Cube Music Visualizer**.
3. Open its properties and choose your music source under
   **Audio Source (music only)**.
4. Leave the color white and place the visualizer against the right edge.

The source is natively transparent. Only the selected OBS source affects it.

## Default design

- 34 logarithmic frequency rows, bass at the bottom and treble at the top
- Independent square blocks
- Grows from right to left
- White with full alpha
- Fast rise and slower fall smoothing
- 900 x 720 transparent source canvas

## Building on Windows

This project uses the official OBS Plugin Template build system and targets the
OBS 31.1.1 plugin SDK, whose ABI is compatible with OBS Studio 32.2.2.

Requirements:

- Windows 10 or 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.30.5 or newer
- Git

Open **Developer PowerShell for VS 2022** in the project folder and run:

```powershell
./build_windows.bat
```

The build output appears below `build_x64`.

## License

GPL-2.0, matching the OBS Plugin Template.
