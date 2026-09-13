# Third-party software

The application's GPL-3.0-only license does not replace the licenses or copyright
notices of third-party code. Downloaded dependencies and files carrying their own
license notices retain those terms, including the skills under `.agents/`.

| Dependency | Version | License information |
| --- | --- | --- |
| Qt Core, Gui, Widgets and image-format plugins | Selected Qt installation; WebP handler from Qt Image Formats 6.8.3 | Qt files offer alternative licenses; the bundled WebP handler offers LGPL-3.0-only, GPL-2.0-only, GPL-3.0-only, or commercial terms. See the notices in the selected Qt sources. |
| OpenJPEG | 2.5.4 | BSD-2-Clause; see its `LICENSE`, including the listed copyright holders. |
| libwebp | 1.6.0 | BSD-3-Clause and the additional patent grant in `PATENTS`; see `COPYING`. |
| libheif | 1.23.4 | Library: LGPL-3.0-or-later. Some wrappers, examples and tests have separate licenses; see `COPYING` and individual files. |
| libde265 | 1.1.2 | Library: LGPL-3.0-or-later. Example applications have separate terms; see `COPYING`. |
| libaom | 3.13.3 | BSD-2-Clause and the Alliance for Open Media patent license; see `LICENSE`, `PATENTS`, and notices for included third-party code. |

Pinned source URLs and checksums are recorded in `CMakeLists.txt`. CMake installs
the bundled codecs' license texts and Qt Image Formats notices under `licenses/`.
Qt runtime libraries and their dependencies vary by platform and installation;
their own notices also apply to distributed binaries.

When distributing binaries, preserve the applicable notices and provide the
corresponding source as required by their licenses. For GPL-covered binaries this
includes the application, applicable linked dependencies, modifications, and
build scripts—not just the application repository. The license files alone do
not constitute a source distribution.
