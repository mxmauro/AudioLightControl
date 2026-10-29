#Requires -Version 7.0
param (
    [Parameter(Mandatory = $false)]
    [switch]$Release = $false,

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

Invoke-BuildCommand -Command 'go' -CommandArguments @('clean', '-cache') -ErrorMessage 'Unable to clean GO build cache'

$wailsArguments = @('tool', 'wails', 'build', '-nosyncgomod', '-nocolour', '-webview2', 'embed')
if (!$Release) {
    $wailsArguments += @('-debug', '-tags', 'debug')
}

Invoke-BuildCommand -Command 'go' -CommandArguments $wailsArguments -ErrorMessage 'Code generation failed'
