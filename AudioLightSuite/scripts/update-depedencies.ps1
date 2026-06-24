<#
Run from a CMD window at the repository root:
  powershell -ExecutionPolicy Bypass -File update-depedencies.ps1

To also restore packages after updating the files:
  powershell -ExecutionPolicy Bypass -File update-depedencies.ps1 -Install
#>

param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$Branch = "master",
    [switch]$Install
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-JsonFile {
    param([string]$Path)

    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$InputObject,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $json = $InputObject | ConvertTo-Json -Depth 20
    Set-Content -LiteralPath $Path -Value $json
}

function Invoke-GitHubJson {
    param([string]$Uri)

    $headers = @{
        "Accept" = "application/vnd.github+json"
        "User-Agent" = "AudioLightSuite-dependency-updater"
    }

    return Invoke-RestMethod -Uri $Uri -Headers $headers
}

function Get-GitHubRepositoryInfo {
    param([string]$RepositoryUrl)

    if ($RepositoryUrl -notmatch "^https://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$") {
        throw "Unsupported registry repository URL: $RepositoryUrl"
    }

    return @{
        Owner = $matches[1]
        Name = $matches[2]
    }
}

function Get-GitHubBranchState {
    param(
        [string]$Owner,
        [string]$Repository,
        [string]$Branch
    )

    try {
        $commit = Invoke-GitHubJson -Uri "https://api.github.com/repos/$Owner/$Repository/commits/$Branch"
        return @{
            Branch = $Branch
            Sha = $commit.sha
        }
    }
    catch {
        $repoInfo = Invoke-GitHubJson -Uri "https://api.github.com/repos/$Owner/$Repository"
        if ($repoInfo.default_branch -eq $Branch) {
            throw
        }

        Write-Warning "Branch '$Branch' was not found in $Owner/$Repository. Falling back to default branch '$($repoInfo.default_branch)'."
        $commit = Invoke-GitHubJson -Uri "https://api.github.com/repos/$Owner/$Repository/commits/$($repoInfo.default_branch)"
        return @{
            Branch = $repoInfo.default_branch
            Sha = $commit.sha
        }
    }
}

function Get-RegistryBaselineData {
    param(
        [string]$Owner,
        [string]$Repository,
        [string]$Branch
    )

    $uri = "https://raw.githubusercontent.com/$Owner/$Repository/$Branch/versions/baseline.json"
    return Invoke-RestMethod -Uri $uri
}

function Get-PackageBaselineEntry {
    param(
        [Parameter(Mandatory = $true)]$BaselineData,
        [Parameter(Mandatory = $true)][string]$PackageName
    )

    $defaultBaseline = $BaselineData.default
    if ($null -eq $defaultBaseline) {
        return $null
    }

    $property = $defaultBaseline.PSObject.Properties[$PackageName]
    if ($null -eq $property) {
        return $null
    }

    return $property.Value
}

function Format-VcpkgVersion {
    param($BaselineEntry)

    $version = $BaselineEntry.baseline
    $portVersionProperty = $BaselineEntry.PSObject.Properties["port-version"]
    if ($null -ne $portVersionProperty -and [int]$portVersionProperty.Value -gt 0) {
        return "$version#$($portVersionProperty.Value)"
    }

    return $version
}

function Get-DependencyName {
    param($Dependency)

    if ($Dependency -is [string]) {
        return $Dependency
    }

    return $Dependency.name
}

function Set-DependencyVersion {
    param(
        [Parameter(Mandatory = $true)]$Dependency,
        [Parameter(Mandatory = $true)][string]$Version
    )

    if ($Dependency -is [string]) {
        return [pscustomobject]@{
            name = $Dependency
            "version>=" = $Version
        }
    }

    if ($Dependency.PSObject.Properties["version>="]) {
        $Dependency."version>=" = $Version
    }
    else {
        Add-Member -InputObject $Dependency -NotePropertyName "version>=" -NotePropertyValue $Version
    }

    return $Dependency
}

function Find-RegistryForPackage {
    param(
        [Parameter(Mandatory = $true)]$Configuration,
        [Parameter(Mandatory = $true)][string]$PackageName
    )

    foreach ($registry in @($Configuration.registries)) {
        foreach ($registryPackage in @($registry.packages)) {
            if ($registryPackage -eq $PackageName) {
                return $registry
            }
        }
    }

    return $null
}

$manifestPath = Join-Path $ProjectRoot "vcpkg.json"
$configurationPath = Join-Path $ProjectRoot "vcpkg-configuration.json"

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifest file not found: $manifestPath"
}

if (-not (Test-Path -LiteralPath $configurationPath)) {
    throw "Configuration file not found: $configurationPath"
}

$manifest = Read-JsonFile -Path $manifestPath
$configuration = Read-JsonFile -Path $configurationPath
$registryState = @{}

for ($index = 0; $index -lt @($configuration.registries).Count; $index++) {
    $registry = $configuration.registries[$index]
    if ($registry.kind -ne "git") {
        continue
    }

    $repositoryInfo = Get-GitHubRepositoryInfo -RepositoryUrl $registry.repository
    $branchState = Get-GitHubBranchState -Owner $repositoryInfo.Owner -Repository $repositoryInfo.Name -Branch $Branch
    $baselineData = Get-RegistryBaselineData -Owner $repositoryInfo.Owner -Repository $repositoryInfo.Name -Branch $branchState.Branch

    $registry.baseline = $branchState.Sha
    $registryState[$index] = @{
        BaselineData = $baselineData
        Repository = "$($repositoryInfo.Owner)/$($repositoryInfo.Name)"
        Branch = $branchState.Branch
        HeadSha = $branchState.Sha
    }
}

for ($index = 0; $index -lt @($manifest.dependencies).Count; $index++) {
    $dependency = $manifest.dependencies[$index]
    $packageName = Get-DependencyName -Dependency $dependency
    $registry = Find-RegistryForPackage -Configuration $configuration -PackageName $packageName

    if ($null -eq $registry) {
        Write-Warning "Skipping '$packageName' because it is not mapped to a custom git registry in vcpkg-configuration.json."
        continue
    }

    $registryIndex = [array]::IndexOf(@($configuration.registries), $registry)
    $state = $registryState[$registryIndex]
    if ($null -eq $state) {
        Write-Warning "Skipping '$packageName' because its registry state could not be resolved."
        continue
    }

    $baselineEntry = Get-PackageBaselineEntry -BaselineData $state.BaselineData -PackageName $packageName
    if ($null -eq $baselineEntry) {
        Write-Warning "Skipping '$packageName' because it was not found in $($state.Repository) baseline on branch '$($state.Branch)'."
        continue
    }

    $latestVersion = Format-VcpkgVersion -BaselineEntry $baselineEntry
    $manifest.dependencies[$index] = Set-DependencyVersion -Dependency $dependency -Version $latestVersion
    Write-Host "Updated $packageName to version>=$latestVersion"
}

Write-JsonFile -InputObject $manifest -Path $manifestPath
Write-JsonFile -InputObject $configuration -Path $configurationPath

foreach ($state in $registryState.Values) {
    Write-Host "Updated registry baseline for $($state.Repository) to $($state.HeadSha)"
}

if ($Install) {
    Push-Location $ProjectRoot
    try {
        & vcpkg install
    }
    finally {
        Pop-Location
    }
}
