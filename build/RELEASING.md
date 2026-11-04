# Playcast DMF releases

After the release workflow change is merged, tag the reviewed commit and push
that tag. Use a Playcast suffix for fork changes so Microsoft tags stay available:

```powershell
git fetch origin master --tags
git tag -a v1.1.159-playcast.1 origin/master -m "Playcast DMF 1.1.159 revision 1"
git push origin refs/tags/v1.1.159-playcast.1
```

The tag is selected explicitly. Increment the final revision for another fork
release based on the same upstream version; never move an already published tag.
Plain tags such as `v1.1.159` remain supported for compatibility. Accepted tags
have three numeric version components and an optional `-playcast.N` suffix,
where N is positive and has no leading zeros.

The Build and Release DMF workflow builds all eight combinations of 20H1/24H2,
x64/arm64 and Debug/Release. Only a tag push schedules publishing, after the whole
matrix succeeds. Branch pushes retain build artifacts without creating releases.
Packaging requires every matrix drop and nonempty library sets, and takes headers
from the exact built commit. The nine ZIP names and extracted directory layouts
are preserved for existing consumers. GitVersion does not choose or rewrite the
published release tag.

`v1.1.159-playcast.1` is published as a normal GitHub release and marked Latest,
as agreed for supported fork builds. Although the suffix is a SemVer prerelease
identifier, GitHub's release classification is explicitly normal. Consumers
should pin the full tag rather than infer compatibility from Latest.

For manual recovery, dispatch Pack and Release from `master` with both the successful build
run ID and an existing release tag resolving to that run's commit. Failed,
cancelled, unrelated and mismatched source runs are rejected. A failed build
must be rerun successfully first. Packaging tooling is retained before checking
out source headers, so recovery can package an older matching source commit.
The normal workflow GITHUB_TOKEN supplies
publication rights; RELEASE_TOKEN and GitVersion release actions are unnecessary.

The workflow resolves exact `refs/tags/` references through the authenticated
GitHub API, including annotated tags. It does not require Git credentials to
remain in the checkout configuration, including in a private repository.

Publication verifies that the tag still names the built commit and attaches all
nine archives. Existing releases/assets are not overwritten. If publication fails
after creating a draft, inspect that draft before retrying; do not silently replace
a published release. No tag or GitHub release is created by the offline tests:

```powershell
./build/Test-Release.ps1
```

## Repository tag protections (administrator setup)

The workflow SHA comparisons guard against stale runs and accidental tag movement;
they do not enforce immutability or prevent a privileged user from changing a tag
before a build. Configure platform rules separately, with explicit administrator
approval. Recommended rules for `refs/tags/v*`:

- An active tag ruleset blocks updates and deletion with no configured bypass.
- A separate active creation ruleset permits only repository administrators to
  create matching tags. Its administrator bypass applies only to creation, so it
  cannot bypass the first ruleset's update/deletion restrictions.

These protections are not installed by the release workflow or its PR. Repository
administrators can still edit rulesets; control changes to those settings through
the organization's administration process. Create a new tag/revision for repairs.
