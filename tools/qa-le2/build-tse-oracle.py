# SPDX-License-Identifier: MIT
"""Stage a headless independent decoder from pinned Trilogy Save Editor source.

This generates a private Cargo project; it does not vendor upstream into MEVR.
The save models, binary serializer/deserializer and rcize macro are copied
unchanged. Only UI derives are stubbed. Build/run instructions live in README.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PIN = "6451f23d1fba62358328dce4e87733970f36ebf1"

CARGO = '''[package]
name = "mevr-tse-oracle"
version = "0.1.0"
edition = "2021"
license = "CECILL-2.1"
[workspace]
[dependencies]
macros = { path = "macros" }
anyhow = "1.0"
derive_more = { version = "0.99", features = ["deref", "deref_mut", "display", "from"], default-features = false }
bitvec = { version = "1.0", features = ["std"], default-features = false }
indexmap = { version = "=1.7", features = ["std"], default-features = false }
encoding_rs = "0.8"
crc = "2.0"
flate2 = { version = "1.0", features = ["rust_backend"], default-features = false }
zip = { version = "0.5", features = ["deflate"], default-features = false }
uuid = "0.8"
ryu = "1.0"
base64 = "0.13"
serde = { version = "1.0", features = ["derive"], default-features = false }
serde_json = "1.0"
ron = { version = "0.7", features = ["indexmap"], default-features = false }
'''

MACROS = '''// SPDX-License-Identifier: MIT
mod rcize;
use proc_macro::TokenStream;
#[proc_macro_attribute]
pub fn rcize_fields(_: TokenStream, input: TokenStream) -> TokenStream {
    rcize::rcize_fields(input)
}
#[proc_macro_derive(RawUi)]
pub fn raw_ui(_: TokenStream) -> TokenStream { TokenStream::new() }
#[proc_macro_derive(RawUiRoot)]
pub fn raw_ui_root(_: TokenStream) -> TokenStream { TokenStream::new() }
#[proc_macro_derive(RawUiChildren)]
pub fn raw_ui_children(_: TokenStream) -> TokenStream { TokenStream::new() }
'''

MAIN = '''// SPDX-License-Identifier: MIT
#![allow(dead_code)]
#[macro_use] extern crate derive_more;
#[macro_use] extern crate macros;
mod save_data;
mod unreal;
use anyhow::{bail, Context, Result};
use crc::{Crc, CRC_32_BZIP2};
use std::{fs, io::Write};
fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 { bail!("usage: mevr-tse-oracle INPUT_SAVE OUTPUT_JSON"); }
    let input = fs::read(&args[1])?;
    if input.len() < 8 { bail!("truncated save"); }
    let body_len = input.len() - 4;
    let stored_crc = u32::from_le_bytes(input[body_len..].try_into()?);
    let crc = Crc::<u32>::new(&CRC_32_BZIP2);
    if crc.checksum(&input[..body_len]) != stored_crc { bail!("invalid checksum"); }
    let save: save_data::mass_effect_2::Me2LeSaveGame =
        unreal::Deserializer::from_bytes(&input[..body_len])?;
    let mut serialized = unreal::Serializer::to_vec(&save)?;
    serialized.extend(crc.checksum(&serialized).to_le_bytes());
    if input != serialized { bail!("TSE independent reserialization differs from input"); }
    let report = serde_json::json!({
        "schema_version": 1,
        "upstream_revision": "6451f23d1fba62358328dce4e87733970f36ebf1",
        "method": "TSE source decoder and serializer; UI derives omitted",
        "crc_valid": true,
        "reserialized_identically": true,
        "data": save
    });
    let mut output = fs::OpenOptions::new().write(true).create_new(true)
        .open(&args[2]).context("output must not already exist")?;
    output.write_all(&serde_json::to_vec_pretty(&report)?)?;
    output.sync_all()?;
    println!("TSE decoded and independently reserialized {} bytes", input.len());
    Ok(())
}
'''


def stage(source: Path, output: Path) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    head = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if head != PIN:
        raise ValueError(f"upstream revision must be {PIN}, got {head}")
    if output == source or source in output.parents:
        raise ValueError("output must be outside the upstream checkout")
    # Read immutable git blobs, not the working tree (which may hide changes
    # through assume-unchanged flags or contain ignored extra modules).
    listing = subprocess.check_output(
        ["git", "-C", str(source), "ls-tree", "-r", PIN, "--", "src/save_data",
         "src/unreal", "macros/Cargo.toml", "macros/src/rcize.rs", "LICENSE.txt",
         "LICENSE_FRENCH.txt"], text=True)
    output.mkdir(parents=True, exist_ok=False)
    (output / "src").mkdir()
    (output / "macros/src").mkdir(parents=True)
    blobs = {}
    for line in listing.splitlines():
        metadata, name = line.split("\t", 1)
        mode, kind, oid = metadata.split()
        if kind != "blob" or mode != "100644" or ".." in Path(name).parts:
            raise ValueError(f"Unexpected upstream entry: {line}")
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(subprocess.check_output(
            ["git", "-C", str(source), "cat-file", "blob", oid]))
        blobs[name] = oid
    lock = Path(__file__).with_name("tse-oracle.Cargo.lock")
    (output / "Cargo.lock").write_bytes(lock.read_bytes())
    for name, content in (("Cargo.toml", CARGO), ("src/main.rs", MAIN),
                          ("macros/src/lib.rs", MACROS)):
        (output / name).write_text(content, encoding="utf-8")
    hashes = {
        str(p.relative_to(output)).replace("\\", "/"):
        hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(output.rglob("*")) if p.is_file()
    }
    manifest = {"schema_version": 1, "upstream_revision": PIN,
                "upstream_url": "https://github.com/KarlitosVII/trilogy-save-editor",
                "upstream_license": "CECILL-2.1", "files": hashes,
                "upstream_git_blobs": blobs,
                "changes": ["UI derive implementations replaced with no-ops",
                            "Headless main and Cargo manifest added",
                            "Save models, rcize and binary codecs unchanged"]}
    (output / "source-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = stage(args.source, args.output)
    print(json.dumps({"revision": result["upstream_revision"],
                      "files": len(result["files"])}))
