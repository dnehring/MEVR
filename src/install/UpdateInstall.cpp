// SPDX-License-Identifier: MIT
#include "install/Updates.hpp"
#include "install/Http.hpp"
#include "install/Packs.hpp"

#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <format>

namespace mevr::install::updates {
namespace {
namespace fs = std::filesystem;

// ZipArchive streams regular files only: archive links are never materialized. Validate all names before writing
// any entry, reject Windows aliases/ADS and duplicates, and cap expanded bytes. Paths are separate -File arguments,
// never interpolated PowerShell code. The directory is fresh and private to this download attempt.
constexpr char kExtract[] = R"PS(param([string]$Archive,[string]$Destination)
$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $base=[IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\'
 $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
 try {
  if($zip.Entries.Count -gt 20000){throw 'Too many archive entries'}
  $names=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  [long]$total=0
  foreach($entry in $zip.Entries){
   $name=$entry.FullName.Replace('\','/')
   $parts=$name.TrimEnd('/').Split('/')
   if(!$name -or $name.StartsWith('/') -or $name -match '[:\x00-\x1f]' -or !$names.Add($name.TrimEnd('/'))){throw 'Unsafe or duplicate archive path'}
   foreach($part in $parts){
    if(!$part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)' -or $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0){throw 'Unsafe archive path'}
   }
   $target=[IO.Path]::GetFullPath([IO.Path]::Combine($base,$name))
   if(!$target.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive path escapes destination'}
   $mode=($entry.ExternalAttributes -shr 16) -band 0xF000
   if($mode -ne 0 -and $mode -ne 0x8000 -and $mode -ne 0x4000){throw 'Archive contains a link or special file'}
   $total+=$entry.Length
   if($total -gt 4294967296 -or $entry.Length -gt 1073741824){throw 'Expanded archive is too large'}
  }
  foreach($entry in $zip.Entries){
   $name=$entry.FullName.Replace('\','/')
   $target=[IO.Path]::GetFullPath([IO.Path]::Combine($base,$name))
   if($name.EndsWith('/')){[void][IO.Directory]::CreateDirectory($target);continue}
   [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
   [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
   Write-Output 'Extracting update'
  }
 } finally {$zip.Dispose()}
 exit 0
} catch {Write-Output $_.Exception.Message;exit 1}
)PS";

bool MatchesVersion(fs::path const& file, std::string const& version) {
    auto const v = detect::FileVersion(file);
    return v && CompareVersions(v->ToString(), version) == 0;
}
} // namespace

Prepared Prepare(Release const& release, Progress const& progress, std::atomic<bool> const* cancel) {
    Prepared out;
    out.tag = release.tag;
    auto report = [&](std::string const& text, int percent) { if (progress) progress(text,percent); };
    auto fail = [&](std::string text) {
        out.error = std::move(text);
        if (!out.directory.empty()) {
            std::error_code ec;
            fs::remove_all(out.directory,ec);  // only our freshly-created GUID directory
        }
        out.installer.clear();
        return out;
    };
    auto cancelled = [&] { return cancel && cancel->load(); };
    try {
        auto const version = PackageVersion(release.tag);
        if (release.prerelease || version.empty() || !release.package) return fail("This release has no supported update package.");
        auto const& asset = *release.package;
        if (asset.size == 0 || asset.size > 1073741824ull) return fail("Invalid update package size.");
        if (asset.sha256.empty() && !release.checksums) return fail("This release has no SHA-256 checksum for its update package.");
        if (cancelled()) return fail("Update cancelled.");
        std::string hash = asset.sha256;
        if (hash.empty()) {
            report("Checking the release checksum",0);
            auto const& sums = *release.checksums;
            auto const response = http::Get(sums.url,65536);
            if (response.status != 200 || !response.error.empty() || response.body.size() != sums.size ||
                (!sums.sha256.empty() && Sha256(response.body) != sums.sha256)) return fail("Couldn't verify the release checksum file.");
            hash = PackageChecksum(response.body,asset.name);
            if (hash.empty()) return fail("The release checksum file has no unique SHA-256 for this package.");
        }
        if (cancelled()) return fail("Update cancelled.");
        GUID id{};
        wchar_t guid[40]{};
        if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id,guid,40) == 0) return fail("Couldn't create an update workspace.");
        fs::path const local = LocalDataDir();
        if (local.empty()) return fail("Couldn't find the update cache folder.");
        fs::path const directory = local / L"updates" / guid;
        fs::create_directories(directory.parent_path());
        if (!fs::create_directory(directory)) return fail("Couldn't create a fresh update workspace.");
        out.directory = directory;
        fs::path const archive = directory / L"release.zip";
        report("Downloading " + release.tag,0);
        auto error = http::Download(asset.url,archive,[&](http::Progress const& p) {
            report("Downloading " + release.tag + " (" + http::SizeText(p.received) + " / " + http::SizeText(asset.size) + ")",
                static_cast<int>(std::min<std::uint64_t>(80,p.received * 80 / asset.size)));
        },cancel,{asset.size,hash});
        if (!error.empty()) return fail(error);
        if (cancelled()) return fail("Update cancelled.");
        report("Unpacking the verified update",85);
        fs::path const unpack = directory / L"package";
        fs::create_directory(unpack);
        fs::path const script = directory / L"extract.ps1";
        if (!WriteFileAtomic(script,kExtract)) return fail("Couldn't prepare archive extraction.");
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system,MAX_PATH)) return fail("Couldn't locate Windows PowerShell.");
        fs::path const powershell = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
        std::wstring const command = packs::QuoteArg(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
            packs::QuoteArg(script.wstring()) + L" -Archive " + packs::QuoteArg(archive.wstring()) + L" -Destination " + packs::QuoteArg(unpack.wstring());
        std::string detail;
        int const code = packs::RunHidden(command,directory,[&](std::string_view line) { detail = line; },cancel);
        if (cancelled()) return fail("Update cancelled.");
        if (code != 0) return fail("Couldn't unpack the update: " + detail);
        out.installer = unpack / L"MEVR-Setup.exe";
        Payload const payload{unpack / L"payload"};
        if (!payload.Valid() || !MatchesVersion(out.installer,version) || !MatchesVersion(payload.Asi(),version) ||
            !MatchesVersion(payload.Loader(),version)) return fail("The update is incomplete or its binaries don't match the release version.");
        for (auto const* name : {L"setup.rml",L"setup.rcss",L"main.rml",L"main.rcss"}) {
            if (!fs::is_regular_file(payload.Menu() / name)) return fail("The update is missing its menu or installer interface.");
        }
        // Avoid loading a package-local UI DLL from an unexpected location during the installer handoff.
        for (auto const& file : fs::directory_iterator(unpack)) {
            if (file.is_regular_file() && _wcsicmp(file.path().extension().c_str(),L".dll") == 0) return fail("Unexpected DLL beside the update installer.");
        }
        std::error_code ec;
        fs::remove(archive,ec);
        fs::remove(script,ec);
        report("Update downloaded and verified",100);
        return out;
    } catch (std::exception const& e) {
        return fail("Couldn't prepare the update: " + std::string(e.what()));
    }
}

Outcome Apply(Layout const& layout, Payload const& payload, fs::path const& installer, Progress const& progress, std::atomic<bool> const* cancel) {
    auto error = [](std::string text) { Outcome out; out.errors.push_back(text); out.log.push_back("error: " + text); return out; };
    if (!detect::IsGameRoot(layout.root)) return error("Choose the installed game's folder before updating.");
    if (!payload.Valid() || !MatchesVersion(installer,kVersion) || !MatchesVersion(payload.Asi(),kVersion) || !MatchesVersion(payload.Loader(),kVersion)) {
        return error("The downloaded installer and payload versions don't match.");
    }
    Transaction tx(layout);
    if (!tx.Load() || !tx.Exists() || tx.Data().modVersion.empty()) return error("Install MEVR first before applying an update.");
    if (CompareVersions(tx.Data().modVersion,kVersion) > 0) return error("A newer MEVR version is already installed; update cancelled.");
    if (!packs::WaitForGameExit(layout.root,30 * 60 * 1000,cancel,[&] {
        if (progress) progress("Waiting for Mass Effect to close. Your settings will be kept.",0);
    }) || (cancel && cancel->load())) return error("Update cancelled or Mass Effect is still running. Nothing was installed.");
    if (!tx.Load() || CompareVersions(tx.Data().modVersion,kVersion) > 0) return error("The installed version changed while waiting. Check for updates again.");
    // Install repeats its process/write checks immediately before changing any files.
    if (progress) progress("Installing the update and preserving your settings",90);
    InstallOptions options;
    options.setupExe = installer;
    options.preserveSettings = true;
    options.gameSettings = false;
    Outcome out = Install(layout,payload,options);
    if (out.ok) {
        out.log.push_back("Updated to " + std::string(kVersion) + ". Your game settings, VR profiles and calibration were kept.");
        if (progress) progress("Update complete. Your settings were kept.",100);
    }
    return out;
}

std::wstring HandoffArguments(fs::path const& game, unsigned long parentPid) {
    return L"--finish-update --game " + packs::QuoteArg(game.wstring()) + L" --wait-pid " + std::to_wstring(parentPid);
}
} // namespace mevr::install::updates
