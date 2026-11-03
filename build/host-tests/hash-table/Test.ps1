[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [string]$MSBuildPath)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$output = Join-Path $root '.output/host-contracts/hash-table'
$generated = Join-Path $output "generated/$Configuration"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $root 'Dmf/Modules.Library/Dmf_HashTable.c'))
$source = [regex]::Replace($source, '(?m)^#include "(?:DmfModule\.h|DmfModules\.Library(?:\.Trace)?\.h)"\r?\n', '')
[IO.File]::WriteAllText((Join-Path $generated 'HashTable.inc'), $source)
foreach ($header in @('Dmf_HashTable.h','Dmf_HashTable.Operations.h')) {
    Copy-Item -LiteralPath (Join-Path $root "Dmf/Modules.Library/$header") -Destination $generated
}
if (!$MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild was not found.' }
& $MSBuildPath (Join-Path $PSScriptRoot 'HashTableRemovalTests.vcxproj') /m /nologo /nr:false /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 "/p:DmfTestIncludeDirectory=$generated" "/p:DmfTestOutputRoot=$output"
if ($LASTEXITCODE) { throw "Host build failed: $LASTEXITCODE" }
& (Join-Path $output "$Configuration/HashTableRemovalTests.exe")
if ($LASTEXITCODE) { throw "Host contracts failed: $LASTEXITCODE" }