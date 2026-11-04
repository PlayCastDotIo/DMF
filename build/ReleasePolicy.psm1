function Get-DmfReleaseTag {
    param([Parameter(Mandatory)][string]$Tag)
    if ($Tag -cnotmatch '\Av(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-playcast\.[1-9][0-9]*)?\z') {
        throw 'Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-playcast.REVISION (positive revision).'
    }
    return $Tag
}

function Assert-DmfReleaseSource {
    param(
        [Parameter(Mandatory)]$Run,
        [Parameter(Mandatory)][string]$RunId,
        [Parameter(Mandatory)][string]$Repository,
        [Parameter(Mandatory)][string]$Tag,
        [Parameter(Mandatory)][string]$TagCommit,
        [switch]$Automatic,
        [string]$CurrentRunId,
        [string]$CurrentTag
    )
    $null = Get-DmfReleaseTag $Tag
    if ($RunId -notmatch '\A[1-9][0-9]*\z' -or [string]$Run.id -cne $RunId) { throw 'Source run ID mismatch.' }
    if ($Run.path -cnotmatch '\A\.github/workflows/build-artifacts\.yml(?:@[^\r\n]+)?\z' -or $Run.event -cne 'push') { throw 'Source must be the DMF push-build workflow.' }
    if ($Run.repository.full_name -cne $Repository -or $Run.head_repository.full_name -cne $Repository) { throw 'Source repository mismatch.' }
    if ($Run.head_sha -notmatch '\A[0-9a-f]{40}\z' -or $Run.head_sha -cne $TagCommit) { throw 'Tag does not identify the built commit.' }
    if ($Automatic) {
        # The caller's release job has needs: build, covering the complete matrix.
        if ($CurrentRunId -cne $RunId -or $CurrentTag -cne $Tag -or $Run.status -cne 'in_progress' -or $null -ne $Run.conclusion) {
            throw 'Automatic release must use its own active tag-build run.'
        }
    } elseif ($Run.status -cne 'completed' -or $Run.conclusion -cne 'success') {
        throw 'Manual recovery requires a completed successful source run.'
    }
}
function Get-DmfRemoteTagCommit {
    param(
        [Parameter(Mandatory)][string]$Repository,
        [Parameter(Mandatory)][string]$Tag,
        [scriptblock]$ReadApi = {
            param($Path)
            $json = gh api $Path
            if ($LASTEXITCODE) { throw 'Could not resolve the remote release tag.' }
            return $json | ConvertFrom-Json
        }
    )
    $Tag = Get-DmfReleaseTag $Tag
    $reference = & $ReadApi "repos/$Repository/git/ref/tags/$Tag"
    if ($reference.ref -cne "refs/tags/$Tag") { throw 'Remote reference is not the requested tag.' }
    $object = $reference.object
    # Resolve lightweight and annotated tags without relying on stored Git credentials.
    for ($depth = 0; $depth -lt 8; ++$depth) {
        if ($object.sha -cnotmatch '\A[0-9a-f]{40}\z') { throw 'Invalid remote tag object SHA.' }
        if ($object.type -ceq 'commit') { return $object.sha }
        if ($object.type -cne 'tag') { throw 'Release tag does not identify a commit.' }
        $annotation = & $ReadApi "repos/$Repository/git/tags/$($object.sha)"
        $object = $annotation.object
    }
    throw 'Annotated tag nesting exceeds the supported depth.'
}
Export-ModuleMember -Function Get-DmfReleaseTag, Assert-DmfReleaseSource, Get-DmfRemoteTagCommit
