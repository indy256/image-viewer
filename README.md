# JPEG Viewer

[![Build](https://github.com/indy256/image-viewer/actions/workflows/build.yml/badge.svg)](https://github.com/indy256/image-viewer/actions/workflows/build.yml)

A single-window Qt Widgets application that opens in full-screen mode. Launch with a JPEG filename:

```powershell
& C:/projects/image-viewer-build/iv.exe "C:/Pictures/photo.jpg"
```

Browse JPEGs in the same folder alphabetically with the Left and Right arrow keys.
Photos automatically fit the window, and newly added files appear without restarting.
Press F or double-click to switch between fullscreen and windowed mode, drag the
image to move the window, and press Esc to exit. Your window size and position are
remembered when switching modes.

Build with the local Qt 6.11.2 / MinGW installation:

```powershell
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;$env:PATH"
& C:/Qt/6.11.2/mingw_64/bin/qt-cmake.bat -S . -B ../image-viewer-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../image-viewer-build
& C:/Qt/6.11.2/mingw_64/bin/windeployqt.exe --release --no-translations ../image-viewer-build/iv.exe
```

Every CI build uploads standalone downloads in the workflow run's Artifacts section:

- Windows x64: `iv.exe`, with Qt and the compiler runtime
  linked statically. No accompanying Qt DLLs or installation are needed.
- Linux x64: `iv.AppImage`. Make it executable with `chmod +x`
  and run it with a JPEG filename. Qt is bundled inside the AppImage.
- macOS ARM64: a ZIP containing `iv.app`, with its Qt frameworks and
  plugins inside the bundle. Extract it and run
  `iv.app/Contents/MacOS/iv /path/to/photo.jpg`.

Linux AppImages target the Ubuntu version used by `ubuntu-latest` and newer compatible systems.
They still depend on the host's standard system libraries and graphics drivers.
macOS packages are not Developer ID signed or notarized.

Windows Release builds enable link-time optimization (LTO), optimize for size,
and strip symbols with MinGW. Set `-DIMAGE_VIEWER_LTO=OFF` to disable LTO.
The prebuilt static Qt libraries are not rebuilt with LTO; optimization across
their internals would require an LTO-enabled Qt build.

Pushing a version tag such as `v1.0.0` builds all three packages and publishes
a GitHub Release with the standalone downloads attached, after every build succeeds:

```sh
git tag v1.0.0
git push origin v1.0.0
```

Local builds use the selected Qt installation. With shared Qt, the following
creates a portable archive containing the required DLLs rather than a single executable:

```sh
cpack --config ../image-viewer-build/CPackConfig.cmake -C Release -G ZIP -B dist
```
