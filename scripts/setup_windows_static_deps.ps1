param(
    [string]$Prefix = "C:\xmrigcc-deps",
    [string]$LibuvVersion = "latest",
    [string]$HwlocVersion = "latest",
    [string]$OpenSSLVersion = "latest",
    [string]$ZlibVersion = "latest",
    [switch]$InDevShell
)

$ErrorActionPreference = "Stop"

$FallbackVersions = @{
    libuv   = "1.52.1"
    hwloc   = "2.13.0"
    openssl = "4.0.0"
    zlib    = "1.3.2"
}

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

function Add-ToolPathIfCommandMissing {
    param(
        [string]$CommandName,
        [string[]]$CandidateDirectories
    )

    if (Get-Command $CommandName -ErrorAction SilentlyContinue) {
        return $true
    }

    foreach ($directory in $CandidateDirectories) {
        $commandPath = Join-Path $directory $CommandName
        if (Test-Path $commandPath) {
            $env:PATH = "$directory;$env:PATH"
            return $true
        }
    }

    return $false
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

    $cmdFile = Join-Path $env:TEMP ("xmrigcc-deps-devshell-{0}.cmd" -f ([guid]::NewGuid().ToString("N")))
    $cmdBody = @"
@echo off
call "$devCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSCommandPath" -Prefix "$Prefix" -LibuvVersion "$LibuvVersion" -HwlocVersion "$HwlocVersion" -OpenSSLVersion "$OpenSSLVersion" -ZlibVersion "$ZlibVersion" -InDevShell
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

function Invoke-Logged {
    param(
        [string]$Name,
        [string]$WorkingDirectory,
        [string]$FilePath,
        [string[]]$Arguments
    )

    $logFile = Join-Path $script:LogsDir "$Name.log"
    Push-Location $WorkingDirectory
    try {
        Write-Host "Running: $FilePath $($Arguments -join ' ')"
        $stdoutFile = Join-Path $script:LogsDir "$Name.stdout.log"
        $stderrFile = Join-Path $script:LogsDir "$Name.stderr.log"
        Remove-Item -LiteralPath $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue

        $processArgs = @{
            FilePath = $FilePath
            WorkingDirectory = $WorkingDirectory
            NoNewWindow = $true
            Wait = $true
            PassThru = $true
            RedirectStandardOutput = $stdoutFile
            RedirectStandardError = $stderrFile
        }

        if ($Arguments -and $Arguments.Count -gt 0) {
            $processArgs.ArgumentList = $Arguments
        }

        $process = Start-Process @processArgs

        $exitCode = $process.ExitCode
        $output = @()
        if (Test-Path $stdoutFile) {
            $output += Get-Content $stdoutFile
        }
        if (Test-Path $stderrFile) {
            $output += Get-Content $stderrFile
        }

        $output | Set-Content -Path $logFile -Encoding ASCII
        if ($output.Count -gt 0) {
            if ($output.Count -gt 80) {
                Write-Host "... output truncated; full log: $logFile"
                $output | Select-Object -Last 80 | Write-Host
            }
            else {
                $output | Write-Host
            }
        }

        if ($exitCode -ne 0) {
            throw "$Name failed with exit code $exitCode. See $logFile"
        }
    }
    finally {
        Pop-Location
    }
}

function Get-WebText {
    param([string]$Uri)
    return (Invoke-WebRequest -Uri $Uri -UseBasicParsing -Headers @{ "User-Agent" = "xmrigcc-deps-setup" }).Content
}

function Resolve-LibuvVersion {
    if ($LibuvVersion -ne "latest") { return $LibuvVersion.TrimStart("v") }

    try {
        $html = Get-WebText "https://dist.libuv.org/dist/"
        $versions = [regex]::Matches($html, 'v([0-9]+\.[0-9]+\.[0-9]+)/') |
            ForEach-Object { [version]$_.Groups[1].Value } |
            Sort-Object -Descending

        if ($versions.Count -gt 0) {
            return $versions[0].ToString()
        }
    }
    catch {
        Write-Warning "Could not resolve latest libuv version, using fallback $($FallbackVersions.libuv): $_"
    }

    return $FallbackVersions.libuv
}

function Resolve-HwlocVersion {
    if ($HwlocVersion -ne "latest") { return $HwlocVersion }

    return $FallbackVersions.hwloc
}

function Resolve-OpenSSLVersion {
    if ($OpenSSLVersion -ne "latest") { return $OpenSSLVersion.TrimStart("openssl-") }

    try {
        $release = Invoke-RestMethod `
            -Uri "https://api.github.com/repos/openssl/openssl/releases/latest" `
            -Headers @{ "User-Agent" = "xmrigcc-deps-setup" }

        return ([string]$release.tag_name).Replace("openssl-", "")
    }
    catch {
        Write-Warning "Could not resolve latest OpenSSL version, using fallback $($FallbackVersions.openssl): $_"
    }

    return $FallbackVersions.openssl
}

function Resolve-ZlibVersion {
    if ($ZlibVersion -ne "latest") { return $ZlibVersion }

    try {
        $html = Get-WebText "https://zlib.net/"
        $patterns = @(
            'zlib source code,\s*version\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)',
            'Current release:\s*</[^>]+>\s*<[^>]+>\s*zlib\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)',
            'Current release:.*?zlib\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)'
        )

        foreach ($pattern in $patterns) {
            $match = [regex]::Match($html, $pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
            if ($match.Success) {
                return $match.Groups[1].Value
            }
        }

        $fossils = Get-WebText "https://zlib.net/fossils/"
        $versions = [regex]::Matches($fossils, 'zlib-([0-9]+\.[0-9]+(?:\.[0-9]+)?)\.tar\.gz') |
            ForEach-Object { [version]$_.Groups[1].Value } |
            Sort-Object -Descending

        if ($versions.Count -gt 0) {
            return $versions[0].ToString()
        }
    }
    catch {
        Write-Warning "Could not resolve latest zlib version, using fallback $($FallbackVersions.zlib): $_"
    }

    return $FallbackVersions.zlib
}

function Download-File {
    param(
        [string]$Uri,
        [string]$OutFile
    )

    if (Test-Path $OutFile) {
        Write-Host "Using existing download: $OutFile"
        return
    }

    Write-Host "Downloading $Uri"
    curl.exe -L $Uri -o $OutFile
    if ($LASTEXITCODE -ne 0) {
        throw "Download failed: $Uri"
    }
}

function Expand-TarGz {
    param(
        [string]$Archive,
        [string]$Destination
    )

    if (Test-Path $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    New-Item -ItemType Directory -Force (Split-Path -Parent $Destination) | Out-Null
    tar -xzf $Archive -C (Split-Path -Parent $Destination)
    if ($LASTEXITCODE -ne 0) {
        throw "Extract failed: $Archive"
    }
}

function Ensure-LibraryAlias {
    param(
        [string[]]$Candidates,
        [string]$Alias
    )

    $aliasPath = Join-Path $script:LibDir $Alias
    if (Test-Path $aliasPath) {
        return
    }

    foreach ($candidate in $Candidates) {
        $candidatePath = Join-Path $script:LibDir $candidate
        if (Test-Path $candidatePath) {
            Copy-Item $candidatePath $aliasPath -Force
            return
        }
    }
}

function Copy-HwlocInstall {
    param(
        [string]$SourceDir,
        [string]$BuildDir
    )

    Copy-Item (Join-Path $SourceDir "include\*") $script:IncludeDir -Recurse -Force

    $library = Get-ChildItem -Path $BuildDir -Recurse -File -Include "hwloc.lib", "libhwloc.lib" |
        Select-Object -First 1

    if (-not $library) {
        throw "Could not find built hwloc static library under $BuildDir."
    }

    Copy-Item $library.FullName (Join-Path $script:LibDir "hwloc.lib") -Force
}

Invoke-SelfInDevShell

Write-Step "Checking required tools"
Add-ToolPathIfCommandMissing "perl.exe" @(
    "C:\Strawberry\perl\bin",
    "C:\Strawberry\c\bin"
) | Out-Null

Assert-Command "cl.exe"
Assert-Command "nmake.exe"
Assert-Command "cmake.exe"
Assert-Command "ninja.exe"
Assert-Command "perl.exe"
Assert-Command "curl.exe"
Assert-Command "tar.exe"

$Prefix = [System.IO.Path]::GetFullPath($Prefix)
$script:IncludeDir = Join-Path $Prefix "include"
$script:LibDir = Join-Path $Prefix "lib"
$script:BinDir = Join-Path $Prefix "bin"
$script:SslDir = Join-Path $Prefix "ssl"
$script:DownloadsDir = Join-Path $Prefix "downloads"
$script:SrcDir = Join-Path $Prefix "src"
$script:BuildDir = Join-Path $Prefix "build"
$script:LogsDir = Join-Path $Prefix "logs"

Write-Step "Creating dependency prefix at $Prefix"
New-Item -ItemType Directory -Force `
    $script:IncludeDir,
    $script:LibDir,
    $script:BinDir,
    $script:SslDir,
    $script:DownloadsDir,
    $script:SrcDir,
    $script:BuildDir,
    $script:LogsDir | Out-Null

Write-Step "Resolving latest dependency versions"
$ResolvedLibuv = Resolve-LibuvVersion
$ResolvedHwloc = Resolve-HwlocVersion
$ResolvedOpenSSL = Resolve-OpenSSLVersion
$ResolvedZlib = Resolve-ZlibVersion

Write-Host "libuv:   $ResolvedLibuv"
Write-Host "hwloc:   $ResolvedHwloc"
Write-Host "OpenSSL: $ResolvedOpenSSL"
Write-Host "zlib:    $ResolvedZlib"

Write-Step "Building libuv $ResolvedLibuv"
$libuvArchive = Join-Path $script:DownloadsDir "libuv-v$ResolvedLibuv.tar.gz"
$libuvSource = Join-Path $script:SrcDir "libuv-v$ResolvedLibuv"
$libuvBuild = Join-Path $script:BuildDir "libuv"
Download-File "https://dist.libuv.org/dist/v$ResolvedLibuv/libuv-v$ResolvedLibuv.tar.gz" $libuvArchive
Expand-TarGz $libuvArchive $libuvSource
if (Test-Path $libuvBuild) { Remove-Item -LiteralPath $libuvBuild -Recurse -Force }
Invoke-Logged "libuv-configure" $Prefix "cmake.exe" @(
    "-S", $libuvSource,
    "-B", $libuvBuild,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DBUILD_TESTING=OFF",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
)
Invoke-Logged "libuv-build" $Prefix "cmake.exe" @("--build", $libuvBuild, "--config", "Release")
Invoke-Logged "libuv-install" $Prefix "cmake.exe" @("--install", $libuvBuild, "--config", "Release")
if (Test-Path (Join-Path $script:LibDir "libuv.lib")) {
    Copy-Item (Join-Path $script:LibDir "libuv.lib") (Join-Path $script:LibDir "uv.lib") -Force
}
else {
    Ensure-LibraryAlias @("uv_a.lib") "uv.lib"
}
Remove-Item -LiteralPath (Join-Path $script:BinDir "uv.dll") -Force -ErrorAction SilentlyContinue

Write-Step "Building hwloc $ResolvedHwloc"
$hwlocMajorMinor = ([version]$ResolvedHwloc).Major.ToString() + "." + ([version]$ResolvedHwloc).Minor.ToString()
$hwlocArchive = Join-Path $script:DownloadsDir "hwloc-$ResolvedHwloc.tar.gz"
$hwlocSource = Join-Path $script:SrcDir "hwloc-$ResolvedHwloc"
$hwlocBuild = Join-Path $script:BuildDir "hwloc"
Download-File "https://download.open-mpi.org/release/hwloc/v$hwlocMajorMinor/hwloc-$ResolvedHwloc.tar.gz" $hwlocArchive
Expand-TarGz $hwlocArchive $hwlocSource
if (Test-Path $hwlocBuild) { Remove-Item -LiteralPath $hwlocBuild -Recurse -Force }

$hwlocCMakeSource = Join-Path $hwlocSource "contrib\windows-cmake"
if (-not (Test-Path $hwlocCMakeSource)) {
    throw "hwloc Windows CMake project not found at $hwlocCMakeSource."
}

Invoke-Logged "hwloc-configure" $Prefix "cmake.exe" @(
    "-S", $hwlocCMakeSource,
    "-B", $hwlocBuild,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
)
Invoke-Logged "hwloc-build" $Prefix "cmake.exe" @("--build", $hwlocBuild, "--config", "Release")
try {
    Invoke-Logged "hwloc-install" $Prefix "cmake.exe" @("--install", $hwlocBuild, "--config", "Release")
}
catch {
    Write-Warning "hwloc install target was not available; copying headers and library manually."
    Copy-HwlocInstall $hwlocSource $hwlocBuild
}
Ensure-LibraryAlias @("libhwloc.lib") "hwloc.lib"

Write-Step "Building OpenSSL $ResolvedOpenSSL with static TLS libraries"
$opensslArchive = Join-Path $script:DownloadsDir "openssl-$ResolvedOpenSSL.tar.gz"
$opensslSource = Join-Path $script:SrcDir "openssl-$ResolvedOpenSSL"
Download-File "https://github.com/openssl/openssl/releases/download/openssl-$ResolvedOpenSSL/openssl-$ResolvedOpenSSL.tar.gz" $opensslArchive
Expand-TarGz $opensslArchive $opensslSource
Invoke-Logged "openssl-configure" $opensslSource "perl.exe" @(
    "Configure",
    "VC-WIN64A",
    "no-shared",
    "enable-static-vcruntime",
    "no-asm",
    "no-zlib",
    "no-comp",
    "no-dgram",
    "no-filenames",
    "no-cms",
    "no-tests",
    "--release",
    "--prefix=$Prefix",
    "--openssldir=$script:SslDir",
    "--libdir=lib"
)
Invoke-Logged "openssl-build" $opensslSource "nmake.exe" @()
Invoke-Logged "openssl-install" $opensslSource "nmake.exe" @("install_sw")

Write-Step "Building zlib $ResolvedZlib"
$zlibArchive = Join-Path $script:DownloadsDir "zlib-$ResolvedZlib.tar.gz"
$zlibSource = Join-Path $script:SrcDir "zlib-$ResolvedZlib"
$zlibBuild = Join-Path $script:BuildDir "zlib"
Download-File "https://zlib.net/fossils/zlib-$ResolvedZlib.tar.gz" $zlibArchive
Expand-TarGz $zlibArchive $zlibSource
if (Test-Path $zlibBuild) { Remove-Item -LiteralPath $zlibBuild -Recurse -Force }
Invoke-Logged "zlib-configure" $Prefix "cmake.exe" @(
    "-S", $zlibSource,
    "-B", $zlibBuild,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
)
Invoke-Logged "zlib-build" $Prefix "cmake.exe" @("--build", $zlibBuild, "--config", "Release")
Invoke-Logged "zlib-install" $Prefix "cmake.exe" @("--install", $zlibBuild, "--config", "Release")
Ensure-LibraryAlias @("zs.lib", "zlibstatic.lib") "zlib.lib"
Remove-Item -LiteralPath (Join-Path $script:BinDir "z.dll") -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $script:LibDir "z.lib") -Force -ErrorAction SilentlyContinue

Write-Step "Writing manifest and environment helper"
$manifest = [ordered]@{
    prefix = $Prefix
    generated_at = (Get-Date).ToString("o")
    runtime = "MSVC static runtime (/MT)"
    dependencies = [ordered]@{
        libuv = $ResolvedLibuv
        hwloc = $ResolvedHwloc
        openssl = $ResolvedOpenSSL
        zlib = $ResolvedZlib
    }
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $Prefix "manifest.json") -Encoding ASCII

@"
`$env:XMRIG_DEPS = "$Prefix"
Write-Host "XMRIG_DEPS=$Prefix"
Write-Host "Use: cmake -S C:\xmrigCC -B C:\xmrigCC\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXMRIG_DEPS=$Prefix -DBUILD_STATIC=ON -DWITH_TLS=ON -DWITH_HWLOC=ON -DWITH_ZLIB=ON -DWITH_OPENCL=OFF -DWITH_CUDA=OFF"
"@ | Set-Content -Path (Join-Path $Prefix "xmrigcc-deps-env.ps1") -Encoding ASCII

Write-Step "Done"
Write-Host "Static dependency prefix: $Prefix"
Write-Host "Manifest: $(Join-Path $Prefix "manifest.json")"
Write-Host "Environment helper: $(Join-Path $Prefix "xmrigcc-deps-env.ps1")"
Write-Host ""
Write-Host "Configure XMRigCC with:"
Write-Host "cmake -S C:\xmrigCC -B C:\xmrigCC\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXMRIG_DEPS=$Prefix -DBUILD_STATIC=ON -DWITH_TLS=ON -DWITH_HWLOC=ON -DWITH_ZLIB=ON -DWITH_OPENCL=OFF -DWITH_CUDA=OFF"
