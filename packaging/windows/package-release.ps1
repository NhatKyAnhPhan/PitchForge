param(
  [string]$EngineDll,
  [string]$OutputDirectory,
  [string]$FlutterPath
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$appDirectory = Join-Path $repositoryRoot 'app'
$releaseDirectory = Join-Path $appDirectory 'build\windows\x64\runner\Release'
$packageName = 'auralis_studio-windows-x64'

if ([string]::IsNullOrWhiteSpace($FlutterPath)) {
  $flutterCommand = Get-Command flutter -ErrorAction SilentlyContinue
  if ($flutterCommand) {
    $FlutterPath = $flutterCommand.Source
  } else {
    $flutterCandidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:FLUTTER_ROOT)) {
      $flutterCandidates += Join-Path $env:FLUTTER_ROOT 'bin\flutter.bat'
    }
    $flutterCandidates += 'C:\flutter\bin\flutter.bat'
    $FlutterPath = $flutterCandidates |
      Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
      Select-Object -First 1
  }
}
if ([string]::IsNullOrWhiteSpace($FlutterPath) -or
    -not (Test-Path -LiteralPath $FlutterPath -PathType Leaf)) {
  throw 'Flutter was not found. Add it to PATH or pass -FlutterPath <path-to-flutter.bat>.'
}
$FlutterPath = (Resolve-Path -LiteralPath $FlutterPath).Path

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $appDirectory 'build\windows\packages'
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
  $OutputDirectory = Join-Path $repositoryRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

$engineDllPath = $null
if (-not [string]::IsNullOrWhiteSpace($EngineDll)) {
  $engineDllInput = if ([IO.Path]::IsPathRooted($EngineDll)) {
    $EngineDll
  } else {
    Join-Path $repositoryRoot $EngineDll
  }
  $engineDllPath = (Resolve-Path -LiteralPath $engineDllInput).Path
  if ([IO.Path]::GetFileName($engineDllPath) -ine 'auralis_audio_engine.dll') {
    throw 'EngineDll must point to a file named auralis_audio_engine.dll.'
  }
}

$hadPreviousEngineDll = Test-Path Env:AURALIS_AUDIO_ENGINE_DLL
$previousEngineDll = $env:AURALIS_AUDIO_ENGINE_DLL
try {
  # Start from a clean Flutter/CMake build so cached engine paths or stale
  # staged DLLs can never leak into the release archive.
  Push-Location $appDirectory
  try {
    & $FlutterPath clean
    if ($LASTEXITCODE -ne 0) {
      throw "flutter clean failed with exit code $LASTEXITCODE."
    }

    if ($engineDllPath) {
      $env:AURALIS_AUDIO_ENGINE_DLL = $engineDllPath
    } else {
      Remove-Item Env:AURALIS_AUDIO_ENGINE_DLL -ErrorAction SilentlyContinue
    }

    & $FlutterPath build windows --release
    if ($LASTEXITCODE -ne 0) {
      throw "flutter build windows failed with exit code $LASTEXITCODE."
    }
  } finally {
    Pop-Location
  }
} finally {
  if ($hadPreviousEngineDll) {
    $env:AURALIS_AUDIO_ENGINE_DLL = $previousEngineDll
  } else {
    Remove-Item Env:AURALIS_AUDIO_ENGINE_DLL -ErrorAction SilentlyContinue
  }
}

if (-not (Test-Path -LiteralPath $releaseDirectory -PathType Container)) {
  throw "Flutter release output was not found: $releaseDirectory"
}

$requiredRuntimeFiles = @(
  'auralis_studio.exe',
  'flutter_windows.dll',
  'data\icudtl.dat',
  'data\flutter_assets'
)
foreach ($relativePath in $requiredRuntimeFiles) {
  $runtimePath = Join-Path $releaseDirectory $relativePath
  if (-not (Test-Path -LiteralPath $runtimePath)) {
    throw "Required Flutter runtime file is missing: $runtimePath"
  }
}

$bundledEngineDll = Join-Path $releaseDirectory 'auralis_audio_engine.dll'
if ($engineDllPath) {
  if (-not (Test-Path -LiteralPath $bundledEngineDll -PathType Leaf)) {
    throw "The engine DLL was not staged beside the executable: $bundledEngineDll"
  }
} else {
  Write-Warning 'No engine DLL was supplied; this archive is the Flutter shell only and has no audio backend.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archivePath = Join-Path $OutputDirectory "$packageName.zip"
Compress-Archive -Path (Join-Path $releaseDirectory '*') `
  -DestinationPath $archivePath -CompressionLevel Optimal -Force

Write-Output "Created Windows release archive: $archivePath"
