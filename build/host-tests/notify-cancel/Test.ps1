[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [string]$MSBuildPath)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$output = Join-Path $root '.output/host-contracts/notify-cancel'
$generated = Join-Path $output "generated/$Configuration"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $root 'Dmf/Modules.Library/Dmf_NotifyUserWithRequest.c'))
$methods = [regex]::Matches($source, '(?ms)^VOID\r?\nEvtIoCanceledOnQueue\(.*?(?=^_IRQL_requires_max_)')
if ($methods.Count -ne 1) { throw 'Expected exactly one production cancellation callback.' }
[IO.File]::WriteAllText((Join-Path $generated 'NotifyRequestCancel.inc'), $methods[0].Value)
Copy-Item -LiteralPath (Join-Path $root 'Dmf/Modules.Library/Dmf_NotifyUserWithRequest.CancelAndJoin.h') -Destination $generated
if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild was not found.' }
& $MSBuildPath (Join-Path $PSScriptRoot 'NotifyRequestCancelAndJoinTests.vcxproj') /m /nologo /nr:false /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 "/p:DmfTestIncludeDirectory=$generated" "/p:DmfTestOutputRoot=$output"
if ($LASTEXITCODE) { throw "Host build failed: $LASTEXITCODE" }
& (Join-Path $output "$Configuration/NotifyRequestCancelAndJoinTests.exe")
if ($LASTEXITCODE) { throw "Host contracts failed: $LASTEXITCODE" }