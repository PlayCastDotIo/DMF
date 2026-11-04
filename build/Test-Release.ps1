$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleasePolicy.psm1') -Force
$script:checks = 0
function Assert([bool]$Condition, [string]$Label) {
    if (!$Condition) { throw "Failed: $Label" }
    ++$script:checks
}
function Reject([scriptblock]$Action, [string]$Label) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert $rejected $Label
}
foreach ($tag in @('v1.1.159','v1.1.159-playcast.1','v1.1.159-playcast.123')) {
    Assert ((Get-DmfReleaseTag $tag) -ceq $tag) "Exact tag retained: $tag"
}
foreach ($tag in @('1.1.159','v01.1.159','v1.1.159-playcast.0','v1.1.159-playcast.01','v1.1.159-beta.1','v1.1.159-playcast.1+other',"v1.1.159`n",'v1.1.159; command')) {
    Reject { Get-DmfReleaseTag $tag } "Invalid tag rejected: $tag"
}
function New-Run {
    [pscustomobject]@{ id=42; path='.github/workflows/build-artifacts.yml@main'; event='push'; head_sha=('a'*40)
        status='completed'; conclusion='success'; repository=@{full_name='PlayCastDotIo/DMF'}; head_repository=@{full_name='PlayCastDotIo/DMF'} }
}
$source = @{ RunId='42'; Repository='PlayCastDotIo/DMF'; Tag='v1.1.159-playcast.1'; TagCommit=('a'*40) }
Assert-DmfReleaseSource -Run (New-Run) @source
Assert $true 'Successful manual recovery accepted'
$active = New-Run; $active.status='in_progress'; $active.conclusion=$null
Assert-DmfReleaseSource -Run $active @source -Automatic -CurrentRunId '42' -CurrentTag $source.Tag
Assert $true 'Own active automatic tag run accepted'
foreach ($path in @('.github/workflows/build-artifacts.yml', '.github/workflows/build-artifacts.yml@refs/tags/v1.1.159-playcast.1')) {
    $run = New-Run; $run.path=$path
    Assert-DmfReleaseSource -Run $run @source
    Assert $true "Supported workflow path: $path"
}
foreach ($path in @('.github/workflows/build-artifacts.yml@', '.github/workflows/build-artifacts.yml.other@main', 'elsewhere/.github/workflows/build-artifacts.yml@main', ".github/workflows/build-artifacts.yml@main`n")) {
    $run = New-Run; $run.path=$path
    Reject { Assert-DmfReleaseSource -Run $run @source } "Invalid workflow path: $path"
}
foreach ($change in @(
    @{field='status';value='in_progress'}, @{field='conclusion';value='failure'},
    @{field='conclusion';value='cancelled'}, @{field='conclusion';value='skipped'},
    @{field='id';value=43}, @{field='event';value='pull_request'},
    @{field='path';value='.github/workflows/other.yml'}, @{field='head_sha';value=('b'*40)}
)) {
    $run = New-Run; $run.($change.field)=$change.value
    Reject { Assert-DmfReleaseSource -Run $run @source } "Run rejection: $($change.field)/$($change.value)"
}
$run = New-Run; $run.repository.full_name='other/DMF'
Reject { Assert-DmfReleaseSource -Run $run @source } 'Foreign repository rejected'
$run = New-Run; $run.head_repository.full_name='other/DMF'
Reject { Assert-DmfReleaseSource -Run $run @source } 'Foreign source repository rejected'
Reject { Assert-DmfReleaseSource -Run $active @source -Automatic -CurrentRunId '43' -CurrentTag $source.Tag } 'Different active run rejected'
Reject { Assert-DmfReleaseSource -Run $active @source -Automatic -CurrentRunId '42' -CurrentTag 'v1.1.159' } 'Different automatic tag rejected'
Reject { Assert-DmfReleaseSource -Run (New-Run) @source -Automatic -CurrentRunId '42' -CurrentTag $source.Tag } 'Completed source cannot impersonate active automatic run'

