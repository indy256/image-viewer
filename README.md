# JPEG Viewer

A single-window Qt Widgets application that opens in full-screen mode. Launch with a JPEG filename:

```powershell
& C:/projects/image-viewer-build/image-viewer.exe "C:/Pictures/photo.jpg"
```

Left and Right open the previous and next `.jpg` or `.jpeg` in the supplied
file's directory (case-insensitive alphabetical filename order, including hidden
files). Navigation stops at either end. The folder is watched for changes, so new
JPEGs appear automatically in alphabetical order while the current file stays selected.
Refresh is driven by filesystem events for the folder and individual JPEGs,
including ongoing copies and replacements. Events are grouped over 150 ms;
there is no periodic polling.
If the current file is removed, the nearest remaining file is selected; an empty
folder displays a waiting message until new JPEGs arrive.
Images fit the window with their aspect ratio preserved; EXIF orientation is applied.
The current image and up to 100 JPEGs on each side are cached at full resolution.
Background decoding prioritizes the current image and then its nearest neighbors.
Cached navigation is immediate; images still being decoded show a loading message.
Images outside this sliding range are evicted. Memory use depends on image dimensions.
Unreadable images show an error inside the window and navigation remains available.
Starting without a filename displays usage instructions. Press F to toggle full
screen (returning to the previous window state), and Esc to exit.
Double-click the image to toggle full-screen mode as well.
The first windowed view is centered and sized to the image, using at most 85%
of the available screen. Later switches restore the window's size and position.
In windowed mode, press and drag anywhere on the image to move the window
(also available through touch-generated mouse input).

Build with the local Qt 6.11.2 / MinGW installation:

```powershell
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;$env:PATH"
& C:/Qt/6.11.2/mingw_64/bin/qt-cmake.bat -S . -B ../image-viewer-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../image-viewer-build
& C:/Qt/6.11.2/mingw_64/bin/windeployqt.exe --release --no-translations ../image-viewer-build/image-viewer.exe
```
