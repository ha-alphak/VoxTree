[CmdletBinding()]
param()

$repositoryRoot = git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
    throw 'This command must be run inside the VoxTree Git repository.'
}

Push-Location -LiteralPath $repositoryRoot
try {
    foreach ($hook in @('.githooks/pre-commit', '.githooks/pre-push')) {
        if (-not (Test-Path -LiteralPath $hook -PathType Leaf)) {
            throw "Required hook is missing: $hook"
        }
    }

    git config --local core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not configure the repository hook path.'
    }

    Write-Host 'Git sensitive-content guards are enabled for this clone.'
}
finally {
    Pop-Location
}
