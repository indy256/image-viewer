# Image Viewer Specification

## Purpose

`iv` is a single-window desktop JPEG viewer built with Qt Widgets. It opens a
specified image, supports fast keyboard navigation through its folder, and
updates the image list when the folder changes.

## Launch and file selection

```text
iv <path-to-jpeg>
```

- Accept one image path, either absolute or relative to the process working
  directory. Paths containing spaces must be quoted in the shell.
- Open in fullscreen mode, including when displaying usage or error messages.
- Browse the supplied file's containing directory, without searching subfolders.
- Include files with `.jpg` or `.jpeg` extensions, matched case-insensitively,
  including hidden files.
- Sort by filename in case-insensitive alphabetical order. Resolve equal
  case-insensitive names using case-sensitive order. Sorting is lexical, so
  `10.jpg` precedes `2.jpg`.
- Select the supplied file initially.
- Display usage instructions when the application argument count is not one.
  Display an error if the supplied path is not an existing JPEG-named file.

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
| Left arrow | Select the preceding JPEG in the sorted list. |
| Right arrow | Select the following JPEG in the sorted list. |
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
CurrentDir/filename (position/total) - JPEG Viewer - Left / Right to navigate
```

`CurrentDir` is the containing folder's final name, not its full absolute path.
`position` is one-based. The title updates with the selection and file count.
Usage, invalid-startup-path, and empty-folder views use `JPEG Viewer` as the title.

## Preloading and responsiveness

- Cache decoded images at full resolution for the current file and up to 100
  preceding and 100 following files: a sliding range of up to 201 files.
- Decode in background workers with at most two requests in flight.
- Prioritize the current selection, then its nearest neighbors.
- Display cached images without waiting for disk access or decoding. Display a
  loading message when the selected image is not yet cached.
- Evict cached entries outside the current range and invalidate changed files.
- Accept background results only when the file still belongs to the current
  cache range and its recorded size and modification time still match.
- Retain decoding failures as cache entries so unreadable files can display an
  error while navigation remains available.

Memory consumption depends on image dimensions; the cache has a file-count
limit rather than a fixed memory budget. Navigation to uncached images depends
on storage and decoding speed. Closing the viewer waits for outstanding workers
to finish safely.

## Live folder updates

Use filesystem event notifications for the containing folder and individual
JPEG files. Group events with a 150 ms debounce before rescanning; there is no
periodic polling.

Rescans discover added, removed, renamed, and changed files, rebuild alphabetical
order, and update file watches. Individual file notifications cover writes to
existing files, while directory notifications support additions and replacements.
Change detection uses file size and last-modified time.

Preserve the selected path when it remains present. If it disappears, select the
file at its previous list index, clamped to the new list bounds. When the folder
has no JPEG files, display a waiting message and continue watching it. Select the
first available image when files appear in the empty folder.

## Build and distribution

The project uses C++17, CMake 3.21 or newer, and Qt 6.8 or newer with Widgets.
The executable target is `iv`. Windows builds also link the system DWM library.

With a suitable Qt installation configured:

```sh
qt-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Windows Release and MinSizeRel builds enable link-time optimization by default,
subject to a compiler-support check. `-DIMAGE_VIEWER_LTO=OFF` disables it. MinGW
builds optimize for size and strip symbols; MSVC builds use size optimization and
linker elimination of unused or identical code. Prebuilt Qt libraries are not
rebuilt with LTO.

Local runtime deployment follows the selected Qt installation: shared Qt requires
its runtime libraries to accompany the executable. The Windows CI build uses
static Qt and compiler-runtime linking for the standalone executable.

| Distribution platform | Deliverable |
| --- | --- |
| Windows x64 | `iv.exe`, with Qt and compiler runtime linked statically; Windows system libraries remain external. |
| Linux x64 | `iv.AppImage`, bundling Qt; requires executable permission and compatible host system libraries and graphics drivers. |
| macOS ARM64 | `iv-macOS-ARM64.zip`, containing `iv.app` with its Qt frameworks and plugins; a single archive with no nested ZIP. |

Linux packages target the Ubuntu version used by the build runner and compatible
newer systems. The macOS application is not Developer ID signed or notarized.

GitHub Actions builds Release packages on `ubuntu-latest`, `windows-latest`, and
`macos-latest` for pushes, pull requests, and manual workflow runs. Each build
uploads its platform artifact. Windows CI checks runtime dependencies and startup
without Qt on `PATH`.

Pushing a version tag matching `v*` publishes the three deliverables to a GitHub
Release after all platform builds succeed. An existing release for the tag is
updated with the generated binaries.
