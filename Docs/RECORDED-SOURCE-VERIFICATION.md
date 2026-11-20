# Recorded source identities

The [license manifest](licenses/SHA256.json) records 18 local source references covering 17 distinct files across ten notices. The PSVR2 release source matches 17 references, including both sound-core files. An independent check also covers the 15 recorded Dear ImGui source files. Some files occur in both checks.

The remaining historical entry is `Src/CPU/PowerPC/ppc.cpp`. Its recorded SHA-256, `9752c5fc141e197a6d6228819749b5c59c85176d068941b296c2d5efe3f0dbc2`, identifies the file in the **Rift S source archive**. The PSVR2 implementation includes updated idle handling and has a different identity. The [policy](../packaging/VendoredSourcePolicy.json) pins its line-normalized SHA-256 as well. The original recorded hashes remain intact.

## Verification

From a Windows checkout, run:

```powershell
./packaging/Test-ReleaseNotices.ps1 -Root . -SourceRoot .
./packaging/Test-VendoredSourceIdentitiesFixtures.ps1
```

The checker strips the historical `Supermodel/` path prefix and normalizes line endings to CRLF for the 18 source references. It verifies notice files using their raw bytes and uses Git's byte convention for the ImGui source checks. The audited subset accepts the reviewed difference for the PSVR2 PowerPC file and rejects any new drift. Strict matching of all 18 historical references against this PSVR2 tree reports the known edition difference. [Edition builds](BUILDING.md)
