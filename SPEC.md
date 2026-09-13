# Image Viewer Specification

## Purpose

`iv` is a single-window desktop PNG, JPEG, JPEG 2000, WebP, HEIC, and AVIF viewer built with Qt Widgets. It opens a
specified image, supports fast keyboard navigation through its folder, and
updates the image list when the folder changes.

## Launch and file selection

```text
iv <path-to-image>
```

- Accept one image path, either absolute or relative to the process working
  directory. Paths containing spaces must be quoted in the shell.
- Open in fullscreen mode, including when displaying usage or error messages.
- Browse the supplied file's containing directory, without searching subfolders.
- Include files with `.png`, `.jpg`, `.jpeg`, `.jp2`, `.webp`, `.heic`, `.heif`, or `.avif` extensions, matched case-insensitively,
  including hidden files.
- Sort by filename in case-insensitive alphabetical order. Resolve equal
  case-insensitive names using case-sensitive order. Sorting is lexical, so
  `10.jpg` precedes `2.jpg`.
- Select the supplied file initially.
- On macOS, accept Finder Open With, file associations, and Dock file-open requests.
  Open the requested image in the existing window, switching the watched folder
  and preload cache when necessary. For multiple requests, the last image is selected.
- Display usage instructions when the application argument count is not one.
  Display an error if the supplied path is not an existing file with a supported image extension.

## Image presentation

- Show one image at a time in a single client area.
- Apply EXIF orientation during decoding.
- Center the image and scale it to fit the client area while preserving its
  aspect ratio. Images may be enlarged or reduced; they are not cropped.
- Use smooth scaling and a dark gray background (`#1c1c1c`) in unused areas.
- Refit the displayed image when the window size changes.
- Show loading, usage, waiting, and decoding-error messages as centered white
  text within the same window.

## Controls

| Input | Behavior |
| --- | --- |
| Left arrow | Select the preceding image in the sorted list. |
| Right arrow | Select the following image in the sorted list. |
| Mouse wheel up / down | Select the preceding / following image, one file per wheel notch. |
| F | Toggle fullscreen and windowed mode. Holding F does not repeatedly toggle. |
| Left-button double-click in the client area | Toggle fullscreen and windowed mode. |
| Esc | Close the viewer. |
| Left-button press and drag in windowed mode | Move the window through the platform's system-move operation. |

Navigation stops at the first and last files without wrapping. Touchpad or touch
gestures work when the operating system delivers the corresponding mouse events.
Dragging begins after the platform drag-distance threshold is reached.

## Window behavior

Fullscreen fills the screen without a title bar or frame. On Windows, fullscreen
also suppresses the compositor border and rounded corners so the viewer covers
the screen edges and corners.

Windowed mode has the platform title bar and resizable frame. On Windows, the
title bar immediately reflects the actual activation state when restored.
On Linux, the window manager controls fullscreen decorations. Returning to
windowed mode requests foreground stacking and keyboard focus on the next event
loop turn, without changing the native decoration flags.

The first switch to windowed mode centers the window on the available desktop.
Once an image is decoded, its dimensions determine the initial client size,
reduced proportionally when necessary so the outer window occupies at most 85%
of the available screen width and height. If the image is still loading, this
sizing is deferred until an image becomes available.

Later fullscreen transitions preserve the windowed size, position, and maximized
state within the current application session. Repeated transitions retain the
same restored size. Navigating to another image does not automatically resize an
already fitted window. Window state is not persisted between application runs.

For a selected file, the title has this format:

```text
CurrentDir/filename (position/total) - width × height - Image Viewer - Left / Right to navigate
```

`CurrentDir` is the containing folder's final name, not its full absolute path.
`position` is one-based. The title updates with the selection and file count.
Resolution is the decoded image's width and height in pixels, after orientation,
and appears once decoding finishes. It is omitted while loading or on a decoding error.
Usage, invalid-startup-path, and empty-folder views use `Image Viewer` as the title.

## Preloading and responsiveness

- Cache decoded images at full resolution for the current file and up to 5
  preceding and 5 following files: a sliding range of up to 11 files.
- Decode PNG, JPEG, JP2, WebP, HEIC, and AVIF images in background workers with at most seven requests
  in flight. Each JP2 request uses an independent OpenJPEG decoder.
- Prioritize the current selection, then its nearest neighbors.
- Display cached images without waiting for disk access or decoding. Display a
  loading message when the selected image is not yet cached.
- Evict cached entries outside the current range and invalidate changed files.
- Accept background results only when the file still belongs to the current
  cache range and its recorded size and modification time still match.
- Retain decoding failures as cache entries so unreadable files can display an
  error while navigation remains available.

Memory consumption depends on image dimensions; the cache has a file-count
limit rather than a fixed memory budget. Image decoding uses a 1024 MiB allocation
limit. JP2 decoding checks estimated raster and component-buffer sizes before
decoding; OpenJPEG's internal working memory is additional. This is separate from
the cache, and `QT_IMAGEIO_MAXALLOC` overrides the limit in MiB.
Navigation to uncached images depends
on storage and decoding speed. Closing the viewer waits for outstanding workers
to finish safely.

## Live folder updates

Use filesystem event notifications for the containing folder and individual
PNG, JPEG, JP2, WebP, HEIC/HEIF, or AVIF files. Group events with a 150 ms debounce before rescanning; there is no
periodic polling.

Rescans discover added, removed, renamed, and changed files, rebuild alphabetical
order, and update file watches. Individual file notifications cover writes to
existing files, while directory notifications support additions and replacements.
Change detection uses file size and last-modified time.

