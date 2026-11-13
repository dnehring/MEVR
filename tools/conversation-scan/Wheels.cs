// SPDX-License-Identifier: GPL-3.0-only
// Every conversation's lines as text, for Thespian mode's phase 0 (docs/thespian-mode-plan.md, research/70): one JSON line per
// BioConversation of the English packages with
//   entries   the other people's lines (and the player's unchosen ones): speaker, text, and the replies each offers on the
//             wheel (the reply's index, the wheel's label "paraphrase" and its category: where on the wheel it sits)
//   replies   the player's lines: the full text Shepard says, the reply type (REPLY_STANDARD is picked on the wheel,
//             REPLY_AUTOCONTINUE goes on by itself), the GUI style (charm, intimidate...), unskippable, where it leads
//   waves     each line's recording: its length in seconds, keyed "<string id>_M" (every line has one; for Shepard's
//             lines the male player's voice) and "<string id>_F" (the female player's take, where there is one)
//   banks     the sound banks the recordings stream from (BioGame/Content/Packages/ISACT/<bank>.isb)
// The text comes from the English talk files of every package (a conversation's strings are not always in the package that
// holds it): "tlk" (the female player's) and "tlk_M" (the male player's); a line is given once, with "textM" only where
// the male file's differs.
//   conversation-scan <CookedPCConsole> <out.jsonl> --wheels [--only <file text>]
// With --isb <ISACT folder> it also lists, per bank, each sample's title, format, rate, channels, length and size (kind "bank").
// Reads only. Its output is game text: keep it out of the repository (scratch/ is ignored).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.TLK.ME1;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class Wheels
{
    public static int Run(string cooked, string outPath, string only, string isbDir)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        var everyFile = Directory.GetFiles(cooked, "*_LOC_INT.pcc").OrderBy(f => f).ToList();
        var files = everyFile.AsEnumerable();
        if (only != null)
        {
            files = files.Where(f => Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase));
        }
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int packages = 0, conversations = 0, failed = 0, unresolved = 0, lines = 0;
        // Pass 1: every English talk file ("tlk" the female player's, "tlk_M" the male player's; the other languages' are
        // named tlk_FR, tlk_M_FR...) and every recording's length, from every package: a conversation's strings are not
        // always in the package that holds the conversation (6,723 of 11,238 reply lines had no text read per package).
        var female = new Dictionary<int, string>();
        var male = new Dictionary<int, string>();
        var allWaves = new Dictionary<string, double>();   // "159867" or "159867_M" -> seconds
        var bankOf = new Dictionary<string, string>();     // the same key -> the ISACT bank's name
        var all = files.ToList();
        string startup = Path.Combine(cooked, "Startup_INT.pcc");
        foreach (string file in File.Exists(startup) ? everyFile.Append(startup) : everyFile)
        {
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                foreach (ExportEntry tlk in pcc.Exports.Where(e => e.ClassName == "BioTlkFile"))
                {
                    string name = tlk.ObjectName.Name;
                    bool isMale = name == "tlk_M" || name.EndsWith("_tlk_M");
                    if (!isMale && name != "tlk" && !name.EndsWith("_tlk"))
                    {
                        continue;
                    }
                    var into = isMale ? male : female;
                    foreach (var s in new ME1TalkFile(tlk).StringRefs)
                    {
                        if (s.Data != null)
                        {
                            into[s.StringID] = s.Data;
                            // A talk file stores some ids only as "the one before, plus one": LEC computes those into
                            // CalculatedID, and the conversations' nodes refer to them by it.
                            if (s.CalculatedID != s.StringID && s.CalculatedID > 0)
                            {
                                into.TryAdd(s.CalculatedID, s.Data);
                            }
                        }
                    }
                }
                // A line's recording is a SoundNodeWave named "<conversation>:VO_<string id>_M" (every line; for Shepard's
                // lines the male player's voice) or "...:VO_<string id>_F" (the female player's take, where there is one).
                foreach (ExportEntry wave in pcc.Exports.Where(e => e.ClassName == "SoundNodeWave" && e.ObjectName.Name.Contains("VO_")))
                {
                    PropertyCollection wp = wave.GetProperties();
                    string objectName = wave.ObjectName.Name;
                    string key = objectName.Substring(objectName.LastIndexOf("VO_", StringComparison.Ordinal) + 3);
                    allWaves[key] = Math.Round(wp.GetProp<FloatProperty>("Duration")?.Value ?? 0f, 3);
                    string bankName = (pcc.GetEntry(wp.GetProp<ObjectProperty>("BioStreamingData")?.Value ?? 0))?.ObjectName.Name;
                    if (bankName != null)
                    {
                        bankOf[key] = bankName;
                    }
                }
            }
            catch (Exception e)
            {
                Console.Error.WriteLine($"{Path.GetFileName(file)} (pass 1): {e.GetType().Name}: {e.Message}");
            }
        }
        Console.Error.WriteLine($"pass 1: {female.Count} strings (female player), {male.Count} (male player), {allWaves.Count} recordings");
        using var writer = new StreamWriter(outPath);
        foreach (string file in all)
        {
            string package = Path.GetFileNameWithoutExtension(file);
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                ++packages;
                foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "BioConversation"))
                {
                    if (!seen.Add(export.InstancedFullPath))
                    {
                        continue;
                    }
                    PropertyCollection props = export.GetProperties();
                    var entryList = props.GetProp<ArrayProperty<StructProperty>>("m_EntryList") ?? new ArrayProperty<StructProperty>("m_EntryList");
                    var replyList = props.GetProp<ArrayProperty<StructProperty>>("m_ReplyList") ?? new ArrayProperty<StructProperty>("m_ReplyList");
                    var waves = new SortedDictionary<string, object>();
                    var banks = new SortedSet<string>();
                    void Text(Dictionary<string, object> into, string key, int id)
                    {
                        into[key + "Ref"] = id;
                        if (id <= 0)
                        {
                            return;
                        }
                        foreach (string waveKey in new[] { id + "_M", id + "_F" })
                        {
                            if (allWaves.TryGetValue(waveKey, out double seconds))
                            {
                                waves[waveKey] = seconds;
                                if (bankOf.TryGetValue(waveKey, out string b))
                                {
                                    banks.Add(b);
                                }
                            }
                        }
                        bool hasF = female.TryGetValue(id, out string f);
                        bool hasM = male.TryGetValue(id, out string m);
                        if (!hasF && !hasM)
                        {
                            ++unresolved;
                            return;
                        }
                        ++lines;
                        into[key] = hasF ? f : m;
                        if (hasF && hasM && f != m)
                        {
                            into[key + "M"] = m;
                        }
                    }
                    var entries = new List<Dictionary<string, object>>();
                    foreach (StructProperty node in entryList)
                    {
                        var e = new Dictionary<string, object>
                        {
                            // -1 the conversation's owner, -2 the player, 0.. m_SpeakerList.
                            ["speaker"] = node.GetProp<IntProperty>("nSpeakerIndex")?.Value ?? -1,
                            ["nonText"] = node.GetProp<BoolProperty>("bNonTextLine")?.Value == true,
                            ["ambient"] = node.GetProp<BoolProperty>("bAmbient")?.Value == true,
                            ["skippable"] = node.GetProp<BoolProperty>("bSkippable")?.Value != false,
                            ["offers"] = (node.GetProp<ArrayProperty<StructProperty>>("ReplyListNew") ?? new ArrayProperty<StructProperty>("ReplyListNew"))
                                .Select(r =>
                                {
                                    var o = new Dictionary<string, object>
                                    {
                                        ["reply"] = r.GetProp<IntProperty>("nIndex")?.Value ?? -1,
                                        ["category"] = r.GetProp<EnumProperty>("Category")?.Value.Instanced ?? "REPLY_CATEGORY_DEFAULT",
                                    };
                                    Text(o, "label", r.GetProp<StringRefProperty>("srParaphrase")?.Value ?? r.GetProp<IntProperty>("srParaphrase")?.Value ?? 0);
                                    return o;
                                }).ToList(),
                        };
                        Text(e, "text", node.GetProp<StringRefProperty>("srText")?.Value ?? node.GetProp<IntProperty>("srText")?.Value ?? 0);
                        entries.Add(e);
                    }
                    var replies = new List<Dictionary<string, object>>();
                    foreach (StructProperty node in replyList)
                    {
                        var r = new Dictionary<string, object>
                        {
                            ["type"] = node.GetProp<EnumProperty>("ReplyType")?.Value.Instanced ?? "REPLY_STANDARD",
                            ["style"] = node.GetProp<EnumProperty>("eGUIStyle")?.Value.Instanced ?? "GUI_STYLE_NONE",
                            ["nonText"] = node.GetProp<BoolProperty>("bNonTextLine")?.Value == true,
                            ["ambient"] = node.GetProp<BoolProperty>("bAmbient")?.Value == true,
                            ["unskippable"] = node.GetProp<BoolProperty>("bUnskippable")?.Value == true,
                            ["transition"] = node.GetProp<IntProperty>("nStateTransition")?.Value ?? -1,
                            ["script"] = node.GetProp<IntProperty>("nScriptIndex")?.Value ?? -1,
                            ["leadsTo"] = node.GetProp<ArrayProperty<IntProperty>>("EntryList")?.Select(x => x.Value).ToList() ?? new List<int>(),
                        };
                        Text(r, "text", node.GetProp<StringRefProperty>("srText")?.Value ?? node.GetProp<IntProperty>("srText")?.Value ?? 0);
                        replies.Add(r);
                    }
                    writer.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object>
                    {
                        ["kind"] = "lines",
                        ["name"] = export.InstancedFullPath,
                        ["package"] = package,
                        ["banks"] = banks.ToList(),
                        ["starting"] = props.GetProp<ArrayProperty<IntProperty>>("m_StartingList")?.Select(x => x.Value).ToList() ?? new List<int>(),
                        ["speakers"] = props.GetProp<ArrayProperty<StructProperty>>("m_SpeakerList")?.Select(s => s.GetProp<NameProperty>("sSpeakerTag")?.Value.Instanced ?? "?").ToList() ?? new List<string>(),
                        ["entries"] = entries,
                        ["replies"] = replies,
                        ["waves"] = waves,
                    }));
                    ++conversations;
                }
            }
            catch (Exception e)
            {
                ++failed;
                Console.Error.WriteLine($"{package}: {e.GetType().Name}: {e.Message}");
            }
        }
        if (isbDir != null)
        {
            foreach (string isb in Directory.GetFiles(isbDir, "*.isb").OrderBy(f => f))
            {
                string name = Path.GetFileNameWithoutExtension(isb);
                // The other languages' banks end in _de, _fr, _it, _plpc, _ra...; the English one has the conversation's name.
                if (only != null && !name.Contains(only, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                try
                {
                    using FileStream stream = File.OpenRead(isb);
                    var bank = new LegendaryExplorerCore.Sound.ISACT.ISACTBank(stream);
                    writer.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object>
                    {
                        ["kind"] = "bank",
                        ["bank"] = name,
                        ["samples"] = bank.GetAllBankChunks().OfType<LegendaryExplorerCore.Sound.ISACT.ISACTListBankChunk>()
                            .Where(c => c.SampleData != null && c.TitleInfo != null)
                            .Select(c => new Dictionary<string, object>
                            {
                                ["title"] = c.TitleInfo.Value, ["format"] = c.CompressionInfo?.CurrentFormat.ToString() ?? "?",
                                ["rate"] = c.SampleInfo?.SamplesPerSecond ?? 0, ["channels"] = c.ChannelCount,
                                ["ms"] = c.SampleInfo?.TimeLength ?? 0, ["bytes"] = c.SampleData.Length,
                            }).ToList(),
                    }));
                }
                catch (Exception e)
                {
                    Console.Error.WriteLine($"{name}.isb: {e.GetType().Name}: {e.Message}");
                }
            }
        }
        Console.WriteLine($"{packages} packages read ({failed} failed), {conversations} conversations, {lines} texts, {unresolved} string ids the talk files hold no text for (pass-through replies, unlabelled offers) -> {outPath}");
        return 0;
    }
}
