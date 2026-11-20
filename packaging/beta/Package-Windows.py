"""Pack independently built emulator binaries with the HarleyVR Windows launchers.

Run from the extracted launcher source directory after following README.md.
Without --verified-release this always produces a source/package draft.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

HERE = Path(__file__).resolve().parent
ASSETS = {"SteamVR":"HarleyVR-PSVR2-source.zip","Meta":"HarleyVR-RiftS-source.zip","Launchers":"HarleyVR-Launchers-source.zip"}
DLLS = ("libopenxr_loader.dll","libgcc_s_seh-1.dll","libstdc++-6.dll","libwinpthread-1.dll")


def sha(data):
    return hashlib.sha256(data).hexdigest().upper()


def collect(root, names, into, prefix=""):
    for name in names:
        into[prefix+name]=(root/name).read_bytes()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--steamvr",type=Path,required=True,help="PSVR2 executable and four x64 DLLs")
    p.add_argument("--meta",type=Path,required=True,help="Rift S executable and four x64 DLLs")
    p.add_argument("--launchers",type=Path,required=True,help="Output of Launcher/Build-Launcher.ps1")
    p.add_argument("--output",type=Path,required=True,help="New destination ZIP")
    p.add_argument("--source-assets",type=Path,help="Directory containing all three source ZIPs")
    p.add_argument("--verified-release",type=Path,help="Maintainer-reviewed release record")
    a=p.parse_args()
    if a.output.exists():
        p.error("Output already exists.")
    data={}
    layout=json.loads((HERE/"install-files.json").read_text(encoding="utf-8"))
    for section, directory in (("common","data"),("scripts","packaging")):
        collect(HERE/directory,layout[section],data)
    collect(a.launchers,("HarleyVR.exe","HarleyVR-Meta.exe","HarleyVR-SteamVR.exe"),data)
    for route, directory in (("Meta",a.meta),("SteamVR",a.steamvr)):
        collect(directory,("supermodel.exe",*DLLS),data,"bin/"+route+"/")
    if a.verified_release:
        record=json.loads(a.verified_release.read_text(encoding="utf-8"))
        if not a.source_assets or not re.fullmatch(r"v[0-9][0-9A-Za-z.-]*",record["tag"]):
            p.error("Provide source assets and a valid release tag.")
        if set(record["sources"])!=set(ASSETS) or set(record["files"])!=set(data):
            p.error("Release record does not cover the complete input set.")
        files=[]
        for name,content in sorted(data.items()):
            if sha(content)!=record["files"][name]:
                p.error("Release input differs from the reviewed bytes: "+name)
            files.append({"path":name.replace("/","\\"),"sha256":sha(content)})
        sources=[]
        for route,name in ASSETS.items():
            checksum=sha((a.source_assets/name).read_bytes())
            if checksum!=record["sources"][route]:
                p.error("Source asset differs from reviewed bytes: "+name)
            url=f"https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases/download/{record['tag']}/{name}"
            sources.append({"route":route,"sha256":checksum,"url":url})
        manifest={"schema":2,"sessionContract":2,"releaseReady":True,"mode":"regular","tag":record["tag"],
                  "validatedRoutes":["Meta","SteamVR"],"files":files,"sources":sources}
        data["release-ready.json"]=(json.dumps(manifest,indent=2)+"\n").encode()
    with ZipFile(a.output,"x",compression=ZIP_DEFLATED,compresslevel=6) as out:
        for name,content in sorted(data.items()):
            out.writestr(name,content)
    print(f"{len(data)} files; SHA-256 {sha(a.output.read_bytes())}")


if __name__=="__main__":
    main()
