[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$MSBuildPath
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repositoryRoot '.output/host-contracts/notify-accounting'
$generated = Join-Path $outputRoot "generated/$Configuration"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$sourcePath = Join-Path $repositoryRoot 'Dmf/Modules.Library/Dmf_NotifyUserWithRequest.c'
$source = [IO.File]::ReadAllText($sourcePath)
# Compile the production method with injected framework boundaries, rather than
# maintaining a second implementation of its accounting logic.
$pattern = '(?ms)^_IRQL_requires_max_\(DISPATCH_LEVEL\)\r?\n_Must_inspect_result_\r?\nNTSTATUS\r?\nDMF_NotifyUserWithRequest_EventRequestAdd\(.*?(?=^_IRQL_requires_max_)'
$methods = [regex]::Matches($source, $pattern)
if ($methods.Count -ne 1) { throw 'Expected exactly one production request-add method.' }
[IO.File]::WriteAllText((Join-Path $generated 'NotifyRequestAdd.inc'), $methods[0].Value)
if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild was not found.' }
$project = Join-Path $PSScriptRoot 'host-tests/NotifyRequestAccountingTests.vcxproj'
& $MSBuildPath $project /m /nologo /nr:false /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 "/p:DmfTestIncludeDirectory=$generated" "/p:DmfTestOutputRoot=$outputRoot"
if ($LASTEXITCODE) { throw "Accounting test build failed: $LASTEXITCODE" }
& (Join-Path $outputRoot "$Configuration/NotifyRequestAccountingTests.exe")
if ($LASTEXITCODE) { throw "Accounting tests failed: $LASTEXITCODE" }
