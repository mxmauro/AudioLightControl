#Requires -Version 7.0
param (
    [Parameter(Mandatory = $false)]
    [switch]$Deps = $false
);

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-BuildCommand {
    param(
        [Parameter(Mandatory)]
        [string]$ErrorMessage,

        [Parameter(Mandatory)]
        [string]$Command,

        [string[]]$CommandArguments = @()
    )

    Write-Host ($Command + ' ' + ($CommandArguments -join ' '))
    & $Command @CommandArguments

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Error: $ErrorMessage"
        exit $LASTEXITCODE
    }
}

if ($Deps) {
    Invoke-BuildCommand -Command 'go' -CommandArguments @('mod', 'vendor') -ErrorMessage 'Dependency download failed'
}

$env:WAILS_EXP_USE_WAKE = '1'
$wailsArguments = @('run', '-mod=mod', 'github.com/wailsapp/wails/v3/cmd/wails3', 'build', '-targets', 'windows/amd64')
Invoke-BuildCommand -Command 'go' -CommandArguments $wailsArguments -ErrorMessage 'Windows build failed'

# After removing vendor/, the equivalent commands are:
# go tool wails3 build -targets windows/amd64
# go run github.com/wailsapp/wails/v3/cmd/wails3 build -targets windows/amd64
