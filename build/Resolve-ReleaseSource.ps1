[CmdletBinding()]
param([Parameter(Mandatory)][string]$RunId, [Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleasePolicy.psm1') -Force
$Tag = Get-DmfReleaseTag $Tag
if ($RunId -notmatch '\A[1-9][0-9]*\z') { throw 'Source run ID must be numeric.' }
$json = gh api "repos/$env:GITHUB_REPOSITORY/actions/runs/$RunId"
if ($LASTEXITCODE) { throw 'Could not read the source workflow run.' }
$run = $json | ConvertFrom-Json
# Fetch only the existing remote tag; do not create or move a tag.
git fetch --no-tags origin "refs/tags/$Tag"
if ($LASTEXITCODE) { throw 'Release tag must already exist on origin.' }
$tagCommit = git rev-parse --verify 'FETCH_HEAD^{commit}'
if ($LASTEXITCODE) { throw 'Could not resolve the release tag commit.' }
$automatic = $env:GITHUB_EVENT_NAME -ceq 'push' -and $env:GITHUB_REF_TYPE -ceq 'tag'
Assert-DmfReleaseSource -Run $run -RunId $RunId -Repository $env:GITHUB_REPOSITORY -Tag $Tag -TagCommit $tagCommit `
    -Automatic:$automatic -CurrentRunId $env:GITHUB_RUN_ID -CurrentTag $env:GITHUB_REF_NAME
"head_sha=$tagCommit" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Append -Encoding utf8
"release_tag=$Tag" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Append -Encoding utf8
