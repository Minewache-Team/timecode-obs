# install.ps1 - Install obs-ltc-timecode plugin for OBS Studio (Windows)
#
# Usage:
#   .\install.ps1                    # Install from build output (build_x64\Release)
#   .\install.ps1 -BuildDir "path"   # Install from custom build directory
#   .\install.ps1 -Uninstall         # Remove the plugin
#
# IMPORTANT: OBS Studio loads plugins from C:\ProgramData\obs-studio\plugins\
# NOT from %APPDATA%\obs-studio\plugins\ (AppData is only for settings/config).

param(
    [string]$BuildDir = "",
    [switch]$Uninstall = $false
)

$ErrorActionPreference = "Stop"

$PluginName = "obs-ltc-timecode"
$ObsProgramData = "$env:ProgramData\obs-studio"
$PluginDir = "$ObsProgramData\plugins\$PluginName"
$DllName = "$PluginName.dll"

# Legacy wrong path (for cleanup/migration)
$LegacyPluginDir = "$env:APPDATA\obs-studio\plugins\$PluginName"

function Write-Header {
    Write-Host ""
    Write-Host "=== obs-ltc-timecode Installer ===" -ForegroundColor Cyan
    Write-Host ""
}

function Find-BuildDir {
    # Try common build output locations
    $candidates = @(
        "build_x64\Release",
        "build_x64\RelWithDebInfo",
        "build_x64\Debug",
        "build\Release",
        "build\RelWithDebInfo"
    )

    foreach ($dir in $candidates) {
        $dllPath = Join-Path (Join-Path $PSScriptRoot $dir) $DllName
        if (Test-Path $dllPath) {
            return (Join-Path $PSScriptRoot $dir)
        }
    }

    return $null
}

function Test-ObsInstalled {
    # Check common OBS install locations
    $obsLocations = @(
        "${env:ProgramFiles}\obs-studio\bin\64bit\obs64.exe",
        "${env:ProgramFiles(x86)}\obs-studio\bin\64bit\obs64.exe",
        "C:\Program Files\obs-studio\bin\64bit\obs64.exe"
    )

    foreach ($loc in $obsLocations) {
        if (Test-Path $loc) {
            return $loc
        }
    }

    return $null
}

# --- Uninstall ---
if ($Uninstall) {
    Write-Header
    $removed = $false
    if (Test-Path $PluginDir) {
        Remove-Item -Recurse -Force $PluginDir
        Write-Host "[OK] Plugin removed from: $PluginDir" -ForegroundColor Green
        $removed = $true
    }
    if (Test-Path $LegacyPluginDir) {
        Remove-Item -Recurse -Force $LegacyPluginDir
        Write-Host "[OK] Removed legacy install from: $LegacyPluginDir" -ForegroundColor Green
        $removed = $true
    }
    if (-not $removed) {
        Write-Host "[INFO] Plugin not installed." -ForegroundColor Yellow
    }
    exit 0
}

# --- Install ---
Write-Header

# Step 0: Check for legacy install and clean up
if (Test-Path $LegacyPluginDir) {
    Write-Host "[WARN] Found plugin in WRONG location (AppData):" -ForegroundColor Yellow
    Write-Host "       $LegacyPluginDir" -ForegroundColor Yellow
    Write-Host "       OBS does NOT load plugins from AppData!" -ForegroundColor Yellow
    Write-Host "       Removing legacy install..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $LegacyPluginDir
    Write-Host "[OK] Legacy install removed." -ForegroundColor Green
    Write-Host ""
}

# Step 1: Check OBS is installed
$obsExe = Test-ObsInstalled
if ($obsExe) {
    Write-Host "[OK] OBS Studio found: $obsExe" -ForegroundColor Green
} else {
    Write-Host "[WARN] OBS Studio not found in standard locations." -ForegroundColor Yellow
    Write-Host "       Make sure OBS Studio (64-bit) is installed." -ForegroundColor Yellow
}

# Step 2: Find the plugin DLL
if ($BuildDir -eq "") {
    $BuildDir = Find-BuildDir
}

if (-not $BuildDir -or -not (Test-Path (Join-Path $BuildDir $DllName))) {
    Write-Host "[ERROR] Plugin DLL not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "  Expected: <build-dir>\$DllName" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Searched in:" -ForegroundColor Yellow
    Write-Host "    - build_x64\Release" -ForegroundColor Yellow
    Write-Host "    - build_x64\RelWithDebInfo" -ForegroundColor Yellow
    Write-Host "    - build_x64\Debug" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Build the plugin first:" -ForegroundColor Cyan
    Write-Host "    cmake --preset windows-x64" -ForegroundColor White
    Write-Host "    cmake --build --preset windows-x64" -ForegroundColor White
    Write-Host ""
    Write-Host "  Or specify the build directory:" -ForegroundColor Cyan
    Write-Host "    .\install.ps1 -BuildDir 'C:\path\to\build'" -ForegroundColor White
    exit 1
}

$DllPath = Join-Path $BuildDir $DllName
Write-Host "[OK] Plugin DLL found: $DllPath" -ForegroundColor Green

