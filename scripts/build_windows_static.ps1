param(
    [string]$SourceDir = "C:\xmrigCC",
    [string]$BuildDir = "C:\xmrigCC\build",
    [string]$DepsDir = "C:\xmrigcc-deps",
    [switch]$WithOpenCL,
    [switch]$WithCuda,
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$InDevShell
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Get-VsDevCmdPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $null
    }

    $installPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if ($LASTEXITCODE -ne 0 -or -not $installPath) {
        return $null
    }

    $devCmd = Join-Path $installPath.Trim() "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $devCmd) {
        return $devCmd
    }

    return $null
}

function Invoke-SelfInDevShell {
    if ($InDevShell -or (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
        return
    }

    $devCmd = Get-VsDevCmdPath
    if (-not $devCmd) {
        throw "cl.exe was not found in PATH, and VsDevCmd.bat could not be located. Run this from an x64 Native Tools Command Prompt for Visual Studio 2022."
    }

    Write-Step "cl.exe not found; relaunching inside Visual Studio x64 developer environment"

    $switches = @()
    if ($WithOpenCL) { $switches += "-WithOpenCL" }
    if ($WithCuda) { $switches += "-WithCuda" }
    if ($Clean) { $switches += "-Clean" }
    if ($SkipBuild) { $switches += "-SkipBuild" }
    $switchText = $switches -join " "

    $cmdFile = Join-Path $env:TEMP ("xmrigcc-build-devshell-{0}.cmd" -f ([guid]::NewGuid().ToString("N")))
    $cmdBody = @"
@echo off
call "$devCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSCommandPath" -SourceDir "$SourceDir" -BuildDir "$BuildDir" -DepsDir "$DepsDir" -InDevShell $switchText
exit /b %errorlevel%
"@

    Set-Content -Path $cmdFile -Value $cmdBody -Encoding ASCII
    try {
        & cmd.exe /d /c "`"$cmdFile`""
        exit $LASTEXITCODE
    }
    finally {
        Remove-Item -LiteralPath $cmdFile -Force -ErrorAction SilentlyContinue
    }
}

function Assert-Path {
    param(
        [string]$Path,
        [string]$Description
    )

    if (-not (Test-Path $Path)) {
        throw "$Description was not found: $Path"
    }
}

Invoke-SelfInDevShell

Write-Step "Checking build tools"
Assert-Command "cmake.exe"
Assert-Command "ninja.exe"
Assert-Command "cl.exe"

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DepsDir = [System.IO.Path]::GetFullPath($DepsDir)

Write-Step "Checking source and dependency directories"
Assert-Path (Join-Path $SourceDir "CMakeLists.txt") "XMRigCC source tree"
Assert-Path (Join-Path $DepsDir "include") "Dependency include directory"
Assert-Path (Join-Path $DepsDir "lib") "Dependency library directory"
Assert-Path (Join-Path $DepsDir "lib\libssl.lib") "Static OpenSSL SSL library"
Assert-Path (Join-Path $DepsDir "lib\libcrypto.lib") "Static OpenSSL crypto library"
Assert-Path (Join-Path $DepsDir "lib\uv.lib") "Static libuv library"
Assert-Path (Join-Path $DepsDir "lib\hwloc.lib") "Static hwloc library"
Assert-Path (Join-Path $DepsDir "lib\zlib.lib") "Static zlib library"

$zlibImportLibrary = Join-Path $DepsDir "lib\z.lib"
if (Test-Path $zlibImportLibrary) {
    Write-Step "Removing zlib DLL import library from dependency prefix"
    Remove-Item -LiteralPath $zlibImportLibrary -Force
}

$zlibDll = Join-Path $DepsDir "bin\z.dll"
if (Test-Path $zlibDll) {
    Write-Step "Removing zlib DLL from dependency prefix"
    Remove-Item -LiteralPath $zlibDll -Force
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Removing existing build directory"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$openclFlag = if ($WithOpenCL) { "ON" } else { "OFF" }
$cudaFlag = if ($WithCuda) { "ON" } else { "OFF" }

Write-Step "Configuring XMRigCC static Windows build"
cmake.exe `
    -S $SourceDir `
    -B $BuildDir `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DXMRIG_DEPS=$DepsDir" `
    -DBUILD_STATIC=ON `
    -DWITH_TLS=ON `
    -DWITH_HWLOC=ON `
    -DWITH_ZLIB=ON `
    -DZLIB_USE_STATIC_LIBS=ON `
    "-DZLIB_INCLUDE_DIR=$DepsDir\include" `
    "-DZLIB_LIBRARY=$DepsDir\lib\zlib.lib" `
    "-DWITH_OPENCL=$openclFlag" `
    "-DWITH_CUDA=$cudaFlag"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

if (-not $SkipBuild) {
    Write-Step "Building XMRigCC"
    cmake.exe --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
}

Write-Step "Build output"
Get-ChildItem -Path $BuildDir -Filter "*.exe" -ErrorAction SilentlyContinue |
    Select-Object FullName, Length, LastWriteTime |
    Format-Table -AutoSize

Write-Host ""
Write-Host "TLS is enabled with -DWITH_TLS=ON."
Write-Host "Build directory: $BuildDir"
