// SPDX-License-Identifier: GPL-3.0-or-later
// Small Windows launcher. It leaves runtime selection, recovery and validation
// with the same packaged PowerShell supervisor used by the command launchers.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace HarleyVRLauncher
{
    internal sealed class Options
    {
        internal string Route;
        internal bool CheckOnly;
        internal static Options Parse(string[] args, string executable)
        {
            var value = new Options();
            string name = Path.GetFileNameWithoutExtension(executable);
            if (String.Equals(name, "HarleyVR-Meta", StringComparison.OrdinalIgnoreCase)) value.Route = "Meta";
            if (String.Equals(name, "HarleyVR-SteamVR", StringComparison.OrdinalIgnoreCase)) value.Route = "SteamVR";
            foreach (string arg in args)
            {
                string route = null;
                if (arg == "--meta") route = "Meta";
                else if (arg == "--steamvr") route = "SteamVR";
                else if (arg == "--check") value.CheckOnly = true;
                else throw new ArgumentException("Unknown option: " + arg);
                if (route != null)
                {
                    if (value.Route != null && value.Route != route) throw new ArgumentException("Choose one headset route.");
                    value.Route = route;
                }
            }
            if (value.CheckOnly && value.Route == null) throw new ArgumentException("Use --meta or --steamvr with --check.");
            return value;
        }
    }

    internal sealed class Result
    {
        internal int ExitCode;
        internal string Message;
    }

    internal static class Runner
    {
        // Windows argument quoting; never execute a shell-built command string.
        internal static string Quote(string value)
        {
            var text = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') { text.Append('\\', slashes * 2 + 1); text.Append(c); }
                else { text.Append('\\', slashes); text.Append(c); }
                slashes = 0;
            }
            text.Append('\\', slashes * 2); text.Append('"');
            return text.ToString();
        }

        internal static ProcessStartInfo StartInfo(string root, string route, bool checkOnly)
        {
            if (route != "Meta" && route != "SteamVR") throw new ArgumentException("Unsupported headset route.");
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string powershell = Path.Combine(windows, "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
            var info = new ProcessStartInfo();
            info.FileName = powershell;
            info.WorkingDirectory = Path.GetFullPath(root);
            info.Arguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                Quote(Path.Combine(info.WorkingDirectory, "HarleyVR-Start.ps1")) + " -Runtime " + route +
                (checkOnly ? " -PreflightOnly" : "");
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.WindowStyle = ProcessWindowStyle.Hidden;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            return info;
        }

        internal static Result Run(string root, string route, bool checkOnly)
        {
            string script = Path.Combine(root, "HarleyVR-Start.ps1");
            if (!File.Exists(script)) return new Result { ExitCode = 1, Message = "Extract the complete HarleyVR ZIP before playing. HarleyVR-Start.ps1 is missing beside this launcher." };
            var output = new StringBuilder();
            object sync = new object();
            DataReceivedEventHandler collect = delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data == null) return;
                lock (sync)
                {
                    output.AppendLine(e.Data);
                    if (output.Length > 16384) output.Remove(0, output.Length - 16384);
                }
            };
            using (var process = new Process())
            {
                process.StartInfo = StartInfo(root, route, checkOnly);
                process.OutputDataReceived += collect; process.ErrorDataReceived += collect;
                if (!process.Start()) throw new InvalidOperationException("The launcher could not start.");
                process.BeginOutputReadLine(); process.BeginErrorReadLine();
                // Stay alive until the supervisor has completed game shutdown
                // and Meta setting restoration. Steam can track this lifetime.
                process.WaitForExit();
                lock (sync) return new Result { ExitCode = process.ExitCode, Message = output.ToString().Trim() };
            }
        }
    }

    internal sealed class LaunchWindow : Form
    {
        private readonly string root;
        private bool busy;
        private readonly Button meta;
        private readonly Button steam;
        private readonly Label status;
        internal LaunchWindow(string packageRoot, string autoRoute)
        {
            root = packageRoot;
            Text = "HarleyVR";
            ClientSize = new Size(580, 390);
            MinimumSize = MaximumSize = Size;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            BackColor = Color.FromArgb(15, 50, 78);
            ForeColor = Color.White;
            Font = new Font("Segoe UI", 11);
            var title = new Label { Text = "HARLEYVR", Location = new Point(28, 20), AutoSize = true, Font = new Font("Segoe UI", 32, FontStyle.Bold), ForeColor = Color.FromArgb(255, 211, 99) };
            var caption = new Label { Text = "Choose your headset. Your ride menu opens in VR.", Location = new Point(32, 90), Size = new Size(520, 28) };
            meta = MakeButton("PLAY WITH META\r\nRift S / Meta Horizon Link", 32, 135);
            steam = MakeButton("PLAY WITH STEAMVR\r\nPSVR2 / SteamVR", 300, 135);
            status = new Label { Text = "Open the matching headset software first.\r\nFor Rift S, leave Meta Horizon Link open and SteamVR closed.", Location = new Point(32, 245), Size = new Size(520, 58) };
            var help = new LinkLabel { Text = "Add HarleyVR to your Steam library", Location = new Point(32, 325), Size = new Size(420, 28), LinkColor = Color.FromArgb(255, 211, 99), ActiveLinkColor = Color.White };
            help.LinkClicked += delegate { MessageBox.Show(this, "In Steam, choose Games > Add a Non-Steam Game. Browse to HarleyVR-SteamVR.exe in this folder and add it.\r\n\r\nMark the shortcut for your VR library if that option is available in Steam. You can then try it from the headset library. This does not install the game or enable Steam updates.\r\n\r\nUse the Meta launcher directly for Rift S; it keeps SteamVR out of that route.", "HarleyVR in Steam", MessageBoxButtons.OK, MessageBoxIcon.Information); };
            Controls.AddRange(new Control[] { title, caption, meta, steam, status, help });
            meta.Click += async delegate { await Launch("Meta"); };
            steam.Click += async delegate { await Launch("SteamVR"); };
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (busy) { e.Cancel = true; Hide(); } };
            if (autoRoute != null) Shown += async delegate { await Launch(autoRoute); };
        }
        private Button MakeButton(string text, int x, int y)
        {
            return new Button { Text = text, Location = new Point(x, y), Size = new Size(248, 90), BackColor = Color.FromArgb(255, 211, 99), ForeColor = Color.FromArgb(15, 35, 50), FlatStyle = FlatStyle.Flat, Font = new Font("Segoe UI", 12, FontStyle.Bold), UseVisualStyleBackColor = false };
        }
        private async Task Launch(string route)
        {
            if (busy) return;
            busy = true; meta.Enabled = steam.Enabled = false; Hide();
            Result result;
            try { result = await Task.Run(() => Runner.Run(root, route, false)); }
            catch (Exception error) { result = new Result { ExitCode = 1, Message = error.Message }; }
            busy = false;
            if (result.ExitCode == 0) { Close(); return; }
            Show(); Activate(); meta.Enabled = steam.Enabled = true;
            status.Text = "The game could not start or finish normally. Your other applications were left running.";
            MessageBox.Show(this, String.IsNullOrWhiteSpace(result.Message) ? "Please check the extracted package and headset software." : result.Message, "HarleyVR", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    internal static class Program
    {
        [STAThread]
        internal static int Main(string[] args)
        {
            try
            {
                var options = Options.Parse(args, Application.ExecutablePath);
                string root = Path.GetDirectoryName(Application.ExecutablePath);
                if (options.CheckOnly)
                {
                    Result result = Runner.Run(root, options.Route, true);
                    if (result.ExitCode == 0) Console.Out.WriteLine(result.Message);
                    else Console.Error.WriteLine(result.Message);
                    return result.ExitCode;
                }
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new LaunchWindow(root, options.Route));
                return 0;
            }
            catch (Exception error)
            {
                if (Array.IndexOf(args, "--check") >= 0) Console.Error.WriteLine(error.Message);
                else MessageBox.Show(error.Message, "HarleyVR", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return 1;
            }
        }
    }
}