# Step 3: Find locale data
$DataDir = Join-Path $PSScriptRoot "data"
if (-not (Test-Path (Join-Path $DataDir "locale\en-US.ini"))) {
    Write-Host "[ERROR] Locale data not found at: $DataDir\locale\en-US.ini" -ForegroundColor Red
    Write-Host "        Make sure you run this from the project root." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Locale data found: $DataDir" -ForegroundColor Green

# Step 4: Create plugin directory structure in ProgramData
# NOTE: C:\ProgramData\obs-studio\plugins\ is where OBS loads third-party plugins from.
# This directory may not exist yet if no plugins have been installed before.
$binDir = "$PluginDir\bin\64bit"
$dataDestDir = "$PluginDir\data"

if (-not (Test-Path "$ObsProgramData\plugins")) {
    Write-Host "[INFO] Creating ProgramData plugins directory (normal for first plugin install)" -ForegroundColor Yellow
}

try {
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
    New-Item -ItemType Directory -Force -Path $dataDestDir | Out-Null
} catch {
    Write-Host "[ERROR] Cannot create directory: $PluginDir" -ForegroundColor Red
    Write-Host "        You may need to run PowerShell as Administrator." -ForegroundColor Red
    Write-Host ""
    Write-Host "  Right-click PowerShell -> 'Run as administrator', then retry." -ForegroundColor Cyan
    exit 1
}
Write-Host "[OK] Plugin directory created: $PluginDir" -ForegroundColor Green

# Step 5: Check if OBS is running (DLL would be locked)
$obsProcess = Get-Process -Name "obs64" -ErrorAction SilentlyContinue
if ($obsProcess) {
    Write-Host "[WARN] OBS Studio is currently running!" -ForegroundColor Yellow
    Write-Host "       The plugin DLL may be locked. Please close OBS first." -ForegroundColor Yellow
    $response = Read-Host "       Close OBS now and continue? (y/n)"
    if ($response -eq 'y') {
        Stop-Process -Name "obs64" -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        Write-Host "[OK] OBS Studio closed." -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Cannot install while OBS is running. Please close OBS and retry." -ForegroundColor Red
        exit 1
    }
}

# Step 6: Copy files
try {
    Copy-Item $DllPath "$binDir\" -Force
    Write-Host "[OK] Copied: $DllName -> $binDir\" -ForegroundColor Green
} catch [System.IO.IOException] {
    Write-Host "[ERROR] Cannot copy DLL - file is locked." -ForegroundColor Red
    Write-Host "        Close OBS Studio completely and try again." -ForegroundColor Red
    exit 1
}

Copy-Item -Recurse -Force "$DataDir\*" "$dataDestDir\"
Write-Host "[OK] Copied: data\ -> $dataDestDir\" -ForegroundColor Green

# Step 7: Deploy MW OBS KIT template (scene collection + profile)
$ObsAppData = "$env:APPDATA\obs-studio"
$TemplateDir = Join-Path $PSScriptRoot "[MW] OBS KIT"
$SceneCollectionFile = Join-Path $TemplateDir "Minewache.json"

if (Test-Path $SceneCollectionFile) {
    $scenesDir = "$ObsAppData\basic\scenes"
    $profileDir = "$ObsAppData\basic\profiles\Minewache"
    $targetScene = "$scenesDir\Minewache.json"

    if (Test-Path $targetScene) {
        Write-Host "[INFO] Minewache scene collection already exists, skipping template deploy" -ForegroundColor Yellow
    } else {
        # Create directories if needed
        New-Item -ItemType Directory -Force -Path $scenesDir | Out-Null
        New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

        # Copy scene collection
        Copy-Item $SceneCollectionFile $targetScene -Force
        Write-Host "[OK] Deployed scene collection: Minewache.json -> $scenesDir\" -ForegroundColor Green

        # Copy profile
        $profileSrc = Join-Path $TemplateDir "Minewache"
        if (Test-Path $profileSrc) {
            Copy-Item -Recurse -Force "$profileSrc\*" "$profileDir\"
            Write-Host "[OK] Deployed profile: Minewache -> $profileDir\" -ForegroundColor Green
        }
    }
} else {
    Write-Host "[INFO] MW OBS KIT template not found, skipping template deploy" -ForegroundColor Yellow
}

# Step 8: Verify installation
Write-Host ""
Write-Host "--- Verification ---" -ForegroundColor Cyan

$checks = @(
    @{ Path = "$binDir\$DllName"; Desc = "Plugin DLL" },
    @{ Path = "$dataDestDir\locale\en-US.ini"; Desc = "Locale file (en-US)" }
)

$allOk = $true
foreach ($check in $checks) {
    if (Test-Path $check.Path) {
        $size = (Get-Item $check.Path).Length
        Write-Host "  [OK] $($check.Desc): $($check.Path) ($size bytes)" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $($check.Desc): $($check.Path)" -ForegroundColor Red
        $allOk = $false
    }
}

Write-Host ""
if ($allOk) {
    Write-Host "Installation successful!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Installed to: $PluginDir" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Restart OBS Studio (close completely and reopen)" -ForegroundColor White
    Write-Host "  2. Scene Collection -> 'Minewache'" -ForegroundColor White
    Write-Host "  3. Profile -> 'Minewache'" -ForegroundColor White
    Write-Host "  4. LTC Timecode is pre-configured on Track 3" -ForegroundColor White
    Write-Host ""
    Write-Host "  (If no template was deployed, manually add: Sources -> + -> 'LTC Timecode Generator')" -ForegroundColor Gray
} else {
    Write-Host "Installation completed with errors. Check the messages above." -ForegroundColor Red
    exit 1
}
