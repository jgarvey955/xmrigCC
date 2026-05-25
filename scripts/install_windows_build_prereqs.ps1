param(
    [switch]$SkipVisualStudio,
    [switch]$SkipGit,
    [switch]$SkipCMake,
    [switch]$SkipNinja,
    [switch]$SkipPerl
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

function Add-PathDirectory {
    param([string]$Directory)

    if ((Test-Path $Directory) -and
        (($env:PATH -split ';') -notcontains $Directory)) {
        $env:PATH = "$Directory;$env:PATH"
    }
}

function Download-File {
    param(
        [string]$Uri,
        [string]$OutFile
    )

    Write-Host "Downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing
}

function Install-AppxIfPresent {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Write-Host "Installing $Path"
        Add-AppxPackage -Path $Path -ErrorAction Stop
    }
}

function Install-WingetDependencies {
    param([string]$WorkDir)

    $vclibs = Join-Path $WorkDir "Microsoft.VCLibs.x64.14.00.Desktop.appx"
    Download-File "https://aka.ms/Microsoft.VCLibs.x64.14.00.Desktop.appx" $vclibs
    Install-AppxIfPresent $vclibs

    $xamlPackage = Join-Path $WorkDir "Microsoft.UI.Xaml.2.8.nupkg"
    $xamlDir = Join-Path $WorkDir "Microsoft.UI.Xaml.2.8"
    Download-File "https://www.nuget.org/api/v2/package/Microsoft.UI.Xaml/2.8.6" $xamlPackage

    if (Test-Path -LiteralPath $xamlDir) {
        Remove-Item -LiteralPath $xamlDir -Recurse -Force
    }

    Expand-Archive -LiteralPath $xamlPackage -DestinationPath $xamlDir -Force
    $xamlAppx = Join-Path $xamlDir "tools\AppX\x64\Release\Microsoft.UI.Xaml.2.8.appx"
    Install-AppxIfPresent $xamlAppx
}

function Repair-ExistingWingetPackage {
    $package = Get-AppxPackage -Name "Microsoft.DesktopAppInstaller" -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if (-not $package -or -not $package.InstallLocation) {
        return
    }

    $manifest = Join-Path $package.InstallLocation "AppXManifest.xml"
    if (Test-Path -LiteralPath $manifest) {
        Write-Step "Repairing existing App Installer registration"
        Add-AppxPackage -DisableDevelopmentMode -Register $manifest -ErrorAction SilentlyContinue
    }
}

function Ensure-Winget {
    Write-Step "Checking winget"

    Add-PathDirectory (Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps")
    if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
        return
    }

    Repair-ExistingWingetPackage
    Add-PathDirectory (Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps")
    if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
        return
    }

    Write-Step "Installing winget / App Installer"
    $workDir = Join-Path $env:TEMP ("winget-bootstrap-{0}" -f ([guid]::NewGuid().ToString("N")))
    New-Item -ItemType Directory -Force -Path $workDir | Out-Null

    try {
        Install-WingetDependencies $workDir

        $bundle = Join-Path $workDir "Microsoft.DesktopAppInstaller.msixbundle"
        Download-File "https://aka.ms/getwinget" $bundle
        Add-AppxPackage -Path $bundle -ErrorAction Stop
    }
    finally {
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    Add-PathDirectory (Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps")
    Assert-Command "winget.exe"
}

function Test-WingetPackageInstalled {
    param([string]$Id)

    & winget list --id $Id --exact --accept-source-agreements | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function Install-WingetPackage {
    param(
        [string]$Id,
        [string]$Name,
        [string[]]$ExtraArgs = @()
    )

    if (Test-WingetPackageInstalled $Id) {
        Write-Step "$Name is already installed"
        return
    }

    Write-Step "Installing $Name"
    $args = @(
        "install",
        "--id", $Id,
        "--exact",
        "--accept-package-agreements",
        "--accept-source-agreements",
        "--silent"
    ) + $ExtraArgs

    & winget @args
    if ($LASTEXITCODE -ne 0) {
        $exitCode = $LASTEXITCODE
        if (Test-WingetPackageInstalled $Id) {
            Write-Warning "winget returned $exitCode for $Name, but the package is installed. Continuing."
            return
        }

        throw "winget install failed for $Name ($Id) with exit code $exitCode."
    }
}

function Get-VSBuildToolsInstallPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $null
    }

    $path = & $vswhere `
        -products Microsoft.VisualStudio.Product.BuildTools `
        -latest `
        -property installationPath

    if ($LASTEXITCODE -eq 0 -and $path) {
        return $path.Trim()
    }

    return $null
}

function Ensure-VSBuildToolsWorkload {
    Write-Step "Checking Visual Studio C++ build tools workload"

    $installPath = Get-VSBuildToolsInstallPath
    $installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe"

    if (-not $installPath) {
        Write-Warning "Could not locate Visual Studio Build Tools with vswhere. If cl.exe is available in the x64 Native Tools prompt, you can continue."
        return
    }

    if (-not (Test-Path $installer)) {
        Write-Warning "Could not locate vs_installer.exe to verify/modify workloads. Install path: $installPath"
        return
    }

    & $installer modify `
        --installPath $installPath `
        --quiet `
        --wait `
        --norestart `
        --add Microsoft.VisualStudio.Workload.VCTools `
        --includeRecommended

    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio workload setup failed with exit code $LASTEXITCODE."
    }
}

Ensure-Winget

if (-not $SkipVisualStudio) {
    Install-WingetPackage `
        -Id "Microsoft.VisualStudio.2022.BuildTools" `
        -Name "Visual Studio 2022 Build Tools" `
        -ExtraArgs @(
            "--override",
            "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        )

    Ensure-VSBuildToolsWorkload
}

if (-not $SkipCMake) {
    Install-WingetPackage -Id "Kitware.CMake" -Name "CMake"
}

if (-not $SkipNinja) {
    Install-WingetPackage -Id "Ninja-build.Ninja" -Name "Ninja"
}

if (-not $SkipGit) {
    Install-WingetPackage -Id "Git.Git" -Name "Git"
}

if (-not $SkipPerl) {
    Install-WingetPackage -Id "StrawberryPerl.StrawberryPerl" -Name "Strawberry Perl"
}

Write-Step "Done"
Write-Host "Close this terminal and open a new x64 Native Tools Command Prompt for Visual Studio 2022."
Write-Host "Then run:"
Write-Host "  cd C:\xmrigCC"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\scripts\setup_windows_static_deps.ps1"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_static.ps1"
