"""Write the CSV of known names that tools/ghidra/seed.ps1 applies to the Ghidra project.

Sources, in the order they're applied (later ones win a name clash at one address):
  1. LExSDKv2's hardcoded RVA table (external/LExSDKv2/Src/LESDK/Init.hpp, the RVA branch),
  2. tools/ghidra/symbols-<game>.csv, hand-curated from src/ and research/.
(The mod's natives dump, %LOCALAPPDATA%\\MEVR\\dump\\natives-<build>.csv, is passed separately,
before these, so our names win over the generic exec names.)

    python -I tools/ghidra/collect_symbols.py LE1 <out.csv>

Output columns: rva,name,kind,comment (kind: func | label | data).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# BUILTIN_<X>_RVA -> (name, kind). Unknown ones become LESDK_<X>.
SDK_NAMES = {
    'CALLFUNCTION': ('UObject::CallFunction', 'func'),
    'PROCESSEVENT': ('UObject::ProcessEvent', 'func'),
    'PROCESSINTERNAL': ('UObject::ProcessInternal', 'func'),
    'GENGINE': ('GEngine', 'data'),
    'GMALLOC': ('GMalloc', 'data'),
    'GNATIVES': ('GNatives', 'data'),
    'GOBJECTS': ('GObjObjects', 'data'),
    'GSYS': ('GSys', 'data'),
    'GWORLD': ('GWorld', 'data'),
    'GERROR': ('GError', 'data'),
    'SFXNAMEPOOLS': ('SFXName::NamePools', 'data'),
    'SFXNAMEINIT': ('SFXName::Init', 'func'),
    'STATICALLOCATEOBJECT': ('UObject::StaticAllocateObject', 'func'),
    'STATICCONSTRUCTOBJECT': ('UObject::StaticConstructObject', 'func'),
    'UWORLDSPAWNACTOR': ('UWorld::SpawnActor', 'func'),
    'UENGINE_EXEC': ('UEngine::Exec', 'func'),
    'UGAMEENGINE_EXEC': ('UGameEngine::Exec', 'func'),
    'VERIFYIMPORT': ('ULinkerLoad::VerifyImport', 'func'),
    'CREATEEXPORT': ('ULinkerLoad::CreateExport', 'func'),
    'LOADPACKAGE': ('UObject::LoadPackage', 'func'),
    'LOADPACKAGEASYNCTICK': ('LoadPackageAsyncTick', 'func'),
    'OBJECT_LOGINTERNAL': ('UObject::LogInternal', 'func'),
    'APPLOGF': ('appLogf', 'func'),
    'APPERRORLOGF': ('appErrorLogf', 'func'),
    'GTLKTABLE': ('GTlkTable', 'data'),
    'GISREQUESTINGEXIT': ('GIsRequestingExit', 'data'),
    'GAMEENGINETICK': ('UGameEngine::Tick', 'func'),
    'TLKLOOKUP': ('TlkLookup', 'func'),
    'SETLINKER': ('UObject::SetLinker', 'func'),
    'FINDPACKAGEFILE': ('FindPackageFile', 'func'),
    'UWORLD_FARMOVEACTOR': ('UWorld::FarMoveActor', 'func'),
    'UTEXTURE2D_SERIALIZE': ('UTexture2D::Serialize', 'func'),
    'OODLE_DECOMPRESS': ('OodleDecompress', 'func'),
    'CALCSCENEVIEW': ('ULocalPlayer::CalcSceneView', 'func'),
}


def sdk_rows(game):
    text = (ROOT / 'external/LExSDKv2/Src/LESDK/Init.hpp').read_text(encoding='utf-8', errors='replace')
    # The RVA table: '#if defined(SDK_TARGET_LE1)' ... up to the next '#elif'/'#endif'.
    blocks = re.split(r'^\s*#\s*(?:if|elif)\s+defined\((SDK_TARGET_LE\d)\)', text, flags=re.M)
    rows, seen = [], set()
    for i in range(1, len(blocks), 2):
        if blocks[i] != f'SDK_TARGET_{game}':
            continue
        body = re.split(r'^\s*#\s*endif', blocks[i + 1], maxsplit=1, flags=re.M)[0]
        for line in body.splitlines():
            if line.lstrip().startswith('//'):
                continue
            m = re.search(r'#define\s+BUILTIN_(\w+?)_RVA\s+::LESDK::Address::FromOffset\((0x[0-9A-Fa-f]+)\)', line)
            if not m or m.group(1) in seen:
                continue
            seen.add(m.group(1))
            name, kind = SDK_NAMES.get(m.group(1), (f'LESDK_{m.group(1)}', 'func'))
            rows.append((int(m.group(2), 16), name, kind, f'LExSDKv2 Init.hpp BUILTIN_{m.group(1)}_RVA'))
    if not rows:
        sys.exit(f'no BUILTIN_*_RVA entries for {game} in LExSDKv2 Init.hpp (submodule not checked out?)')
    return rows


def curated_rows(game):
    rows = []
    path = ROOT / f'tools/ghidra/symbols-{game.lower()}.csv'
    if not path.exists():
        return rows
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('rva,'):
            continue
        parts = line.split(',', 3)
        if len(parts) < 3 or parts[2] not in ('func', 'label', 'data'):
            sys.exit(f'{path}:{n}: expected rva,name,kind[,comment]')
        rows.append((int(parts[0], 16), parts[1], parts[2], parts[3] if len(parts) > 3 else ''))
    return rows


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    game, out = sys.argv[1].upper(), sys.argv[2]
    sdk, curated = sdk_rows(game), curated_rows(game)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('rva,name,kind,comment\n')
        for rva, name, kind, comment in sdk + curated:
            f.write(f'0x{rva:X},{name},{kind},{comment}\n')
    print(f'{len(sdk)} LExSDKv2 + {len(curated)} curated names -> {out}')


if __name__ == '__main__':
    main()
