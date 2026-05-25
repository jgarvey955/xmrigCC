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

Write-Step "Checking winget"
Assert-Command "winget.exe"

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
