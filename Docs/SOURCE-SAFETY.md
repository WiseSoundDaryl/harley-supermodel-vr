# Source-distribution policy

This repository contains emulator source, build and validation scripts, notices, documentation, and reviewed documentation images. Player packages belong in separately reviewed release assets.

Keep ROM archives, extracted game data, save states, NVRAM, personal settings, recovery journals, private captures, runtime binaries, compiled objects and profiling output out of source commits. Do not copy a development installation into the repository. `Config/Games.xml` and `Config/Music.xml` are source metadata; ROM filenames and checksums in them are not the ROM contents. The two upstream crosshair bitmaps and images under `Docs/Images/` have explicit policy treatment.

`.gitignore` prevents common accidental additions, including case variants used by the portable Windows layout. It does not protect files that are already tracked or added with `git add -f`.

Run the independent check before committing:

```powershell
packaging/Test-SourceSafety.ps1 -Root .
packaging/Test-SourceSafetyFixtures.ps1
```

The checker reads the Git index and its blobs, not just filenames or the current working copies. It rejects prohibited paths, archives and recognizable executable/object signatures, and unexpected binary content outside permitted image locations. It also rejects unresolved entries, links and submodules pending explicit review. It is read-only; it never deletes a file or rewrites history. `-ReportOnly` returns findings for review instead of throwing; CI uses the default failing behavior.

Tests generate harmless text, small signature probes and disposable Git indexes. They demonstrate rejection even when prohibited files are force-added or renamed, and acceptance of legitimate source, documentation, metadata and images. No real ROM, player save, diagnostic recording or executable is used.

Passing checks do not establish redistribution rights, an exhaustive secrets audit, or a clean history. Image signatures establish file type, not permission to publish the content. Review new images and source dependencies before adding them. A future legitimate asset that conflicts with policy needs a narrow, documented exception and regression coverage.

Before release, inspect the selected public refs and their history separately. Distinguish inherited templates and dependency fixtures from personal data and game files. If a potentially sensitive artifact is found, record its identity privately, avoid copying its contents into an issue or log, and arrange maintainer review. Do not automatically rewrite the shared history or delete local development evidence. Keep the corresponding-source, license, package and headset gates in the [release checklist](RELEASE-CHECKLIST.md) open until independently verified.
