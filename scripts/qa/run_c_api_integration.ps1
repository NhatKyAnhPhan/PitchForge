param(
    [string]$Library,

    [switch]$HeaderOnly,
    [string]$Header,

    [switch]$Audio,

    [ValidateRange(0, 8)]
    [int]$ParameterEffect = 1,

    [ValidateRange(0, 65535)]
    [int]$ParameterId = 1,

    [double]$SampleRate = 48000,
    [ValidateRange(1, 2147483647)]
    [int]$BufferSize = 256,
    [ValidateRange(1, 32)]
    [int]$InputChannels = 1,
    [ValidateRange(1, 32)]
    [int]$OutputChannels = 2,
    [Nullable[int]]$InputDevice,
    [Nullable[int]]$OutputDevice,
    [string]$JsonReport
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pythonCommand = Get-Command python -ErrorAction Stop
$runner = Join-Path $repoRoot 'tests\integration\c_api_integration.py'

if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "Integration runner not found: $runner"
}
if ($SampleRate -le 0) {
    throw 'SampleRate must be positive.'
}

$testArgs = @()
if ($HeaderOnly) {
    if ([string]::IsNullOrWhiteSpace($Header)) {
        throw '-HeaderOnly requires -Header.'
    }
    if (-not [string]::IsNullOrWhiteSpace($Library)) {
        throw '-Library cannot be used with -HeaderOnly.'
    }
    $headerInput = if ([IO.Path]::IsPathRooted($Header)) {
        $Header
    } else {
        Join-Path $repoRoot $Header
    }
    $headerPath = (Resolve-Path $headerInput -ErrorAction Stop).Path
    $testArgs += @('--header-only', '--header', $headerPath)
} else {
    if ([string]::IsNullOrWhiteSpace($Library)) {
        throw '-Library is required unless -HeaderOnly is selected.'
    }
    $libraryInput = if ([IO.Path]::IsPathRooted($Library)) {
        $Library
    } else {
        Join-Path $repoRoot $Library
    }
    $libraryPath = (Resolve-Path $libraryInput -ErrorAction Stop).Path
    $testArgs += @('--library', $libraryPath)
    if (-not [string]::IsNullOrWhiteSpace($Header)) {
        $headerInput = if ([IO.Path]::IsPathRooted($Header)) {
            $Header
        } else {
            Join-Path $repoRoot $Header
        }
        $headerPath = (Resolve-Path $headerInput -ErrorAction Stop).Path
        $testArgs += @('--header', $headerPath)
    }
}
if ($Audio) {
    $testArgs += @(
        '--audio',
        '--parameter-effect', "$ParameterEffect",
        '--parameter-id', "$ParameterId",
        '--sample-rate', $SampleRate.ToString(
            [Globalization.CultureInfo]::InvariantCulture
        ),
        '--buffer-size', "$BufferSize",
        '--input-channels', "$InputChannels",
        '--output-channels', "$OutputChannels"
    )
    if ($InputDevice.HasValue) {
        $testArgs += @('--input-device', "$($InputDevice.Value)")
    }
    if ($OutputDevice.HasValue) {
        $testArgs += @('--output-device', "$($OutputDevice.Value)")
    }
}
if ($JsonReport) {
    $testArgs += @('--json-report', $JsonReport)
}

Push-Location $repoRoot
try {
    & $pythonCommand.Source $runner @testArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
