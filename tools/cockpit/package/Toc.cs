// SPDX-License-Identifier: GPL-3.0-only
// One entry in or out of a copy of PCConsoleTOC.bin (research/47). Never writes over its input.
using System;
using System.IO;
using System.Linq;
using LegendaryExplorerCore.Unreal;

namespace Mevr.CockpitPackage;

internal static class Toc
{
    private static readonly uint[] CrcTable = MakeTable();

    private static uint[] MakeTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; ++i)
        {
            uint crc = i << 24;
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc & 0x80000000) != 0 ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
            }
            table[i] = crc;
        }
        return table;
    }

    // The engine's hash of a file's bare name (LEC's TOCCreator): each UTF-16 code unit of the upper-cased name, low byte then a zero.
    public static uint Hash(string fileName)
    {
        uint hash = 0;
        foreach (char c in fileName.ToUpperInvariant())
        {
            hash = (hash >> 8) ^ CrcTable[(hash ^ (byte)c) & 0xFF];
            hash = (hash >> 8) ^ CrcTable[hash & 0xFF];
        }
        return hash;
    }

    private static int WrongBuckets(TOCBinFile toc)
    {
        int wrong = 0;
        for (int i = 0; i < toc.HashBuckets.Count; ++i)
        {
            wrong += toc.HashBuckets[i].TOCEntries.Count(e => Hash(Path.GetFileName(e.name)) % (uint)toc.HashBuckets.Count != (uint)i);
        }
        return wrong;
    }

    public static int Run(Options options)
    {
        string input = options.Get("toc");
        if (input == null || !File.Exists(input))
        {
            Console.Error.WriteLine($"--toc: no file '{input}'");
            return 2;
        }
        var toc = new TOCBinFile(input);
        int entries = toc.GetAllEntries().Count;
        int wrong = WrongBuckets(toc);
        Console.WriteLine($"{input}: {entries} entries in {toc.HashBuckets.Count} buckets; {wrong} entries not in the bucket our hash gives");
        if (wrong != 0)
        {
            Console.Error.WriteLine("This hash doesn't describe this TOC: refusing to edit it.");
            return 1;
        }
        if (options.Has("list"))
        {
            string text = options.Get("list");
            foreach (TOCBinFile.Entry entry in toc.GetAllEntries().Where(e => e.name.Contains(text, StringComparison.OrdinalIgnoreCase)).Take(50))
            {
                Console.WriteLine($"  {entry.name}: {entry.size} bytes, flags 0x{entry.flags:X}");
            }
            return 0;
        }
        string output = options.Get("out");
        if (output == null || Path.GetFullPath(output).Equals(Path.GetFullPath(input), StringComparison.OrdinalIgnoreCase))
        {
            Console.Error.WriteLine("--out: a new file, not the TOC it reads.");
            return 2;
        }
        if (options.Has("add"))
        {
            var file = new FileInfo(options.Get("add"));
            string relative = options.Get("as") ?? $@"BIOGame\CookedPCConsole\{file.Name}";
            RemoveNamed(toc, file.Name);
            var bucket = toc.HashBuckets[(int)(Hash(file.Name) % (uint)toc.HashBuckets.Count)];
            bucket.TOCEntries.Add(new TOCBinFile.Entry { flags = 0, size = (int)file.Length, name = relative });
            Console.WriteLine($"Added {relative}, {file.Length} bytes");
        }
        else if (options.Has("remove"))
        {
            Console.WriteLine($"Removed {RemoveNamed(toc, options.Get("remove"))} entries named {options.Get("remove")}");
        }
        else
        {
            Console.Error.WriteLine("--add or --remove");
            return 2;
        }
        using (MemoryStream saved = toc.Save())
        {
            File.WriteAllBytes(output, saved.ToArray());
        }
        // Read back what the engine would read.
        var back = new TOCBinFile(output);
        Console.WriteLine($"Wrote {output}: {back.GetAllEntries().Count} entries, {WrongBuckets(back)} in a wrong bucket");
        return WrongBuckets(back) == 0 ? 0 : 1;
    }

    private static int RemoveNamed(TOCBinFile toc, string fileName)
    {
        int removed = 0;
        foreach (TOCBinFile.TOCHashTableEntry bucket in toc.HashBuckets)
        {
            removed += bucket.TOCEntries.RemoveAll(e => Path.GetFileName(e.name).Equals(fileName, StringComparison.OrdinalIgnoreCase));
        }
        return removed;
    }
}
