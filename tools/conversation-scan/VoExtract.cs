// SPDX-License-Identifier: GPL-3.0-only
// Lines' recordings out of the game's sound banks as WAV files, for Thespian mode's bench (research/70): the recognizer is
// run over Shepard's own takes of the lines it will have to recognize from a player.
//   conversation-scan <ISACT folder> <out folder> --vo-extract <list.tsv>
// <ISACT folder> is BioGame/Content/Packages/ISACT (one <conversation>.isb per conversation: the English bank; the other
// languages' end in _de, _fr, _it, _plpc, _ra...). <list.tsv> has one row per recording wanted:
//   banks (comma separated, without .isb) <TAB> string id <TAB> M or F <TAB> anything more (copied to the manifest)
// A sample's title in a bank is "EN_<conversation>_<string id, 8 digits>_M.wav" or "..._F.wav": every line has an _M take
// (for Shepard's lines the male player's voice); a line the female player hears or says differently has an _F take too
// (for Shepard's lines the female player's voice). Each is Ogg Vorbis, mono, 44.1 or 48 kHz; it is decoded with NAudio.Vorbis (in Legendary Explorer's folder)
// and written as 16-bit PCM at its own rate to <out folder>/<string id>_<M|F>.wav. <out folder>/manifest.tsv lists
//   wav file <TAB> seconds <TAB> the row's fields
// Reads only. The WAVs are game audio: keep them out of the repository (scratch/ is ignored).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using LegendaryExplorerCore.Sound.ISACT;
using NAudio.Vorbis;

namespace Mevr.ConversationScan;

internal static class VoExtract
{
    public static int Run(string isbDir, string outDir, string listPath)
    {
        Directory.CreateDirectory(outDir);
        var wanted = File.ReadAllLines(listPath).Where(l => l.Trim().Length > 0).Select(l => l.Split('\t')).Where(f => f.Length >= 3).ToList();
        // Every bank a row names, opened once: title -> sample bytes.
        var banks = new Dictionary<string, Dictionary<string, byte[]>>(StringComparer.OrdinalIgnoreCase);
        int written = 0, missing = 0, failed = 0;
        using var manifest = new StreamWriter(Path.Combine(outDir, "manifest.tsv"));
        foreach (string[] row in wanted)
        {
            string id = row[1].Trim().PadLeft(8, '0');
            bool male = row[2].Trim().Equals("M", StringComparison.OrdinalIgnoreCase);
            string suffix = "_" + id + (male ? "_M.wav" : "_F.wav");
            byte[] data = null;
            foreach (string bankName in row[0].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                if (!banks.TryGetValue(bankName, out var samples))
                {
                    samples = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
                    string path = Path.Combine(isbDir, bankName + ".isb");
                    if (File.Exists(path))
                    {
                        try
                        {
                            using FileStream stream = File.OpenRead(path);
                            foreach (ISACTListBankChunk chunk in new ISACTBank(stream).GetAllBankChunks().OfType<ISACTListBankChunk>())
                            {
                                if (chunk.SampleData != null && chunk.TitleInfo?.Value != null)
                                {
                                    samples[chunk.TitleInfo.Value] = chunk.SampleData;
                                }
                            }
                        }
                        catch (Exception e)
                        {
                            Console.Error.WriteLine($"{bankName}.isb: {e.GetType().Name}: {e.Message}");
                        }
                    }
                    banks[bankName] = samples;
                }
                data = samples.FirstOrDefault(s => s.Key.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)).Value;
                if (data != null)
                {
                    break;
                }
            }
            if (data == null)
            {
                ++missing;
                continue;
            }
            try
            {
                string name = $"{row[1].Trim()}_{(male ? "M" : "F")}.wav";
                double seconds = WriteWav(data, Path.Combine(outDir, name));
                manifest.WriteLine($"{name}\t{seconds:F2}\t{string.Join('\t', row)}");
                ++written;
            }
            catch (Exception e)
            {
                ++failed;
                Console.Error.WriteLine($"{row[1]} ({row[0]}): {e.GetType().Name}: {e.Message}");
            }
        }
        Console.WriteLine($"{written} recordings -> {outDir} ({missing} not in their banks, {failed} failed to decode), {banks.Count} banks opened");
        return 0;
    }

    // Decodes an Ogg Vorbis stream and writes it as 16-bit PCM mono at its own rate. Returns its length in seconds.
    private static double WriteWav(byte[] ogg, string path)
    {
        using var reader = new VorbisWaveReader(new MemoryStream(ogg), true);
        int channels = reader.WaveFormat.Channels;
        int rate = reader.WaveFormat.SampleRate;
        var mono = new List<short>();
        float[] buffer = new float[rate * channels];
        int read;
        while ((read = reader.Read(buffer, 0, buffer.Length)) > 0)
        {
            for (int i = 0; i + channels <= read; i += channels)
            {
                float sum = 0;
                for (int c = 0; c < channels; ++c)
                {
                    sum += buffer[i + c];
                }
                mono.Add((short)Math.Clamp(sum / channels * 32767f, -32768f, 32767f));
            }
        }
        using var w = new BinaryWriter(File.Create(path));
        int bytes = mono.Count * 2;
        w.Write("RIFF"u8);
        w.Write(36 + bytes);
        w.Write("WAVEfmt "u8);
        w.Write(16);
        w.Write((short)1);
        w.Write((short)1);
        w.Write(rate);
        w.Write(rate * 2);
        w.Write((short)2);
        w.Write((short)16);
        w.Write("data"u8);
        w.Write(bytes);
        foreach (short s in mono)
        {
            w.Write(s);
        }
        return (double)mono.Count / rate;
    }
}
