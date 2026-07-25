[CmdletBinding()]
param(
    [ValidateSet(
        'windows-msvc-debug',
        'windows-msvc-release',
        'windows-msvc-livekit-quality-gate',
        'windows-msvc-client'
    )]
    [string] $Preset = 'windows-msvc-debug'
)

$ErrorActionPreference = 'Stop'

function Repair-ProcessPath {
    $pathVariables = @(
        [Environment]::GetEnvironmentVariables().GetEnumerator() |
            Where-Object { $_.Key -ieq 'Path' }
    )

    if ($pathVariables.Count -le 1) {
        return
    }

    $canonicalPath = $env:Path
    [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
    [Environment]::SetEnvironmentVariable('Path', $canonicalPath, 'Process')
}

function Find-CMake {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        return $cmakeCommand.Source
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsWhere)) {
        throw 'CMake was not found in PATH and vswhere.exe is unavailable.'
    }

    $installationPath = & $vsWhere `
        -latest `
        -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'A Visual Studio installation with the C++ toolchain was not found.'
    }

    $bundledCMake = Join-Path `
        $installationPath `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

    if (-not (Test-Path -LiteralPath $bundledCMake)) {
        throw "Visual Studio CMake was not found at '$bundledCMake'."
    }

    return $bundledCMake
}

Repair-ProcessPath
$cmake = Find-CMake
Write-Host "Using CMake: $cmake"
& $cmake --workflow --preset $Preset

if ($LASTEXITCODE -ne 0) {
    throw "Build workflow '$Preset' failed with exit code $LASTEXITCODE."
}
