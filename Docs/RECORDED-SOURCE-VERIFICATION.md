# Recorded source verification

The [license manifest](licenses/SHA256.json) contains 18 structured local source references across 10 notice entries, covering 17 distinct files. The November 9, 2026 audit matched 15 references (14 distinct files) to the public checkout after the byte convention below was applied. Some references concern ImGui files already covered by its separate 15-file Git-identity check; these counts are not additive.

## Unresolved identities

Three recorded references describe different source versions from those currently published:

| Recorded source | Finding |
| --- | --- |
| `Src/CPU/PowerPC/ppc.cpp` | The recorded development version includes private Harley changes and differs from the public PowerPC source. |
| `Src/Sound/SCSP.cpp` | The recorded development version predates the sound-core changes present in the public checkout. |
| `Src/Sound/SCSPDSP.cpp` | The recorded development version predates the sound-core changes present in the public checkout. |

The original recorded hashes are preserved. [The verification policy](../packaging/VendoredSourcePolicy.json) identifies the audited reference inventory and keeps these three findings explicit. Its public canonical SHA-256 values pin the reviewed mismatching files so further drift requires review; they do not replace the manifest's SHA-256 values or establish a match to them. Pending references are not automatically promoted if their content later changes.

## Paths and byte convention

Only the exact historical `Supermodel/` prefix is removed from a recorded local path before resolving it beneath `SourceRoot`. Absolute paths, traversal, invalid path components and linked files/directories are rejected. The reviewed policy requires the expected notice/path inventory rather than silently skipping an unfamiliar reference.

The recorded local SHA-256 values use the development checkout's CRLF byte convention. Validation replaces CRLF with LF, then replaces LF with CRLF, before hashing. Bare carriage returns, byte-order marks, whitespace, encoding and every other byte are preserved. This is byte processing, not text decoding, trimming or source reformatting. The pending public identities use the same convention. Notice/license files remain byte-exact, and ImGui retains its separate documented Git-blob verification.

## Running the checks

```powershell
packaging/Test-VendoredSourceIdentities.ps1 -Scope AuditedSubset
packaging/Test-VendoredSourceIdentitiesFixtures.ps1
```

CI and `Test-ReleaseNotices.ps1 -SourceRoot .` explicitly select `AuditedSubset`: they verify the 15 supported references, reject unexpected inventory or identity changes, and report three pending findings with `AllRecordedSourcesVerified=false`. A successful subset result does not verify the pending sources.

For strict verification of every recorded local identity, run `packaging/Test-VendoredSourceIdentities.ps1` without a scope argument, or use `-Scope All`. This currently fails on the three unresolved identities. Review the corresponding source versions before changing the policy or manifest; do not refresh hashes merely to make the check pass.

The tests exercise the verifier using disposable fixtures and leave source inputs unchanged. They do not download remote provenance URLs, authenticate upstream authorship, inspect the contents of linked binaries, build HarleyVR, or use ROMs or VR hardware. URL-only references and hashes embedded in prose are outside this local-file check. A matching hash is evidence of file identity against the reviewed record, not legal clearance, a complete corresponding-source review, or release readiness.
