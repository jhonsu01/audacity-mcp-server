# Builds the native engine (aumcp-engine.exe, used by the MCP server) and the Audacity extension
# library (audacity_mcp_native.dll) with MSVC. Static CRT: no Visual C++ runtime needed.
# Usage: powershell -ExecutionPolicy Bypass -File native\build.ps1
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Error 'vswhere.exe not found: install Visual Studio with the C++ workload'; exit 1 }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Write-Error 'No Visual Studio with C++ tools found'; exit 1 }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$common = '/nologo /O2 /EHsc /std:c++17 /MT /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS'
$srcs = 'src\audio.cpp src\codecs.cpp src\dsp.cpp'
$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$vcvars" >nul || exit /b 1
cd /d "$root"
mkdir "$out\obj-exe" 2>nul
mkdir "$out\obj-dll" 2>nul
cl $common src\engine_main.cpp $srcs /Fo"$out\obj-exe\\" /Fe"$out\aumcp-engine.exe" /link /SUBSYSTEM:CONSOLE || exit /b 1
cl $common /LD src\extension.cpp $srcs /Fo"$out\obj-dll\\" /Fe"$out\audacity_mcp_native.dll" || exit /b 1
"@ | Set-Content -Encoding ASCII $bat
cmd /c "`"$bat`""
if ($LASTEXITCODE -ne 0) { Write-Error "Native build failed ($LASTEXITCODE)"; exit 1 }

# Ship the DLL inside the Audacity extension bundle
$plat = Join-Path (Split-Path $root -Parent) 'extension\platform\windows\x86_64'
New-Item -ItemType Directory -Force $plat | Out-Null
Copy-Item (Join-Path $out 'audacity_mcp_native.dll') $plat -Force
Write-Host "Built: $out\aumcp-engine.exe and $out\audacity_mcp_native.dll"
