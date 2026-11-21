// SPDX-License-Identifier: MIT
// The hands census (research/74): every scripted scene of a game's packages (each SeqAct_Interp, level or conversation, and
// each InterpData no SeqAct_Interp plays), with the facts a rule needs to say whether Shepard's hands act in it:
//   scene   the matinee: package, sequence, its InterpData and length; each group's class, name and what the SeqAct_Interp binds
//           to it (the player variable, an object and its class, tag and mesh, a find-by-tag); each track's class and what it
//           plays (animation keys with their sequence names, resolved lengths and windows; ME2/ME3 gesture keys; the actor a
//           BioEvtSysTrack* finds; move spans; director cuts; fades; anything named for a prop, an attachment or a weapon,
//           property by property); the Kismet around it (the event that leads to it, what its outputs do, the attach and prop
//           actions beside it in its sequence)
//   use     a "use" event (SeqEvent_Used, BioSeqEvt_OnPlayerActivate and kin): its originator's class, name and mesh and the
//           matinees it reaches within a few actions (context: a use that plays no matinee on Shepard is not a candidate)
//   classes per game, how many exports of each sequence/interp class the packages hold (to find each game's prop and attach
//           classes rather than assume them)
// tools/hands-census.py classifies. `conversation-scan <BioGame> <out.jsonl> --hands-census [--game LE1|LE2|LE3] [--only <text>]
// [--threads n]`: <BioGame> is the game's BioGame folder (CookedPCConsole and DLC\*\CookedPCConsole are read) or one
// CookedPCConsole. Reads only; its output is game data (names and object paths): keep it out of the repository
// (scratch/hands-census/).
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class HandsCensus
{
    // Track and sequence-object classes whose properties are dumped whole: props, attachments, weapons, meshes.
    static readonly Regex PropLike = new(@"Attach|Prop|Weapon|Equip|Holster|Socket|Mesh|Hand|Item|Pickup|Grab|Drink",
        RegexOptions.IgnoreCase);
    // A matinee's property tracks (a camera's FOV, a light's colour): named "Prop" for a property, not a prop.
    static readonly Regex PropertyTrack = new(@"(Float|Vector|Color|LinearColor|Bool|Byte|Int|Enum|Material)Prop", RegexOptions.IgnoreCase);
    static readonly Regex UseEvent = new(@"^(SeqEvent_Used|BioSeqEvt_OnPlayerActivate|SFXSeqEvt_OnPlayerActivate|SeqEvent_Use\w*|\w*Evt_\w*Use\w*)$");

    // A key's time or a mesh's bound can be infinite: written as Infinity, which Python's json reads.
    static readonly JsonSerializerOptions Json = new() { NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals };

    public static int Run(string root, string outPath, string only, int threads)
    {
        InspectionContext.Initialize();
        try
        {
            // A game may be running (another lane's): read at a low priority.
            Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.BelowNormal;
        }
        catch
        {
        }
        var files = Files(root).Where(f => only == null || Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase)).ToArray();
        var classes = new ConcurrentDictionary<string, int>();
        int packages = 0, failed = 0, scenes = 0, uses = 0;
        var started = Stopwatch.StartNew();
        using var writer = new StreamWriter(outPath);
        object gate = new();
        Parallel.ForEach(files, new ParallelOptions { MaxDegreeOfParallelism = Math.Max(1, threads) }, file =>
        {
            var lines = new List<string>();
            string package = Path.GetFileNameWithoutExtension(file);
            string relative = Relative(root, file);
            try
            {
                using IMEPackage pcc = InspectionContext.Open(file);
                var census = new PackageCensus(pcc, package, relative);
                foreach (ExportEntry export in pcc.Exports)
                {
                    if (export.IsDefaultObject || export.InstancedFullPath.Contains("Default__"))
                    {
                        continue;
                    }
                    string cls = export.ClassName;
                    if (cls.Contains("Seq") || cls.Contains("Interp") || cls.Contains("EvtSys") || cls.Contains("Track"))
                    {
                        classes.AddOrUpdate(cls, 1, (_, n) => n + 1);
                    }
                    Dictionary<string, object> record = null;
                    try
                    {
                        if (cls == "SeqAct_Interp" || cls.EndsWith("SeqAct_Interp", StringComparison.Ordinal))
                        {
                            record = census.Scene(export);
                        }
                        else if (cls == "InterpData" && !census.PlayedData(export))
                        {
                            record = census.OrphanData(export);
                        }
                        else if (UseEvent.IsMatch(cls))
                        {
                            record = census.Use(export);
                        }
                        else if (cls == "BioConversation")
                        {
                            record = census.Conversation(export);
                        }
                    }
                    catch (Exception e)
                    {
                        record = new Dictionary<string, object> { ["kind"] = "error", ["path"] = export.InstancedFullPath, ["error"] = e.Message };
                    }
                    if (record != null)
                    {
                        record["game"] = Program.GameName;
                        record["package"] = package;
                        record["file"] = relative;
                        record["exportIndex"] = export.UIndex;
                        lines.Add(JsonSerializer.Serialize(record, Json));
                        if ((string)record["kind"] == "scene") Interlocked.Increment(ref scenes);
                        if ((string)record["kind"] == "use") Interlocked.Increment(ref uses);
                    }
                }
                Interlocked.Increment(ref packages);
            }
            catch (Exception e)
            {
                Interlocked.Increment(ref failed);
                Console.Error.WriteLine($"{package}: {e.GetType().Name}: {e.Message}");
            }
            lock (gate)
            {
                foreach (string line in lines) writer.WriteLine(line);
                if (packages % 250 == 0) Console.Error.WriteLine($"{packages}/{files.Length} packages, {scenes} scenes, {started.Elapsed.TotalSeconds:0} s");
            }
        });
        writer.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object>
        {
            ["kind"] = "classes", ["game"] = Program.GameName, ["packages"] = packages, ["failed"] = failed, ["files"] = files.Length,
            ["seconds"] = Math.Round(started.Elapsed.TotalSeconds, 1), ["root"] = Path.GetFileName(Path.GetFullPath(root).TrimEnd('\\', '/')),
            ["folders"] = files.Select(f => Path.GetDirectoryName(Relative(root, f))).Distinct().OrderBy(f => f).ToList(),
            ["counts"] = new SortedDictionary<string, int>(classes),
        }));
        Console.WriteLine($"{packages} packages read ({failed} failed), {scenes} scenes, {uses} uses in {started.Elapsed.TotalSeconds:0} s -> {outPath}");
        return failed > 0 && packages == 0 ? 1 : 0;
    }

    // The game's base CookedPCConsole and every DLC's, or the one folder given. The sound, art, lighting and texture packages
    // hold no scenes; of the localised ones only English (conversations' line matinees live there on ME2 and ME3).
    static IEnumerable<string> Files(string root)
    {
        var folders = new List<string>();
        if (Directory.Exists(Path.Combine(root, "CookedPCConsole")))
        {
            folders.Add(Path.Combine(root, "CookedPCConsole"));
            string dlc = Path.Combine(root, "DLC");
            if (Directory.Exists(dlc))
            {
                folders.AddRange(Directory.GetDirectories(dlc).Select(d => Path.Combine(d, "CookedPCConsole")).Where(Directory.Exists).OrderBy(d => d));
            }
        }
        else
        {
            folders.Add(root);
        }
        foreach (string folder in folders)
        {
            foreach (string file in Directory.GetFiles(folder, "*.pcc").OrderBy(f => f))
            {
                string upper = Path.GetFileNameWithoutExtension(file).ToUpperInvariant();
                if (upper.Contains("_LOC_") ? upper.EndsWith("_LOC_INT") :
                    !(upper.EndsWith("_SND") || upper.EndsWith("_ART") || upper.EndsWith("_LIGHT") || upper.StartsWith("TEXTURES")
                      || upper.EndsWith("_T") || upper.StartsWith("STARTUP_") && !upper.EndsWith("_INT")))
                {
                    yield return file;
                }
            }
        }
    }

    static string Relative(string root, string file) => Path.GetRelativePath(Path.GetFullPath(root), Path.GetFullPath(file));

    // One package's lookups: the matinees that play each InterpData, the variables by name, Kismet's links backwards.
    private sealed class PackageCensus
    {
        readonly IMEPackage pcc;
        readonly string package, file;
        readonly HashSet<int> playedData = new();
        readonly Dictionary<string, List<ExportEntry>> namedVariables = new(StringComparer.OrdinalIgnoreCase);
        Dictionary<int, List<(ExportEntry source, string output)>> reverse;
        readonly Dictionary<int, Dictionary<string, (float length, float rate)>> animSets = new();

        public PackageCensus(IMEPackage pcc, string package, string file)
        {
            this.pcc = pcc;
            this.package = package;
            this.file = file;
            foreach (ExportEntry e in pcc.Exports)
            {
                if (e.ClassName.EndsWith("SeqAct_Interp", StringComparison.Ordinal) && !e.IsDefaultObject)
                {
                    foreach (ExportEntry data in Linked(Props(e), "Data"))
                    {
                        playedData.Add(data.UIndex);
                    }
                }
                else if (e.ClassName.StartsWith("SeqVar", StringComparison.Ordinal) || e.ClassName.Contains("SeqVar_"))
                {
                    if (e.ClassName == "SeqVar_Named") continue;
                    PropertyCollection p = Props(e);
                    if (p?.GetProp<NameProperty>("VarName")?.Value.Instanced is { } name && name != "None")
                    {
                        if (!namedVariables.TryGetValue(name, out var list)) namedVariables[name] = list = new List<ExportEntry>();
                        list.Add(e);
                    }
                }
            }
        }

        public bool PlayedData(ExportEntry data) => playedData.Contains(data.UIndex);

        static PropertyCollection Props(ExportEntry e)
        {
            try
            {
                return e.GetProperties();
            }
            catch
            {
                return null;
            }
        }

        IEnumerable<ExportEntry> Linked(PropertyCollection props, string linkDesc)
        {
            foreach (StructProperty link in props?.GetProp<ArrayProperty<StructProperty>>("VariableLinks") ?? new ArrayProperty<StructProperty>("VariableLinks"))
            {
                if (linkDesc != null && !string.Equals(link.GetProp<StrProperty>("LinkDesc")?.Value, linkDesc, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                foreach (ObjectProperty v in link.GetProp<ArrayProperty<ObjectProperty>>("LinkedVariables") ?? new ArrayProperty<ObjectProperty>("LinkedVariables"))
                {
                    if (pcc.GetEntry(v.Value) is ExportEntry variable)
                    {
                        yield return variable;
                    }
                }
            }
        }

        static float[] Vector(StructProperty v) => v == null ? null :
            new[] { v.GetProp<FloatProperty>("X")?.Value ?? 0, v.GetProp<FloatProperty>("Y")?.Value ?? 0, v.GetProp<FloatProperty>("Z")?.Value ?? 0 };

        public Dictionary<string, object> Scene(ExportEntry interp)
        {
            PropertyCollection props = Props(interp);
            var record = new Dictionary<string, object>
            {
                ["kind"] = "scene",
                ["path"] = interp.InstancedFullPath,
                ["class"] = interp.ClassName,
                ["sequence"] = interp.Parent?.InstancedFullPath,
                ["inConversation"] = interp.InstancedFullPath.Split('.').Contains("Node_Data_Sequence"),
            };
            // A level matinee's designer comment ("Shep Drinking"); a conversation line's comment is its text: never read.
            if (!(bool)record["inConversation"]) record["comment"] = props?.GetProp<ArrayProperty<StrProperty>>("m_aObjComment")?.FirstOrDefault()?.Value;
            if (props?.GetProp<FloatProperty>("PlayRate") is { } rate) record["playRate"] = rate.Value;
            // What the SeqAct_Interp binds to each group: a variable link per group, named for it.
            var bindings = new Dictionary<string, List<object>>(StringComparer.OrdinalIgnoreCase);
            foreach (StructProperty link in props?.GetProp<ArrayProperty<StructProperty>>("VariableLinks") ?? new ArrayProperty<StructProperty>("VariableLinks"))
            {
                string desc = link.GetProp<StrProperty>("LinkDesc")?.Value ?? "";
                if (desc.Equals("Data", StringComparison.OrdinalIgnoreCase)) continue;
                var list = new List<object>();
                foreach (ObjectProperty v in link.GetProp<ArrayProperty<ObjectProperty>>("LinkedVariables") ?? new ArrayProperty<ObjectProperty>("LinkedVariables"))
                {
                    if (pcc.GetEntry(v.Value) is ExportEntry variable)
                    {
                        list.Add(Binding(variable, 0));
                    }
                }
                bindings[desc] = list;
            }
            ExportEntry data = Linked(props, "Data").FirstOrDefault(e => e.ClassName == "InterpData");
            FillData(record, data, bindings);
            record["upstream"] = Upstream(interp);
            record["downstream"] = Downstream(interp);
            record["siblings"] = Siblings(interp);
            return record;
        }

        public Dictionary<string, object> OrphanData(ExportEntry data)
        {
            var record = new Dictionary<string, object>
            {
                ["kind"] = "scene", ["path"] = data.InstancedFullPath, ["class"] = "InterpData", ["sequence"] = data.Parent?.InstancedFullPath,
                ["orphanData"] = true, ["inConversation"] = data.InstancedFullPath.Split('.').Contains("Node_Data_Sequence"),
            };
            FillData(record, data, new Dictionary<string, List<object>>());
            if (((List<object>)record["groups"]).Count == 0) return null;
            return record;
        }

        void FillData(Dictionary<string, object> record, ExportEntry data, Dictionary<string, List<object>> bindings)
        {
            var groups = new List<object>();
            float length = 0;
            if (data != null)
            {
                PropertyCollection dataProps = Props(data);
                length = dataProps?.GetProp<FloatProperty>("InterpLength")?.Value ?? 0;
                record["data"] = data.InstancedFullPath;
                foreach (ObjectProperty g in dataProps?.GetProp<ArrayProperty<ObjectProperty>>("InterpGroups") ?? new ArrayProperty<ObjectProperty>("InterpGroups"))
                {
                    if (pcc.GetEntry(g.Value) is ExportEntry group)
                    {
                        groups.Add(Group(group, bindings, length));
                    }
                }
            }
            record["length"] = length;
            record["groups"] = groups;
            // Bindings with no group of that name (a link the matinee doesn't use, or a group named differently).
            var used = new HashSet<string>(groups.Cast<Dictionary<string, object>>().Select(x => (string)x["name"] ?? ""), StringComparer.OrdinalIgnoreCase);
            var spare = bindings.Where(b => !used.Contains(b.Key) && b.Value.Count > 0).ToDictionary(b => b.Key, b => (object)b.Value);
            if (spare.Count > 0) record["unusedBindings"] = spare;
        }

        Dictionary<string, object> Group(ExportEntry group, Dictionary<string, List<object>> bindings, float length)
        {
            PropertyCollection gp = Props(group);
            string name = gp?.GetProp<NameProperty>("GroupName")?.Value.Instanced;
            var result = new Dictionary<string, object> { ["class"] = group.ClassName, ["name"] = name };
            if (name != null && bindings.TryGetValue(name, out var bound)) result["bound"] = bound;
            foreach (Property p in gp ?? new PropertyCollection())
            {
                // ME2/ME3 groups that find their actor themselves (SFXInterpGroup's and kin's names), any m_* name property.
                if (p is NameProperty n && p.Name.Instanced != "GroupName" && n.Value.Instanced != "None")
                {
                    result["name:" + p.Name.Instanced] = n.Value.Instanced;
                }
            }
            var sets = new List<string>();
            var lengths = new Dictionary<string, (float length, float rate)>(StringComparer.OrdinalIgnoreCase);
            foreach (ObjectProperty set in gp?.GetProp<ArrayProperty<ObjectProperty>>("GroupAnimSets") ?? new ArrayProperty<ObjectProperty>("GroupAnimSets"))
            {
                IEntry entry = pcc.GetEntry(set.Value);
                if (entry == null) continue;
                sets.Add(entry.ObjectName.Instanced);
                if (entry is ExportEntry setExport)
                {
                    foreach (var pair in AnimSet(setExport)) lengths[pair.Key] = pair.Value;
                }
            }
            if (sets.Count > 0) result["animSets"] = sets;
            var tracks = new List<object>();
            foreach (ObjectProperty t in gp?.GetProp<ArrayProperty<ObjectProperty>>("InterpTracks") ?? new ArrayProperty<ObjectProperty>("InterpTracks"))
            {
                if (pcc.GetEntry(t.Value) is ExportEntry track)
                {
                    tracks.Add(Track(track, lengths, length));
                }
            }
            result["tracks"] = tracks;
            return result;
        }

        Dictionary<string, (float length, float rate)> AnimSet(ExportEntry set)
        {
            if (animSets.TryGetValue(set.UIndex, out var known)) return known;
            var map = new Dictionary<string, (float, float)>(StringComparer.OrdinalIgnoreCase);
            foreach (ObjectProperty s in Props(set)?.GetProp<ArrayProperty<ObjectProperty>>("Sequences") ?? new ArrayProperty<ObjectProperty>("Sequences"))
            {
                if (pcc.GetEntry(s.Value) is ExportEntry seq && Props(seq) is { } sp)
                {
                    string n = sp.GetProp<NameProperty>("SequenceName")?.Value.Instanced ?? seq.ObjectName.Instanced;
                    map[n] = (sp.GetProp<FloatProperty>("SequenceLength")?.Value ?? 0, sp.GetProp<FloatProperty>("RateScale")?.Value ?? 1);
                }
            }
            animSets[set.UIndex] = map;
            return map;
        }

        Dictionary<string, object> Track(ExportEntry track, Dictionary<string, (float length, float rate)> lengths, float sceneLength)
        {
            PropertyCollection tp = Props(track);
            var result = new Dictionary<string, object> { ["class"] = track.ClassName };
            if (tp == null) return result;
            if (tp.GetProp<StrProperty>("TrackTitle")?.Value is { } title) result["title"] = title;
            if (tp.GetProp<BoolProperty>("bDisableTrack")?.Value == true) result["disabled"] = true;
            foreach (Property p in tp)
            {
                if (p is NameProperty n && (p.Name.Instanced.StartsWith("m_nm", StringComparison.Ordinal) || p.Name.Instanced.StartsWith("nmStarting", StringComparison.Ordinal))
                    && n.Value.Instanced != "None")
                {
                    result[p.Name.Instanced] = n.Value.Instanced;
                }
            }
            // Whom a conversation track acts on: ME1's sActorTag ("Owner", "Player", a speaker's tag), the sequels' m_nmFindActor.
            string actor = tp.GetProp<StrProperty>("sActorTag")?.Value ?? tp.GetProp<NameProperty>("m_nmFindActor")?.Value.Instanced;
            if (!string.IsNullOrEmpty(actor) && actor != "None") result["actor"] = actor;
            // Animation keys (InterpTrackAnimControl and its game subclasses): when each sequence starts and, with its length from
            // the group's AnimSets, when it ends.
            if (tp.GetProp<ArrayProperty<StructProperty>>("AnimSeqs") is { } seqs)
            {
                if (tp.GetProp<NameProperty>("SlotName")?.Value.Instanced is { } slot && slot != "None") result["slot"] = slot;
                var keys = new List<object>();
                for (int i = 0; i < seqs.Count; ++i)
                {
                    StructProperty k = seqs[i];
                    float start = k.GetProp<FloatProperty>("StartTime")?.Value ?? 0;
                    string anim = k.GetProp<NameProperty>("AnimSeqName")?.Value.Instanced;
                    float rate = k.GetProp<FloatProperty>("AnimPlayRate")?.Value ?? 1;
                    float startOff = k.GetProp<FloatProperty>("AnimStartOffset")?.Value ?? 0;
                    float endOff = k.GetProp<FloatProperty>("AnimEndOffset")?.Value ?? 0;
                    bool loop = k.GetProp<BoolProperty>("bLooping")?.Value == true;
                    float next = i + 1 < seqs.Count ? seqs[i + 1].GetProp<FloatProperty>("StartTime")?.Value ?? sceneLength : sceneLength;
                    float? animLength = anim != null && lengths.TryGetValue(anim, out var l) ? l.length : null;
                    float end = next;
                    if (!loop && animLength is float al && rate > 0)
                    {
                        end = Math.Min(next, start + Math.Max(0, al - startOff - endOff) / rate);
                    }
                    keys.Add(new Dictionary<string, object>
                    {
                        ["t"] = Math.Round(start, 3), ["end"] = Math.Round(end, 3), ["anim"] = anim, ["rate"] = rate, ["loop"] = loop,
                        ["animLength"] = animLength is float x ? Math.Round(x, 3) : null,
                    });
                }
                result["anims"] = keys;
            }
            // ME2/ME3 conversation gestures (BioEvtSysTrackGesture): each key's name fields, with its time.
            if (tp.GetProp<ArrayProperty<StructProperty>>("m_aGestures") is { } gestures)
            {
                var times = tp.GetProp<ArrayProperty<StructProperty>>("m_aTrackKeys");
                var keys = new List<object>();
                for (int i = 0; i < gestures.Count; ++i)
                {
                    var key = new Dictionary<string, object> { ["t"] = i < (times?.Count ?? 0) ? Math.Round(times[i].GetProp<FloatProperty>("fTime")?.Value ?? 0, 3) : null };
                    foreach (Property p in gestures[i].Properties)
                    {
                        switch (p)
                        {
                            case NameProperty n when n.Value.Instanced != "None": key[p.Name.Instanced] = n.Value.Instanced; break;
                            case FloatProperty f when p.Name.Instanced.Contains("Length") || p.Name.Instanced.Contains("Speed") || p.Name.Instanced.Contains("Start"):
                                key[p.Name.Instanced] = Math.Round(f.Value, 3); break;
                            case ObjectProperty o when o.Value != 0: key[p.Name.Instanced] = pcc.GetEntry(o.Value)?.ObjectName.Instanced; break;
                        }
                    }
                    keys.Add(key);
                }
                result["gestures"] = keys;
            }
            if (tp.GetProp<StructProperty>("PosTrack")?.GetProp<ArrayProperty<StructProperty>>("Points") is { Count: > 0 } points)
            {
                float[] a = Vector(points[0].GetProp<StructProperty>("OutVal")), b = Vector(points[^1].GetProp<StructProperty>("OutVal"));
                double path = 0;
                for (int i = 1; i < points.Count; ++i)
                {
                    float[] p0 = Vector(points[i - 1].GetProp<StructProperty>("OutVal")), p1 = Vector(points[i].GetProp<StructProperty>("OutVal"));
                    path += Math.Sqrt((p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[1] - p0[1]) * (p1[1] - p0[1]) + (p1[2] - p0[2]) * (p1[2] - p0[2]));
                }
                result["moveKeys"] = points.Count;
                result["moveFirst"] = Math.Round(points[0].GetProp<FloatProperty>("InVal")?.Value ?? 0, 3);
                result["moveLast"] = Math.Round(points[^1].GetProp<FloatProperty>("InVal")?.Value ?? 0, 3);
                result["movePathUnits"] = Math.Round(path, 1);
                result["moveNetUnits"] = Math.Round(Math.Sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2])), 1);
            }
            if (tp.GetProp<ArrayProperty<StructProperty>>("CutTrack") is { } cuts)
            {
                result["cuts"] = cuts.Count;
            }
            if (tp.GetProp<StructProperty>("FloatTrack")?.GetProp<ArrayProperty<StructProperty>>("Points") is { } floats)
            {
                result["floatKeys"] = floats.Count;
                if (track.ClassName.Contains("Fade")) result["fadeMax"] = floats.Count == 0 ? 0 : floats.Max(f => f.GetProp<FloatProperty>("OutVal")?.Value ?? 0);
            }
            if (tp.GetProp<ArrayProperty<StructProperty>>("m_aTrackKeys") is { } trackKeys && !result.ContainsKey("gestures"))
            {
                result["keyTimes"] = trackKeys.Take(16).Select(k => Math.Round(k.GetProp<FloatProperty>("fTime")?.Value ?? 0, 3)).ToList();
                result["keyCount"] = trackKeys.Count;
            }
            if (PropLike.IsMatch(track.ClassName) && !PropertyTrack.IsMatch(track.ClassName))
            {
                result["props"] = Simple(tp);
            }
            return result;
        }

        // A sequence object's or track's properties, flattened (names, numbers, object paths, small structs and arrays).
        Dictionary<string, object> Simple(PropertyCollection props, int depth = 0)
        {
            var result = new Dictionary<string, object>();
            foreach (Property p in props)
            {
                string n = p.Name.Instanced;
                if (n is "ParentSequence" or "OutputLinks" or "VariableLinks" or "InputLinks" or "EventLinks" or "ObjPosX" or "ObjPosY"
                    or "DrawWidth" or "DrawHeight" or "ObjInstanceVersion" or "m_aObjComment" or "None" || p is NoneProperty)
                {
                    continue;
                }
                object value = p switch
                {
                    ObjectProperty o => pcc.GetEntry(o.Value)?.InstancedFullPath,
                    NameProperty nm => nm.Value.Instanced,
                    StrProperty s => s.Value,
                    IntProperty i => i.Value,
                    FloatProperty f => Math.Round(f.Value, 3),
                    BoolProperty b => b.Value,
                    EnumProperty e => e.Value.Instanced,
                    ByteProperty by => by.Value,
                    StructProperty st when depth < 2 => Simple(st.Properties, depth + 1),
                    ArrayProperty<StructProperty> a when depth < 2 => a.Take(12).Select(x => (object)Simple(x.Properties, depth + 1)).ToList(),
                    ArrayProperty<ObjectProperty> a => a.Take(12).Select(x => (object)pcc.GetEntry(x.Value)?.InstancedFullPath).ToList(),
                    ArrayProperty<NameProperty> a => a.Take(12).Select(x => (object)x.Value.Instanced).ToList(),
                    ArrayPropertyBase a => $"[{a.Count}]",
                    _ => null,
                };
                if (value != null) result[n] = value;
            }
            return result;
        }

        // What a variable stands for: the player, an object (its class, name, tag and mesh), a find-by-tag, a named variable's
        // target (one hop), or just its class.
        Dictionary<string, object> Binding(ExportEntry variable, int depth)
        {
            var result = new Dictionary<string, object> { ["var"] = variable.ClassName };
            PropertyCollection vp = Props(variable);
            if (vp == null) return result;
            foreach (Property p in vp)
            {
                if (p is NameProperty n && n.Value.Instanced != "None" && p.Name.Instanced != "VarName") result[p.Name.Instanced] = n.Value.Instanced;
                if (p is StrProperty s && p.Name.Instanced.Contains("Tag", StringComparison.OrdinalIgnoreCase)) result[p.Name.Instanced] = s.Value;
                if (p is IntProperty i && p.Name.Instanced.StartsWith("m_", StringComparison.Ordinal)) result[p.Name.Instanced] = i.Value;
                if (p is EnumProperty e && p.Name.Instanced.StartsWith("m_", StringComparison.Ordinal)) result[p.Name.Instanced] = e.Value.Instanced;
            }
            if (vp.GetProp<NameProperty>("VarName")?.Value.Instanced is { } varName && varName != "None") result["varName"] = varName;
            if (pcc.GetEntry(vp.GetProp<ObjectProperty>("ObjValue")?.Value ?? 0) is { } obj)
            {
                result["objClass"] = obj.ClassName;
                result["obj"] = obj.ObjectName.Instanced;
                if (obj is ExportEntry objExport) Actor(objExport, result);
            }
            if (variable.ClassName == "SeqVar_Named" && depth == 0 && vp.GetProp<NameProperty>("FindVarName")?.Value.Instanced is { } find
                && namedVariables.TryGetValue(find, out var targets))
            {
                result["named"] = targets.Take(3).Select(t => (object)Binding(t, 1)).ToList();
            }
            // A sub-sequence's external variable: what its sequence (or the SequenceReference that holds it) is given for that
            // label in the parent sequence, followed up to three sequences out.
            if (variable.ClassName == "SeqVar_External" && depth < 3 && vp.GetProp<StrProperty>("VariableLabel")?.Value is { } label
                && variable.Parent is ExportEntry seq)
            {
                var outer = new List<object>();
                foreach (ExportEntry holder in new[] { seq, seq.Parent as ExportEntry }.Where(h => h != null && h.ClassName is "Sequence" or "SequenceReference"))
                {
                    foreach (ExportEntry v in Linked(Props(holder), label))
                    {
                        outer.Add(Binding(v, depth + 1));
                    }
                }
                if (outer.Count > 0) result["outer"] = outer;
            }
            return result;
        }

        // An actor's tag and the meshes its components show.
        void Actor(ExportEntry actor, Dictionary<string, object> into)
        {
            PropertyCollection ap = Props(actor);
            if (ap == null) return;
            if (ap.GetProp<NameProperty>("Tag")?.Value.Instanced is { } tag && tag != "None") into["tag"] = tag;
            if (ap.GetProp<NameProperty>("UniqueTag")?.Value.Instanced is { } unique && unique != "None") into["uniqueTag"] = unique;
            var meshes = new List<string>();
            foreach (Property p in ap)
            {
                if (p is ObjectProperty o && pcc.GetEntry(o.Value) is ExportEntry component && component.ClassName.EndsWith("MeshComponent", StringComparison.Ordinal)
                    && Props(component) is { } cp)
                {
                    foreach (string field in new[] { "StaticMesh", "SkeletalMesh" })
                    {
                        if (pcc.GetEntry(cp.GetProp<ObjectProperty>(field)?.Value ?? 0) is { } mesh && !meshes.Contains(mesh.ObjectName.Instanced))
                        {
                            meshes.Add(mesh.ObjectName.Instanced);
                        }
                    }
                }
                if (p is ObjectProperty a && p.Name.Instanced is "ActorType" or "m_oActorType" && pcc.GetEntry(a.Value) is { } type) into["actorType"] = type.ObjectName.Instanced;
            }
            if (meshes.Count > 0) into["meshes"] = meshes;
        }

        Dictionary<int, List<(ExportEntry source, string output)>> Reverse()
        {
            if (reverse != null) return reverse;
            reverse = new Dictionary<int, List<(ExportEntry, string)>>();
            foreach (ExportEntry op in pcc.Exports.Where(e => e.ClassName.Contains("Seq", StringComparison.Ordinal) && !e.IsDefaultObject))
            {
                foreach ((ExportEntry next, string desc) in Outputs(op))
                {
                    if (!reverse.TryGetValue(next.UIndex, out var list)) reverse[next.UIndex] = list = new List<(ExportEntry, string)>();
                    list.Add((op, desc));
                }
            }
            return reverse;
        }

        IEnumerable<(ExportEntry next, string desc)> Outputs(ExportEntry op)
        {
            PropertyCollection props = Props(op);
            foreach (StructProperty link in props?.GetProp<ArrayProperty<StructProperty>>("OutputLinks") ?? new ArrayProperty<StructProperty>("OutputLinks"))
            {
                string desc = link.GetProp<StrProperty>("LinkDesc")?.Value;
                foreach (StructProperty target in link.GetProp<ArrayProperty<StructProperty>>("Links") ?? new ArrayProperty<StructProperty>("Links"))
                {
                    if (pcc.GetEntry(target.GetProp<ObjectProperty>("LinkedOp")?.Value ?? 0) is ExportEntry next)
                    {
                        yield return (next, desc);
                    }
                }
            }
        }

        static bool IsEvent(ExportEntry e) => e.ClassName.Contains("Evt_", StringComparison.Ordinal) || e.ClassName.StartsWith("SeqEvent", StringComparison.Ordinal);

        // The nearest events upstream (up to 3, within 10 actions, through a sub-sequence's own input to its parent): their class,
        // originator and the classes on the way.
        List<object> Upstream(ExportEntry start)
        {
            var found = new List<object>();
            var links = Reverse();
            var seen = new HashSet<int> { start.UIndex };
            var queue = new Queue<(ExportEntry op, int depth, List<string> way)>();
            queue.Enqueue((start, 0, new List<string>()));
            while (queue.Count > 0 && found.Count < 3)
            {
                var (op, depth, way) = queue.Dequeue();
                if (depth >= 10 || !links.TryGetValue(op.UIndex, out var sources)) continue;
                foreach ((ExportEntry source, string output) in sources)
                {
                    if (IsEvent(source) && source.ClassName != "SeqEvent_SequenceActivated")
                    {
                        PropertyCollection ep = Props(source);
                        var ev = new Dictionary<string, object> { ["class"] = source.ClassName, ["path"] = source.InstancedFullPath, ["hops"] = depth + 1, ["way"] = way };
                        if (pcc.GetEntry(ep?.GetProp<ObjectProperty>("Originator")?.Value ?? 0) is { } originator)
                        {
                            ev["originatorClass"] = originator.ClassName;
                            ev["originator"] = originator.ObjectName.Instanced;
                        }
                        foreach (Property p in ep ?? new PropertyCollection())
                        {
                            if (p is IntProperty i && p.Name.Instanced is "m_nNodeID" or "m_nConvNodeID" or "NodeID") ev["node"] = i.Value;
                            if (p is NameProperty n && n.Value.Instanced != "None" && p.Name.Instanced is "EventName" or "m_nmConversation") ev[p.Name.Instanced] = n.Value.Instanced;
                        }
                        found.Add(ev);
                        if (found.Count >= 3) break;
                        continue;
                    }
                    ExportEntry next = source.ClassName == "SeqEvent_SequenceActivated" && source.Parent is ExportEntry sequence ? sequence : source;
                    if (seen.Add(next.UIndex)) queue.Enqueue((next, depth + 1, way.Append(next.ClassName).ToList()));
                }
            }
            return found;
        }

        // What the matinee's outputs lead to, three actions deep: each action's class and, for the telling ones (plot, props,
        // conversations, teleports, fades, other matinees), its name and details.
        List<object> Downstream(ExportEntry interp)
        {
            var result = new List<object>();
            var seen = new HashSet<int> { interp.UIndex };
            var frontier = new List<(ExportEntry op, string via)> { (interp, null) };
            for (int depth = 1; depth <= 3 && frontier.Count > 0; ++depth)
            {
                var next = new List<(ExportEntry, string)>();
                foreach ((ExportEntry op, string via) in frontier)
                {
                    foreach ((ExportEntry to, string desc) in Outputs(op))
                    {
                        if (!seen.Add(to.UIndex)) continue;
                        string output = depth == 1 ? desc : via;
                        var step = new Dictionary<string, object> { ["class"] = to.ClassName, ["depth"] = depth, ["from"] = output };
                        if (PropLike.IsMatch(to.ClassName) || Regex.IsMatch(to.ClassName, "PMExecute|Plot|Transition|Conversation|Teleport|Fade|Interp|Cinematic|Destroy|Hide|Toggle|Give|Remove|Inventory"))
                        {
                            step["name"] = to.ObjectName.Instanced;
                            if (PropLike.IsMatch(to.ClassName)) step["props"] = Simple(Props(to) ?? new PropertyCollection());
                            if (PropLike.IsMatch(to.ClassName)) step["vars"] = Vars(to);
                        }
                        result.Add(step);
                        if (result.Count >= 40) return result;
                        next.Add((to, output));
                    }
                }
                frontier = next;
            }
            return result;
        }

        // The attach, prop, weapon and mesh actions in the same sequence (the prop put in Shepard's hand before the matinee, taken
        // after): class, name, properties, what each variable link binds.
        List<object> Siblings(ExportEntry interp)
        {
            var result = new List<object>();
            if (interp.Parent is not ExportEntry sequence) return result;
            foreach (ObjectProperty o in Props(sequence)?.GetProp<ArrayProperty<ObjectProperty>>("SequenceObjects") ?? new ArrayProperty<ObjectProperty>("SequenceObjects"))
            {
                if (pcc.GetEntry(o.Value) is ExportEntry op && op.UIndex != interp.UIndex && !op.ClassName.StartsWith("SeqVar", StringComparison.Ordinal)
                    && PropLike.IsMatch(op.ClassName) && !op.ClassName.Contains("Interp"))
                {
                    result.Add(new Dictionary<string, object>
                    {
                        ["class"] = op.ClassName, ["name"] = op.ObjectName.Instanced, ["props"] = Simple(Props(op) ?? new PropertyCollection()), ["vars"] = Vars(op),
                    });
                    if (result.Count >= 20) break;
                }
            }
            return result;
        }

        Dictionary<string, object> Vars(ExportEntry op)
        {
            var result = new Dictionary<string, object>();
            foreach (StructProperty link in Props(op)?.GetProp<ArrayProperty<StructProperty>>("VariableLinks") ?? new ArrayProperty<StructProperty>("VariableLinks"))
            {
                string desc = link.GetProp<StrProperty>("LinkDesc")?.Value ?? "?";
                var list = (link.GetProp<ArrayProperty<ObjectProperty>>("LinkedVariables") ?? new ArrayProperty<ObjectProperty>("LinkedVariables"))
                    .Select(v => pcc.GetEntry(v.Value)).OfType<ExportEntry>().Select(v => (object)Binding(v, 0)).ToList();
                if (list.Count > 0) result[desc] = list;
            }
            return result;
        }

        // A conversation's lines by node: each entry's and reply's export ID (a line matinee's BioSeqEvt_ConvNode names it) and
        // who says it (an entry's speaker tag, -1 the owner; a reply is always Shepard's). No text.
        public Dictionary<string, object> Conversation(ExportEntry conversation)
        {
            PropertyCollection props = Props(conversation);
            var speakers = (props?.GetProp<ArrayProperty<StructProperty>>("m_SpeakerList") ?? new ArrayProperty<StructProperty>("m_SpeakerList"))
                .Select(s => s.GetProp<NameProperty>("sSpeakerTag")?.Value.Instanced ?? "?").ToList();
            var entries = new Dictionary<string, object>();
            foreach (StructProperty node in props?.GetProp<ArrayProperty<StructProperty>>("m_EntryList") ?? new ArrayProperty<StructProperty>("m_EntryList"))
            {
                int speaker = node.GetProp<IntProperty>("nSpeakerIndex")?.Value ?? -1;
                if (node.GetProp<IntProperty>("nExportID")?.Value is int id)
                    entries[id.ToString()] = speaker >= 0 && speaker < speakers.Count ? speakers[speaker] : speaker == -1 ? "Owner" : speaker.ToString();
            }
            var replies = (props?.GetProp<ArrayProperty<StructProperty>>("m_ReplyList") ?? new ArrayProperty<StructProperty>("m_ReplyList"))
                .Select(r => r.GetProp<IntProperty>("nExportID")?.Value).Where(i => i != null).ToList();
            return new Dictionary<string, object> { ["kind"] = "conversation", ["path"] = conversation.InstancedFullPath, ["entries"] = entries, ["replies"] = replies };
        }

        // A use: the used object, and the matinees within 12 actions downstream (into sub-sequences by their SequenceActivated
        // inputs is not followed: a use's own sequence is where its scene is, in the packages read).
        public Dictionary<string, object> Use(ExportEntry ev)
        {
            PropertyCollection ep = Props(ev);
            var record = new Dictionary<string, object> { ["kind"] = "use", ["path"] = ev.InstancedFullPath, ["class"] = ev.ClassName };
            if (pcc.GetEntry(ep?.GetProp<ObjectProperty>("Originator")?.Value ?? 0) is { } originator)
            {
                record["originatorClass"] = originator.ClassName;
                record["originator"] = originator.ObjectName.Instanced;
                if (originator is ExportEntry oe) Actor(oe, record);
            }
            if (ep?.GetProp<StrProperty>("InteractText")?.Value is { } text) record["hasInteractText"] = text.Length > 0;
            var matinees = new List<string>();
            var seen = new HashSet<int> { ev.UIndex };
            var frontier = new List<ExportEntry> { ev };
            for (int depth = 1; depth <= 12 && frontier.Count > 0 && matinees.Count < 8; ++depth)
            {
                var next = new List<ExportEntry>();
                foreach (ExportEntry op in frontier)
                {
                    foreach ((ExportEntry to, _) in Outputs(op))
                    {
                        if (!seen.Add(to.UIndex)) continue;
                        if (to.ClassName.EndsWith("SeqAct_Interp", StringComparison.Ordinal)) matinees.Add(to.InstancedFullPath);
                        next.Add(to);
                    }
                }
                frontier = next;
            }
            record["matinees"] = matinees;
            return record;
        }
    }
}
