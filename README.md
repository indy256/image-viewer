# Image Viewer

[![Build](https://github.com/indy256/image-viewer/actions/workflows/build.yml/badge.svg)](https://github.com/indy256/image-viewer/actions/workflows/build.yml)

Licensed under the [GNU GPL v3.0 only](LICENSE) (`GPL-3.0-only`).
Third-party dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).

A single-window viewer for PNG (`.png`), JPEG (`.jpg`, `.jpeg`), JPEG 2000 (`.jp2`), WebP (`.webp`), HEIC/HEIF (`.heic`, `.heif`), and AVIF (`.avif`) images that opens in full-screen mode. Launch with an image filename:

```powershell
iv photo.jpg
```

Browse PNG, JPEG, JP2, WebP, HEIC, and AVIF images in the same folder alphabetically with the Left and Right arrow keys
or the mouse wheel (up for previous, down for next).
Photos automatically fit the window, and newly added files appear without restarting.
Press S to toggle sharpening and Up / Down to increase / decrease gamma for
the current image in 0.1 steps (default 1.0, range 0.1–4.0). Up lightens midtones;
Down darkens them, preserving black and white. Hold an arrow key to keep adjusting. Both effects
reset when another image loads and are never saved to the image file or remembered between runs.
Press F or double-click to switch between fullscreen and windowed mode, drag the
image to move the window, and press Esc to exit. Your window size and position are
remembered when switching modes. WebP supports transparency; animated WebP shows its first frame.
HEIC/HEIF and AVIF display the primary photo, with rotation and cropping applied.
AVIF animation playback is not supported.

Building AVIF requires Perl and, on x86/x64, NASM or Yasm on PATH.

Build with the local Qt 6.11.2 / MinGW installation. Initial configuration downloads
pinned OpenJPEG, libwebp, Qt Image Formats, libheif, libde265, and libaom sources. Image decoders
are linked into the viewer and need no extra runtime installation:

```powershell
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;$env:PATH"
& C:/Qt/6.11.2/mingw_64/bin/qt-cmake.bat -S . -B ../image-viewer-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../image-viewer-build
& C:/Qt/6.11.2/mingw_64/bin/windeployqt.exe --release --no-translations ../image-viewer-build/iv.exe
```

Every CI build uploads standalone downloads in the workflow run's Artifacts section:

- Windows x64: `iv-Windows-X64.exe`, with Qt and the compiler runtime
  linked statically. No accompanying Qt DLLs or installation are needed.
- Windows ARM64: `iv-Windows-ARM64.exe`, a native ARM64 executable with Qt
  and the compiler runtime linked statically.
- Linux x64: `iv-Linux-X64.AppImage`. Make it executable with `chmod +x`
  and run it with an image filename. Qt is bundled inside the AppImage.
- macOS ARM64: a ZIP containing `iv.app`, with its Qt frameworks and
  plugins inside the bundle. Extract it and run
  `iv.app/Contents/MacOS/iv /path/to/photo.jpg`.

A separate **Windows x64 fast build** uses cached, prebuilt Qt 6.8.3 with LTO
disabled. Download the `windows-x64-fast` artifact, extract it, and run `bin/iv.exe`.
Keep the accompanying Qt DLLs and plugins with it. This job runs independently
of the standalone builds and provides a workflow artifact, not a release asset.

Linux AppImages target the Ubuntu version used by `ubuntu-latest` and newer compatible systems.
They still depend on the host's standard system libraries and graphics drivers.
macOS packages are not Developer ID signed or notarized.

On macOS, `iv.app` accepts images opened from Finder or dropped onto its Dock icon,
including when the viewer is already running. To associate a format, move `iv.app`
to Applications, select an image in Finder, press Command-I, and choose **iv**
under **Open with**, then click **Change All**. Repeat for other extensions.

Windows Release builds enable link-time optimization (LTO), optimize for size,
and strip symbols with MinGW. Set `-DIMAGE_VIEWER_LTO=OFF` to disable LTO.
Windows CI uses MSVC and builds Qt Base 6.11.2 statically with LTO and size optimization.
The Qt installation is cached per toolchain and build-script version. Linux and
macOS use prebuilt Qt. `IMAGE_VIEWER_LTO` controls the application build; Qt's
LTO setting is determined when Qt itself is built.

For an alternative MinGW build locally, use the MSYS2 UCRT64 toolchain
and put its GCC, CMake, Ninja, and Perl on PATH, then run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-qt-windows.ps1 -WorkDirectory ../image-viewer-build/qt-ucrt-lto -InstallDirectory ../image-viewer-build/qt-ucrt-lto/install -Parallel 2
& ../image-viewer-build/qt-ucrt-lto/install/bin/qt-cmake.bat -S . -B ../image-viewer-build/iv-lto -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMAGE_VIEWER_STATIC_RUNTIME=ON -DIMAGE_VIEWER_LTO=ON
cmake --build ../image-viewer-build/iv-lto --parallel 2
```

Use the same compiler for Qt and the viewer: GCC LTO archives are compiler-version dependent.
The script applies MinGW LTO workarounds to the downloaded Qt sources (bigobj and
duplicate SIMD assembler macros) and disables GCC's problematic constructor/destructor decloning pass.
Qt's Windows platform plugins, Widgets accessibility files, and widget-window implementation use native objects
to avoid a MinGW LTO COMDAT-thunk bug; Core, Gui, and the remaining Widgets code use LTO.

To reproduce the Windows CI build, open an x64 Visual Studio developer PowerShell with CMake,
Ninja, Perl, and NASM on PATH, then use separate build and installation directories:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-qt-windows.ps1 -Toolchain MSVC -WorkDirectory ../image-viewer-build/qt-msvc-lto -InstallDirectory ../image-viewer-build/qt-msvc-lto/install -Parallel 2
& ../image-viewer-build/qt-msvc-lto/install/bin/qt-cmake.bat -S . -B ../image-viewer-build/iv-msvc-lto -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMAGE_VIEWER_STATIC_RUNTIME=ON -DIMAGE_VIEWER_LTO=ON
cmake --build ../image-viewer-build/iv-msvc-lto --parallel 2
```

The MSVC variant uses unmodified Qt sources with LTO and a static compiler runtime
(`/MT`). Windows CI uses MSVC; the script also retains MinGW support.

Windows ARM64 builds run on `windows-11-arm` with the ARM64 MSVC toolchain.
Each Windows architecture has its own cached Qt installation.

Pushing a version tag such as `v1.0.0` builds all four packages and publishes
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