Preserve the selected path when it remains present. If it disappears, select the
file at its previous list index, clamped to the new list bounds. When the folder
has no PNG, JPEG, JP2, WebP, HEIC/HEIF, or AVIF files, display a waiting message and continue watching it. Select the
first available image when files appear in the empty folder.

## Build and distribution

The project uses C++17, CMake 3.21 or newer, and Qt 6.8 or newer with Widgets.
The executable target is `iv`. Windows builds also link the system DWM library.
JPEG 2000 support uses a static Qt image plugin backed by OpenJPEG 2.5.4.
PNG uses Qt's built-in image reader, including transparency, without an additional
image plugin. Transparent pixels show the viewer's background.
JPEG loading uses a statically linked libjpeg-turbo 3.1.3 decoder with required
SIMD acceleration. Each worker owns its decoder; RGB and grayscale images decode
through privately prefixed libjpeg symbols so static Qt and the accelerated
decoder cannot mix their internal implementations. Images decode
directly to display pixels, preserving ICC profiles and EXIF orientation. CMYK and
higher-precision JPEGs use Qt's reader. The configured image allocation limit also
bounds the accelerated output raster and, separately, decoder working buffers.
Initial configuration downloads the checksum-verified OpenJPEG source archive;
subsequent builds reuse it. A C compiler is required to build the codec.

With a suitable Qt installation configured:

```sh
qt-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Windows Release and MinSizeRel builds enable link-time optimization by default,
subject to a compiler-support check. `-DIMAGE_VIEWER_LTO=OFF` disables it. MinGW
builds optimize for size and strip symbols; MSVC builds use size optimization and
linker elimination of unused or identical code. Windows CI uses MSVC to build Qt Base 6.11.2
from checksum-verified sources with static linking, LTO, size optimization, and
bundled dependencies. Its installation is cached by toolchain and build-script
version. Linux and macOS retain prebuilt Qt.
With MinGW, the Windows platform plugins, Widgets accessibility sources, and widget-window implementation are excluded
from LTO to avoid MinGW duplicate-thunk errors while preserving accessibility.
The Qt build script also supports MSVC through `-Toolchain MSVC`, using separate
sources and build directories, unmodified Qt sources, LTO, and the static runtime.
`IMAGE_VIEWER_STATIC_RUNTIME=ON` selects `/MT` for the MSVC viewer and bundled codecs.

Local runtime deployment follows the selected Qt installation: shared Qt requires
its runtime libraries to accompany the executable. The Windows CI build uses
static Qt and compiler-runtime linking for the standalone executable.

| Distribution platform | Deliverable |
| --- | --- |
| Windows x64 | `iv-Windows-X64.exe`, with Qt and compiler runtime linked statically; Windows system libraries remain external. |
| Windows ARM64 | `iv-Windows-ARM64.exe`, a native ARM64 executable with Qt and compiler runtime linked statically; Windows system libraries remain external. |
| Linux x64 | `iv-Linux-X64.AppImage`, bundling Qt; requires executable permission and compatible host system libraries and graphics drivers. |
| macOS ARM64 | `iv-macOS-ARM64.zip`, containing `iv.app` with its Qt frameworks and plugins; a single archive with no nested ZIP. |

Linux packages target the Ubuntu version used by the build runner and compatible
newer systems. The macOS application is not Developer ID signed or notarized.
The Linux AppImage bundles the XCB/X11 client libraries, including XCB cursor and
XKB keyboard support, plus `libOpenGL.so.0` and `libGLdispatch.so.0`; its launcher
adds the bundled library directory to `LD_LIBRARY_PATH`. GPU drivers remain external.
The host supplies glibc and its ELF loader; these and glibc companion libraries
are excluded from deployment to avoid mixing incompatible runtime versions.

GitHub Actions builds Release packages on `ubuntu-latest`, `windows-latest`, `windows-11-arm`, and
`macos-latest` for pushes, pull requests, and manual workflow runs. Each build
uploads its platform artifact. Windows CI checks executable architecture and runtime DLL dependencies.
The Windows ARM64 job uses native ARM64 MSVC tools and a separate Qt cache.

An independent Windows x64 fast-build job uses cached prebuilt Qt 6.8.3 for
MSVC, disables LTO and static-runtime linking, and uploads `windows-x64-fast`.
This portable artifact includes `bin/iv.exe`, Qt DLLs, plugins, runtime files,
and license notices. It is available from workflow runs and is excluded from
release assets; release publication does not wait for this job.

Pushing a version tag matching `v*` publishes the four deliverables to a GitHub
Release after all platform builds succeed. An existing release for the tag is
updated with the generated binaries.

WebP support uses Qt Image Formats 6.8.3's static WebP plugin with libwebp 1.6.0.
Configuration downloads checksum-verified source archives. Lossy, lossless, and
transparent WebP images are supported; animated WebP displays its first frame.

HEIC and HEVC-encoded HEIF support uses a static Qt image plugin backed by
libheif 1.23.4 and libde265 1.1.2, built from checksum-verified source archives.
No system HEIC codec is required. The primary image is displayed; auxiliary
images, bursts, and sequences are not browsed separately. Container rotation,
mirroring, cropping, and transparency are applied. High-bit-depth images are
converted to 8-bit RGBA for display. Each background request uses its own decoder.

AVIF support shares the HEIF reader and uses bundled libaom 3.13.3 for AV1
decoding. The primary still image is displayed, including transparency and
container transformations. Animation playback is not supported. Building requires
Perl and an assembler (NASM or Yasm) on x86/x64.
