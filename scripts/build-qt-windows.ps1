param(
    [Parameter(Mandatory = $true)][string]$WorkDirectory,
    [Parameter(Mandatory = $true)][string]$InstallDirectory,
    [int]$Parallel = 2,
    [ValidateSet('MinGW', 'MSVC')][string]$Toolchain = 'MinGW'
)

$ErrorActionPreference = 'Stop'
$version = '6.11.2'
$sha256 = '5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22'
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
$InstallDirectory = [IO.Path]::GetFullPath($InstallDirectory)
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
$archive = Join-Path $WorkDirectory "qtbase-$version.tar.xz"
$sourceRoot = Join-Path $WorkDirectory $Toolchain.ToLowerInvariant()
New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
$source = Join-Path $sourceRoot "qtbase-everywhere-src-$version"
$build = Join-Path $WorkDirectory "qtbase-$($Toolchain.ToLowerInvariant())-lto-build"
$ltoOverrides = Join-Path $PSScriptRoot 'qt-mingw-lto.cmake'

if (-not (Test-Path -LiteralPath $archive)) {
    & "$env:SystemRoot/System32/curl.exe" -fL --retry 3 "https://download.qt.io/official_releases/qt/6.11/$version/submodules/qtbase-everywhere-src-$version.tar.xz" -o $archive
    if ($LASTEXITCODE -ne 0) { throw 'Qt source download failed' }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) {
    throw 'Qt source checksum mismatch'
}
if (-not (Test-Path -LiteralPath "$source/configure.bat")) {
    & "$env:SystemRoot/System32/tar.exe" -xf $archive -C $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw 'Qt source extraction failed' }
}
$compilerOptions = @('-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
if ($Toolchain -eq 'MinGW') {
    $compilerOptions = @('-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++',
        '-DCMAKE_CXX_FLAGS=-fno-declone-ctor-dtor', "-DCMAKE_PROJECT_QtBase_INCLUDE=$ltoOverrides")
    # GCC's LTO plugin cannot read MinGW bigobj files. Slim LTO objects do not
    # need Qt's unconditional bigobj flag; retain normal COFF for LTO processing.
    $targetsFile = Join-Path $source 'cmake/QtInternalTargets.cmake'
    $targets = [IO.File]::ReadAllText($targetsFile)
    $bigobj = 'target_compile_options(PlatformCommonInternal INTERFACE -Wa,-mbig-obj)'
    if ($targets.Contains($bigobj)) {
        [IO.File]::WriteAllText($targetsFile, $targets.Replace($bigobj,
            '# bigobj disabled for the MinGW LTO build (see build-qt-windows.ps1).'))
    }
    # LTO combines translation units; define Qt's assembler macros only once per
    # assembler unit, while preserving its workaround for unaligned AVX accesses.
    $simdFile = Join-Path $source 'src/corelib/global/qsimd_p.h'
    $simd = [IO.File]::ReadAllText($simdFile)
    if (-not $simd.Contains('qt_mingw_avx_macros')) {
        $simd = $simd.Replace('".macro vmovapd args:vararg\n"',
            '".ifndef qt_mingw_avx_macros\n" " .set qt_mingw_avx_macros, 1\n" ".macro vmovapd args:vararg\n"')
        $simd = [regex]::Replace($simd, '("    vmovdqu64 \\\\args\\n"\s*"\.endm\\n")', '$1 ".endif\n"')
        [IO.File]::WriteAllText($simdFile, $simd)
    }
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    & "$source/configure.bat" -prefix $InstallDirectory -release -static -static-runtime `
        -ltcg -optimize-size -opensource -confirm-license -nomake examples -nomake tests `
        -no-opengl -no-dbus -no-openssl -qt-zlib -qt-pcre -qt-libb2 -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz `
        -no-feature-network -no-feature-sql -no-feature-testlib -no-feature-concurrent `
        -no-feature-xml -no-feature-printsupport -no-feature-printpreviewwidget -no-feature-printer `
        -no-feature-zstd -no-feature-brotli -no-feature-windeployqt `
        -no-feature-system-libb2 -no-feature-openssl `
        -- @compilerOptions
    if ($LASTEXITCODE -ne 0) { throw 'Qt configure failed' }
    if (-not (Select-String -LiteralPath "$build/CMakeCache.txt" -Pattern '^QT_FEATURE_ltcg:INTERNAL=ON$|^QT_FEATURE_ltcg:INTERNAL=1$' -Quiet)) {
        throw 'Qt did not enable LTO'
    }
    & cmake --build . --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw 'Qt build failed' }
    & cmake --install .
    if ($LASTEXITCODE -ne 0) { throw 'Qt installation failed' }
} finally {
    Pop-Location
}
exit 0