$remote = @{Repository='PlayCastDotIo/DMF';Tag='v1.1.159-playcast.1'}
$lightweight = {
    param($path)
    Assert ($path -ceq 'repos/PlayCastDotIo/DMF/git/ref/tags/v1.1.159-playcast.1') 'Exact tag endpoint used'
    [pscustomobject]@{ref='refs/tags/v1.1.159-playcast.1';object=@{type='commit';sha=('a'*40)}}
}
Assert ((Get-DmfRemoteTagCommit @remote -ReadApi $lightweight) -ceq ('a'*40)) 'Lightweight tag resolves to commit'
$annotated = {
    param($path)
    if ($path -ceq 'repos/PlayCastDotIo/DMF/git/ref/tags/v1.1.159-playcast.1') {
        return [pscustomobject]@{ref='refs/tags/v1.1.159-playcast.1';object=@{type='tag';sha=('b'*40)}}
    }
    Assert ($path -ceq ('repos/PlayCastDotIo/DMF/git/tags/'+('b'*40))) 'Annotated tag object endpoint used'
    [pscustomobject]@{object=@{type='commit';sha=('a'*40)}}
}
Assert ((Get-DmfRemoteTagCommit @remote -ReadApi $annotated) -ceq ('a'*40)) 'Annotated tag resolves to commit'
Reject { Get-DmfRemoteTagCommit @remote -ReadApi { throw 'API failure' } } 'Remote API errors reject release'
Reject { Get-DmfRemoteTagCommit @remote -ReadApi { [pscustomobject]@{ref='refs/heads/v1.1.159-playcast.1';object=@{type='commit';sha=('a'*40)}} } } 'Branch cannot substitute for tag'
Reject { Get-DmfRemoteTagCommit @remote -ReadApi { [pscustomobject]@{ref='refs/tags/v1.1.159-playcast.1';object=@{type='commit';sha='invalid'}} } } 'Malformed remote SHA rejected'
Reject { Get-DmfRemoteTagCommit @remote -ReadApi { [pscustomobject]@{ref='refs/tags/v1.1.159-playcast.1';object=@{type='tree';sha=('a'*40)}} } } 'Non-commit tag target rejected'
Reject { Get-DmfRemoteTagCommit @remote -ReadApi { [pscustomobject]@{ref='refs/tags/v1.1.159-playcast.1';object=@{type='tag';sha=('b'*40)}} } } 'Unbounded annotated tag chain rejected'

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('dmf-release-tests-'+[guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot
function New-Fixture([string]$Name, [switch]$Partial) {
    $root = Join-Path $testRoot $Name
    $sourceRoot = Join-Path $root 'source'
    foreach ($folder in @('Dmf','Dmf/Framework','Dmf/Modules.Library','Dmf/Modules.Template')) {
        $directory = Join-Path $sourceRoot $folder
        $null = New-Item -ItemType Directory -Force -Path $directory
        [IO.File]::WriteAllText((Join-Path $directory 'fixture.h'), '// header')
        [IO.File]::WriteAllText((Join-Path $directory 'fixture.c'), '// excluded source')
    }
    $binaries = Join-Path $root 'binaries'
    foreach ($os in @('20H1','24H2')) { foreach ($platform in @('x64','arm64')) { foreach ($configuration in @('Debug','Release')) {
        if ($Partial -and $os -ceq '20H1' -and $platform -ceq 'x64' -and $configuration -ceq 'Debug') { continue }
        $directory = Join-Path $binaries "Bin_${os}_${platform}_${configuration}/lib/DmfK"
        $null = New-Item -ItemType Directory -Force -Path $directory
        [IO.File]::WriteAllText((Join-Path $directory 'DmfK.lib'), 'library fixture')
        [IO.File]::WriteAllText((Join-Path $directory 'DmfK.pdb'), 'excluded symbols')
    } } }
    return @{ SourceRoot=$sourceRoot; BinariesRoot=$binaries; OutputDirectory=(Join-Path $root 'archives') }
}
try {
    $fixture = New-Fixture 'complete'
    & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture
    $archives = @(Get-ChildItem -LiteralPath $fixture.OutputDirectory -File -Filter '*.zip')
    Assert ($archives.Count -eq 9) 'Complete release has nine archives'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($archive in $archives) {
        $zip = [IO.Compression.ZipFile]::OpenRead($archive.FullName)
        try {
            $names = @($zip.Entries | Where-Object { $_.FullName -notmatch '[/\\]$' } | ForEach-Object { $_.FullName.Replace([char]92,[char]47) })
            if ($archive.Name -ceq 'DMF-Headers.zip') {
                Assert ($names.Count -eq 4) 'All four header locations packed'
                Assert ($names -ccontains 'include/Dmf/Framework/fixture.h') 'Header layout retained'
                Assert (@($names | Where-Object { $_ -notlike '*.h' }).Count -eq 0) 'Only headers included'
            } else {
                $drop = $archive.BaseName.Replace('_libs','')
                Assert ($names.Count -eq 1 -and $names[0] -ceq "$drop/lib/DmfK/DmfK.lib") "Architecture layout retained: $drop"
            }
        } finally { $zip.Dispose() }
    }
    Reject { & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture } 'Existing archives are not overwritten'
    $fixture = New-Fixture 'partial' -Partial
    Reject { & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture } 'Incomplete matrix rejected'
    $fixture = New-Fixture 'empty-library'
    Remove-Item -LiteralPath (Join-Path $fixture.BinariesRoot 'Bin_20H1_x64_Debug/lib/DmfK/DmfK.lib')
    Reject { & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture } 'Empty library artifact rejected'
    $fixture = New-Fixture 'missing-header'
    Remove-Item -LiteralPath (Join-Path $fixture.SourceRoot 'Dmf/Framework/fixture.h')
    Reject { & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture } 'Missing headers rejected'
    $fixture = New-Fixture 'extra-artifact'
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture.BinariesRoot 'unexpected')
    Reject { & (Join-Path $PSScriptRoot 'New-ReleaseArchives.ps1') @fixture } 'Unexpected matrix artifact rejected'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notlike 'dmf-release-tests-*') { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
Write-Host "DMF release tooling: $script:checks checks passed (offline; no release published)."
