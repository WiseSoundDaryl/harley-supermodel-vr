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
        }
        Console.WriteLine("Launcher policy checks passed: " + count);
        return 0;
    }
}
