Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Show-Usage {
    Write-Host 'Usage: .\idf-build.ps1 <rebuild|build> [--debug] [--target c3|s3|...]'
    Write-Host '       .\idf-build.ps1 flash --port COM# [--erase] [--debug]'
    Write-Host '       .\idf-build.ps1 monitor --port COM#'
}

if ($args.Count -lt 1 -or $args.Count -gt 64) {
    Show-Usage
    exit 1
}

$action = $args[0].ToLowerInvariant()
$isDebug = $false
$eraseFlash = $false
$serialPort = $null
$target = 'c3'

if ($action -notin @('rebuild', 'build', 'flash', 'monitor')) {
    Write-Error "Unknown action: $action"
    Show-Usage
    exit 1
}

$index = 1
while ($index -lt $args.Count) {
    switch ($args[$index]) {
        '--debug' {
            if ($action -notin @('rebuild', 'build', 'flash')) {
                Write-Error "--debug is only valid for rebuild, build and flash"
                Show-Usage
                exit 1
            }

            $isDebug = $true
            $index++
        }

        '--port' {
            if ($action -notin @('flash', 'monitor')) {
                Write-Error "--port is only valid for flash and monitor"
                Show-Usage
                exit 1
            }

            if ($index + 1 -ge $args.Count) {
                Write-Error 'Missing value for --port'
                Show-Usage
                exit 1
            }

            $serialPort = $args[$index + 1]
            $index += 2
        }

        '--erase' {
            if ($action -ne 'flash') {
                Write-Error "--erase is only valid for flash"
                Show-Usage
                exit 1
            }

            $eraseFlash = $true
            $index++
        }

        '--target' {
            if ($action -ne 'rebuild') {
                Write-Error "--target is only valid for rebuild"
                Show-Usage
                exit 1
            }

            if ($index + 1 -ge $args.Count) {
                Write-Error 'Missing value for --target'
                Show-Usage
                exit 1
            }

            $target = $args[$index + 1].ToLowerInvariant()
            $index += 2
        }

        default {
            Write-Error "Unknown option: $($args[$index])"
            Show-Usage
            exit 1
        }
    }
}

if ($action -in @('flash', 'monitor')) {
    if ([string]::IsNullOrWhiteSpace($serialPort)) {
        Write-Error "--port is required for action '$action'"
        Show-Usage
        exit 1
    }
}

if ([string]::IsNullOrWhiteSpace($target)) {
    Write-Error '--target cannot be empty'
    Show-Usage
    exit 1
}

$buildType = if ($isDebug) { 'debug' } else { 'release' }
$buildRoot = Join-Path $PSScriptRoot 'build'
$buildDir = Join-Path $buildRoot $buildType
$sdkConfigDefaults = "sdkconfig.defaults;sdkconfig.$buildType"
$chipTarget = "esp32$target"

$idfArgs = @('-B', $buildDir)

switch ($action) {
    'rebuild' {
        if (Test-Path -LiteralPath $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }

        $idfArgs += @(
            "-DSDKCONFIG_DEFAULTS=$sdkConfigDefaults",
            'set-target', $chipTarget,
            'update-dependencies',
            'build'
        )
    }

    'build' {
        $idfArgs += @(
            "-DSDKCONFIG_DEFAULTS=$sdkConfigDefaults",
            'build'
        )
    }

    'flash' {
        $idfArgs += @(
            '-p', $serialPort
        )

        if ($eraseFlash) {
            $idfArgs += @(
                'erase-flash'
            )
        }

        $idfArgs += @(
            'flash',
            'monitor'
        )
    }

    'monitor' {
        $idfArgs += @(
            '-p', $serialPort,
            'monitor'
        )
    }
}

Push-Location -LiteralPath $PSScriptRoot
try {
    & idf.py @idfArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
