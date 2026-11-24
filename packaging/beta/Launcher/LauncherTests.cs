// SPDX-License-Identifier: GPL-3.0-or-later
// Pure policy tests. No GUI, runtime, emulator or diagnostic CLI is started.
using System;
using System.IO;
using HarleyVRLauncher;
internal static class LauncherTests
{
    private static int count;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); count++; }
    private static void Reject(string[] args, string exe)
    {
        bool rejected = false;
        try { Options.Parse(args, exe); } catch (ArgumentException) { rejected = true; }
        Check(rejected, "Invalid argument set accepted");
    }
    public static int Main()
    {
        Check(Options.Parse(new string[0], "HarleyVR.exe").Route == null, "Default route must be chosen");
        Check(Options.Parse(new string[0], "HarleyVR-Meta.exe").Route == "Meta", "Meta shortcut");
        Check(Options.Parse(new string[0], "HarleyVR-SteamVR.exe").Route == "SteamVR", "Steam shortcut");
        Check(Options.Parse(new[] { "--steamvr", "--check" }, "HarleyVR.exe").CheckOnly, "Read-only route");
        Check(!Options.Parse(new string[0], "HarleyVR-Meta.exe").DesktopGraphics, "Meta shortcut opens VR graphics by default");
        var desktop = Options.Parse(new[] { "--desktop-graphics" }, "HarleyVR.exe");
        Check(desktop.Route == "Meta" && desktop.DesktopGraphics, "Explicit desktop graphics from chooser executable");
        Check(Options.Parse(new[] { "--desktop-graphics" }, "HarleyVR-Meta.exe").DesktopGraphics, "Explicit desktop graphics from Meta executable");
        Reject(new[] { "--desktop-graphics" }, "HarleyVR-SteamVR.exe");
        Reject(new[] { "--check", "--desktop-graphics" }, "HarleyVR.exe");
        Reject(new[] { "--desktop-graphics", "--steamvr" }, "HarleyVR.exe");
        Reject(new[] { "--meta", "--steamvr" }, "HarleyVR.exe");
        Reject(new[] { "--meta" }, "HarleyVR-SteamVR.exe");
        Reject(new[] { "--check" }, "HarleyVR.exe");
        Reject(new[] { "--arbitrary-script" }, "HarleyVR.exe");
        Check(Runner.Quote("") == "\"\"", "Empty argument");
        Check(Runner.Quote("hello world") == "\"hello world\"", "Space argument");
        Check(Runner.Quote("end\\") == "\"end\\\\\"", "Trailing backslash");
        Check(Runner.Quote("say\"hello") == "\"say\\\"hello\"", "Embedded quote");
        foreach (string route in new[] { "Meta", "SteamVR" })
        {
            var info = Runner.StartInfo(@"C:\Games\Harley's & $ ride", route, true);
            Check(info.FileName.StartsWith(Environment.GetFolderPath(Environment.SpecialFolder.Windows)), "Use installed Windows host");
            Check(!info.UseShellExecute && info.CreateNoWindow, "No shell / console window");
            Check(info.RedirectStandardError && info.RedirectStandardOutput, "Capture useful diagnostics");
            Check(info.Arguments.Contains("-PreflightOnly"), "Read-only launch check");
            Check(info.Arguments.Contains("-Runtime " + route), "Exact route selection");
            Check(info.Arguments.Contains("\"C:\\Games\\Harley's & $ ride\\HarleyVR-Start.ps1\""), "Quoted file path");
            Check(!info.Arguments.Contains("-MetaRenderPreset"), "No preference mutation on a check");
        }
        string folder = Path.Combine(Path.GetTempPath(), "HarleyVR-launcher-quality-" + Guid.NewGuid().ToString("N"));
        string config = Path.Combine(folder, "Config");
        string preference = Path.Combine(config, "HarleyVRMetaGraphics.ini");
        try
        {
            Check(MetaGraphics.ReadPreset(folder) == MetaRenderPreset.Balanced, "First start uses Balanced");
            Check(!Directory.Exists(folder), "Reading first-run preference creates nothing");
            Directory.CreateDirectory(config);
            foreach (MetaRenderPreset preset in Enum.GetValues(typeof(MetaRenderPreset)))
            {
                var info = Runner.StartInfo(folder, "Meta", false, preset);
                Check(info.Arguments.EndsWith(" -MetaRenderPreset " + preset, StringComparison.Ordinal), "Forward one selected preset");
                string body = "RenderPreset=" + preset + "\r\n";
                File.WriteAllText(preference, body);
                Check(MetaGraphics.ReadPreset(folder) == preset, "Remembered selection");
                Check(File.ReadAllText(preference) == body, "Reading does not modify the preference");
            }
            foreach (string invalid in new[] { "", "RenderPreset=Unlimited", "RenderPreset=Quality\nRenderPreset=Performance",
                "RenderPreset=" + new string('x', 160), "RenderPreset=Quality; exit" })
            {
                File.WriteAllText(preference, invalid);
                Check(MetaGraphics.ReadPreset(folder) == MetaRenderPreset.Balanced, "Malformed preference falls back to Balanced");
            }
            Check(!Runner.StartInfo(folder, "SteamVR", false).Arguments.Contains("-MetaRenderPreset"), "SteamVR arguments unchanged");
            Check(!Runner.StartInfo(folder, "Meta", false).Arguments.Contains("-MetaRenderPreset"), "Normal Meta launch opens headset picker");
            RejectPreset(folder, "SteamVR", false, MetaRenderPreset.Quality);
            RejectPreset(folder, "Meta", true, MetaRenderPreset.Quality);
            RejectPreset(folder, "Meta", false, (MetaRenderPreset)99);
        }
        finally
        {
            if (File.Exists(preference)) File.Delete(preference);
            if (Directory.Exists(config)) Directory.Delete(config);
            if (Directory.Exists(folder)) Directory.Delete(folder);
        }
        Console.WriteLine("Launcher policy checks passed: " + count);
        return 0;
    }
    private static void RejectPreset(string root, string route, bool checkOnly, MetaRenderPreset preset)
    {
        bool rejected = false;
        try { Runner.StartInfo(root, route, checkOnly, preset); } catch (ArgumentException) { rejected = true; }
        Check(rejected, "Invalid preset or route rejected");
    }
}
