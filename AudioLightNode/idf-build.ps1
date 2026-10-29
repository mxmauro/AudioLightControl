#Requires -Version 5.1
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('rebuild', 'build', 'flash', 'monitor')]
    [string]$Action,

    [switch]$Release,

    [string]$Port,

    [switch]$Erase,

    [switch]$NoMonitor,

    [string]$Target = 'c3'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Show-Usage {
    Write-Host 'Usage: .\idf-build.ps1 <rebuild|build> [-Release] [-Target esp32|c3|esp32c3|s3|esp32s3|...]'
    Write-Host '       .\idf-build.ps1 flash [-Port COM#] [-Erase] [-NoMonitor] [-Release]'
    Write-Host '       .\idf-build.ps1 monitor [-Port COM#] [-Release]'
}

function Get-EspressifSerialPort {
    $devices = Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPClass = 'Ports'"

    foreach ($device in $devices) {
        if ($device.PNPDeviceID -notmatch '^USB\\VID_303A&' -or $device.Name -notmatch '\((COM\d+)\)') {
            continue
        }

        return $Matches[1]
    }

    return $null
}

if ($Action -in @('flash', 'monitor') -and [string]::IsNullOrWhiteSpace($Port)) {
    $Port = Get-EspressifSerialPort
    if ([string]::IsNullOrWhiteSpace($Port)) {
        Write-Error 'No Espressif USB serial device (VID_303A) with a COM port was found. Connect a device or specify -Port COM#.'
        exit 1
    }

    Write-Host "Using detected Espressif serial port $Port."
}
if ($Erase -and $Action -ne 'flash') {
    Write-Error '-Erase is only valid for flash.'
    Show-Usage
    exit 1
}
if ($NoMonitor -and $Action -ne 'flash') {
    Write-Error '-NoMonitor is only valid for flash.'
    Show-Usage
    exit 1
}
if ($Action -ne 'rebuild' -and $PSBoundParameters.ContainsKey('Target')) {
    Write-Error '-Target is only valid for rebuild.'
    Show-Usage
    exit 1
}
if ([string]::IsNullOrWhiteSpace($Target)) {
    Write-Error '-Target cannot be empty.'
    exit 1
}
$target = $Target.Trim().ToLowerInvariant()
if (-not $target.StartsWith('esp32')) {
    $target = "esp32$target"
}
if ([string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
    Write-Error 'IDF_PATH is not set. Run this from an ESP-IDF environment.'
    exit 1
}

$preset = if ($Release) { 'Release' } else { 'Debug' }
$buildProfile = if ($Release) { 'release' } else { 'debug' }
$buildDir = Join-Path $PSScriptRoot (Join-Path 'build' $buildProfile)
$sdkconfig = Join-Path $buildDir 'sdkconfig'
$idfPy = Join-Path $env:IDF_PATH 'tools\idf.py'

if ($action -eq 'build' -and (Test-Path -LiteralPath $sdkconfig)) {
    $configuredTarget = Select-String -LiteralPath $sdkconfig -Pattern '^CONFIG_IDF_TARGET="([^"]+)"$' | Select-Object -First 1

    if ($configuredTarget) {
        $target = $configuredTarget.Matches[0].Groups[1].Value
    }
}

if (-not (Test-Path -LiteralPath $idfPy)) {
    Write-Error "Unable to find idf.py at $idfPy."
    exit 1
}

Push-Location -LiteralPath $PSScriptRoot
try {
    switch ($action) {
        'rebuild' {
            if (Test-Path -LiteralPath $buildDir) {
                Remove-Item -LiteralPath $buildDir -Recurse -Force
            }
            & python $idfPy --preset $preset -D "IDF_TARGET=$target" set-target $target update-dependencies build
        }

        'build' {
            & python $idfPy --preset $preset -D "IDF_TARGET=$target" build
        }

        'flash' {
            $flashCommands = @()
            if ($Erase) {
                $flashCommands += 'erase-flash'
            }
            $flashCommands += 'flash'
            if (-not $NoMonitor) {
                $flashCommands += 'monitor'
            }

            & python $idfPy --preset $preset -p $Port @flashCommands
        }

        'monitor' {
            & python $idfPy --preset $preset -p $Port monitor
        }
    }
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
