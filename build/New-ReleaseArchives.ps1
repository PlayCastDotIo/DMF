[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$BinariesRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$BinariesRoot = [IO.Path]::GetFullPath($BinariesRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Archive output must be a new directory.' }
$expected = foreach ($os in @('20H1','24H2')) { foreach ($platform in @('x64','arm64')) { foreach ($configuration in @('Debug','Release')) {
    "Bin_${os}_${platform}_${configuration}"
} } }
$drops = @(Get-ChildItem -LiteralPath $BinariesRoot -Directory)
if ($drops.Count -ne $expected.Count -or @($drops | Where-Object { $_.Name -cnotin $expected }).Count) { throw 'Expected exactly the eight DMF matrix artifacts.' }
foreach ($name in $expected) {
    $lib = Join-Path $BinariesRoot "$name/lib"
    if (!(Test-Path -LiteralPath $lib) -or !(Get-ChildItem -LiteralPath $lib -Recurse -File -Filter '*.lib')) { throw "Missing libraries: $name" }
}
foreach ($folder in @('Dmf','Dmf/Framework','Dmf/Modules.Library','Dmf/Modules.Template')) {
    if (!(Get-ChildItem -LiteralPath (Join-Path $SourceRoot $folder) -File -Filter '*.h')) { throw "Missing headers: $folder" }
}
$null = New-Item -ItemType Directory -Path $OutputDirectory
$staging = Join-Path $OutputDirectory 'staging'
$null = New-Item -ItemType Directory -Path $staging
foreach ($name in $expected) {
    $lib = Join-Path $BinariesRoot "$name/lib"
    $drop = Join-Path $staging $name
    foreach ($file in Get-ChildItem -LiteralPath $lib -Recurse -File -Filter '*.lib') {
        $relative = [IO.Path]::GetRelativePath($lib, $file.FullName)
        $destination = Join-Path $drop "lib/$relative"
        $null = New-Item -ItemType Directory -Force -Path (Split-Path $destination)
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    Compress-Archive -LiteralPath $drop -DestinationPath (Join-Path $OutputDirectory "${name}_libs.zip")
}
$include = Join-Path $staging 'include'
foreach ($folder in @('Dmf','Dmf/Framework','Dmf/Modules.Library','Dmf/Modules.Template')) {
    $directory = Join-Path $SourceRoot $folder
    $files = if ($folder -ceq 'Dmf') { Get-ChildItem -LiteralPath $directory -File -Filter '*.h' }
        else { Get-ChildItem -LiteralPath $directory -Recurse -File -Filter '*.h' }
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($SourceRoot, $file.FullName)
        $destination = Join-Path $include $relative
        $null = New-Item -ItemType Directory -Force -Path (Split-Path $destination)
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
}
Compress-Archive -LiteralPath $include -DestinationPath (Join-Path $OutputDirectory 'DMF-Headers.zip')
Write-Host 'Prepared eight library archives and matching source headers.'
