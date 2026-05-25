param(
    [string]$SourceDir = "C:\xmrigCC",
    [string]$DepsDir = "C:\xmrigcc-deps"
)

$ErrorActionPreference = "Continue"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Remove-Tree {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $protectedPath = [System.IO.Path]::GetFullPath("C:\Test").TrimEnd('\')
    if ($fullPath.TrimEnd('\').Equals($protectedPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith("$protectedPath\", [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Skipping protected path $fullPath"
        return
    }

    if (Test-Path -LiteralPath $Path) {
        Write-Host "Removing $fullPath"
        Remove-Item -LiteralPath $fullPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Remove-MatchingChildren {
    param(
        [string]$Path,
        [string[]]$Patterns
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    foreach ($pattern in $Patterns) {
        Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue -Filter $pattern |
            ForEach-Object { Remove-Tree $_.FullName }
    }
}

function Uninstall-WingetPackage {
    param(
        [string]$Id,
        [string]$Name
    )

    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        Write-Warning "winget.exe not found; skipping $Name"
        return
    }

    Write-Host "Uninstalling $Name ($Id)"
    winget uninstall --id $Id --exact --accept-source-agreements --silent
}

function Remove-Winget {
    Write-Host "Removing winget / App Installer"

    Remove-Tree (Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps\winget.exe")
    Remove-Tree (Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe")
    Remove-Tree (Join-Path $env:LOCALAPPDATA "Packages\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe")

    Get-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq "Microsoft.DesktopAppInstaller" } |
        ForEach-Object {
            Write-Host "Removing provisioned AppX package $($_.PackageName)"
            Remove-AppxProvisionedPackage -Online -PackageName $_.PackageName -ErrorAction SilentlyContinue | Out-Null
        }

    if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
        $packages = Get-AppxPackage -Name "Microsoft.DesktopAppInstaller" -AllUsers -ErrorAction SilentlyContinue
        if ($packages) {
            foreach ($package in $packages) {
                Write-Warning "Windows marks $($package.PackageFullName) as a protected system package; PowerShell cannot fully remove it on this build."
            }
        }
        else {
            Write-Warning "Windows is still exposing winget.exe even though the App Installer package was not listed."
        }
    }
}

Write-Host "Purging XMRigCC build state, caches, dependencies, and Windows build prerequisites."
Write-Host "SourceDir: $SourceDir"
Write-Host "DepsDir:   $DepsDir"
Write-Host "Protected: C:\Test"

Write-Step "Stopping XMRigCC processes"
"xmrig", "xmrigDaemon", "xmrigServer", "xmrigProxy" | ForEach-Object {
    Get-Process -Name $_ -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Step "Removing build output and dependency directories"
Remove-Tree (Join-Path $SourceDir "build")
Remove-Tree (Join-Path $SourceDir "build-debug")
Remove-Tree (Join-Path $SourceDir "build-release")
Remove-Tree (Join-Path $SourceDir "cmake-build-debug")
Remove-Tree (Join-Path $SourceDir "cmake-build-release")
Remove-Tree (Join-Path $SourceDir "CMakeCache.txt")
Remove-Tree (Join-Path $SourceDir "CMakeFiles")
Remove-Tree (Join-Path $SourceDir ".vs")
Remove-Tree $DepsDir

Write-Step "Removing generated binaries from the source root"
if (Test-Path -LiteralPath $SourceDir) {
    Get-ChildItem -LiteralPath $SourceDir -Force -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".exe", ".pdb", ".ilk", ".obj", ".lib", ".exp") } |
        ForEach-Object { Remove-Tree $_.FullName }
}

Write-Step "Removing runtime and temp caches"
Remove-Tree (Join-Path $env:LOCALAPPDATA "xmrig")
Remove-Tree (Join-Path $env:LOCALAPPDATA "xmrigCC")
Remove-Tree (Join-Path $env:APPDATA "xmrig")
Remove-Tree (Join-Path $env:APPDATA "xmrigCC")
Remove-Tree (Join-Path $env:TEMP "xmrig")
Remove-Tree (Join-Path $env:TEMP "xmrigCC")
Remove-MatchingChildren -Path $env:TEMP -Patterns @("xmrig*", "xmrigcc*", "cmake-*", "ninja-*")

Write-Step "Removing winget caches"
Remove-Tree (Join-Path $env:LOCALAPPDATA "Packages\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe\LocalCache")
Remove-Tree (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages")
Remove-Tree (Join-Path $env:TEMP "WinGet")

Write-Step "Uninstalling build prerequisites"
Uninstall-WingetPackage -Id "Microsoft.VisualStudio.2022.BuildTools" -Name "Visual Studio 2022 Build Tools"
Uninstall-WingetPackage -Id "Kitware.CMake" -Name "CMake"
Uninstall-WingetPackage -Id "Ninja-build.Ninja" -Name "Ninja"
Uninstall-WingetPackage -Id "Git.Git" -Name "Git"
Uninstall-WingetPackage -Id "StrawberryPerl.StrawberryPerl" -Name "Strawberry Perl"

Write-Step "Removing leftover prerequisite folders"
Remove-Tree "C:\Strawberry"

Write-Step "Removing winget"
Remove-Winget

Write-Step "Done"
