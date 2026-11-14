// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;

namespace Mevr.ConversationScan;

internal static class InspectionContext
{
    internal static bool IsLe2 => Program.GameName == "LE2";
    internal static MEGame Game => IsLe2 ? MEGame.LE2 : MEGame.LE1;

    internal static void Initialize() => LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default,
        s => Console.Error.WriteLine("LEC: " + s), null, new[] { Game });

    internal static IMEPackage Open(string path)
    {
        IMEPackage package = MEPackageHandler.OpenMEPackage(path, forceLoadFromDisk: true);
        if (package.Game == Game)
            return package;
        string actual = package.Game.ToString();
        package.Dispose();
        throw new InvalidDataException($"Package {Path.GetFileName(path)} is {actual}, selected game is {Game}; use --game explicitly.");
    }
}
