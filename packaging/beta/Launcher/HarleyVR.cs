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
        internal bool DesktopGraphics;
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
                else if (arg == "--desktop-graphics") { value.DesktopGraphics = true; route = "Meta"; }
                else throw new ArgumentException("Unknown option: " + arg);
                if (route != null)
                {
                    if (value.Route != null && value.Route != route) throw new ArgumentException("Choose one headset route.");
                    value.Route = route;
                }
            }
            if (value.CheckOnly && value.Route == null) throw new ArgumentException("Use --meta or --steamvr with --check.");
            if (value.CheckOnly && value.DesktopGraphics) throw new ArgumentException("Choose a graphics menu or a read-only check.");
            return value;
        }
    }

    internal sealed class Result
    {
        internal int ExitCode;
        internal string Message;
    }

    internal enum MetaRenderPreset { Performance, Balanced, Quality }

    internal static class MetaGraphics
    {
        internal static MetaRenderPreset ReadPreset(string root)
        {
            string path = Path.Combine(root, "Config", "HarleyVRMetaGraphics.ini");
            try
            {
                using (var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var reader = new StreamReader(file))
                {
                    if (file.Length <= 128)
                    {
                        string setting = reader.ReadLine();
                        if (reader.ReadLine() == null)
                        {
                            if (setting == "RenderPreset=Performance") return MetaRenderPreset.Performance;
                            if (setting == "RenderPreset=Quality") return MetaRenderPreset.Quality;
                        }
                    }
                }
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return MetaRenderPreset.Balanced;
        }
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

        internal static ProcessStartInfo StartInfo(string root, string route, bool checkOnly, MetaRenderPreset? preset = null)
        {
            if (route != "Meta" && route != "SteamVR") throw new ArgumentException("Unsupported headset route.");
            if (preset.HasValue && (route != "Meta" || checkOnly || !Enum.IsDefined(typeof(MetaRenderPreset), preset.Value)))
                throw new ArgumentException("Choose a render preset when starting the Meta PC VR edition.");
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string powershell = Path.Combine(windows, "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
            var info = new ProcessStartInfo();
            info.FileName = powershell;
            info.WorkingDirectory = Path.GetFullPath(root);
            info.Arguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                Quote(Path.Combine(info.WorkingDirectory, "HarleyVR-Start.ps1")) + " -Runtime " + route +
                (checkOnly ? " -PreflightOnly" : "") + (preset.HasValue ? " -MetaRenderPreset " + preset.Value : "");
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.WindowStyle = ProcessWindowStyle.Hidden;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            return info;
        }

        internal static Result Run(string root, string route, bool checkOnly, MetaRenderPreset? preset = null)
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
                process.StartInfo = StartInfo(root, route, checkOnly, preset);
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
        private readonly Panel routes = new Panel();
        private readonly Panel graphics = new Panel();
        private readonly RadioButton performance;
        private readonly RadioButton balanced;
        private readonly RadioButton quality;
        private readonly Button start;
        private readonly Button back;
        internal LaunchWindow(string packageRoot, string autoRoute, bool desktopGraphics)
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
            routes.Bounds = graphics.Bounds = new Rectangle(0, 86, 580, 304);
            graphics.Visible = false;
            var caption = new Label { Text = "Choose the VR software you're using on this PC.", Location = new Point(32, 4), Size = new Size(520, 28) };
            meta = MakeButton("META PC VR\r\nOpen headset graphics", 32, 49);
            steam = MakeButton("STEAMVR\r\nOpen the ride menu", 300, 49);
            status = new Label { Text = "For direct Meta PC VR, open Meta Horizon Link and close SteamVR.\r\nOn Quest, enter wired Link or Air Link for direct Meta. If you use Steam Link, connect to SteamVR first.", Location = new Point(32, 159), Size = new Size(520, 70) };
            var desktopLink = new LinkLabel { Text = "Meta graphics on this PC", Location = new Point(32, 232), Size = new Size(500, 28), LinkColor = Color.FromArgb(255, 211, 99), ActiveLinkColor = Color.White };
            desktopLink.LinkClicked += delegate { OpenGraphics(); };
            var help = new LinkLabel { Text = "Add HarleyVR to your Steam library", Location = new Point(32, 265), Size = new Size(500, 28), LinkColor = Color.FromArgb(255, 211, 99), ActiveLinkColor = Color.White };
            help.LinkClicked += delegate { MessageBox.Show(this, "In Steam, choose Games > Add a Non-Steam Game. Browse to HarleyVR-SteamVR.exe in this folder and add it.\r\n\r\nMark the shortcut for your VR library if that option is available in Steam. You can then launch it from the headset library.", "HarleyVR in Steam", MessageBoxButtons.OK, MessageBoxIcon.Information); };
            routes.Controls.AddRange(new Control[] { caption, meta, steam, status, desktopLink, help });
            var graphicsTitle = new Label { Text = "Meta PC VR render quality", Font = new Font("Segoe UI", 17, FontStyle.Bold), Location = new Point(32, 0), Size = new Size(520, 36) };
            var graphicsInfo = new Label { Text = "Applies when the game starts. Each percentage scales the width and height Meta recommends for each eye.", Location = new Point(32, 39), Size = new Size(520, 48) };
            performance = PresetButton("&Performance (50%)", 90, 0);
            balanced = PresetButton("&Balanced (75%)", 129, 1);
            quality = PresetButton("&Quality (100%)", 168, 2);
            back = new Button { Text = desktopGraphics ? "Cancel" : "Back", TabIndex = 4, Location = new Point(32, 224), Size = new Size(144, 52), UseVisualStyleBackColor = true, ForeColor = SystemColors.ControlText };
            start = MakeButton("Start arcade game", 296, 224);
            start.Size = new Size(250, 52);
            start.TabIndex = 3;
            graphics.Controls.AddRange(new Control[] { graphicsTitle, graphicsInfo, performance, balanced, quality, start, back });
            Controls.AddRange(new Control[] { title, routes, graphics });

            back.Click += delegate {
                if (desktopGraphics) { Close(); return; }
                graphics.Visible = false;
                routes.Visible = true;
                AcceptButton = null;
                CancelButton = null;
                meta.Select();
            };
            start.Click += async delegate {
                var preset = quality.Checked ? MetaRenderPreset.Quality : performance.Checked ? MetaRenderPreset.Performance : MetaRenderPreset.Balanced;
                await Launch("Meta", preset);
            };
            meta.Click += async delegate { await Launch("Meta"); };
            steam.Click += async delegate { await Launch("SteamVR"); };
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (busy) { e.Cancel = true; Hide(); } };
            if (desktopGraphics) OpenGraphics();
            else if (autoRoute != null) Shown += async delegate { await Launch(autoRoute); };
        }
        private static RadioButton PresetButton(string caption, int y, int tabIndex)
        {
            return new RadioButton { Text = caption, Location = new Point(36, y), Size = new Size(510, 36), TabIndex = tabIndex, Font = new Font("Segoe UI", 13) };
        }
        private void OpenGraphics()
        {
            var saved = MetaGraphics.ReadPreset(root);
            var selected = saved == MetaRenderPreset.Quality ? quality : saved == MetaRenderPreset.Performance ? performance : balanced;
            routes.Visible = false;
            graphics.Visible = true;
            selected.Checked = true;
            selected.Select();
            AcceptButton = start;
            CancelButton = back;
        }
        private Button MakeButton(string text, int x, int y)
        {
            return new Button { Text = text, Location = new Point(x, y), Size = new Size(248, 90), BackColor = Color.FromArgb(255, 211, 99), ForeColor = Color.FromArgb(15, 35, 50), FlatStyle = FlatStyle.Flat, Font = new Font("Segoe UI", 12, FontStyle.Bold), UseVisualStyleBackColor = false };
        }
        private async Task Launch(string route, MetaRenderPreset? preset = null)
        {
            if (busy) return;
            busy = true; meta.Enabled = steam.Enabled = false; Hide();
            Result result;
            try { result = await Task.Run(() => Runner.Run(root, route, false, preset)); }
            catch (Exception error) { result = new Result { ExitCode = 1, Message = error.Message }; }
            busy = false;
            if (result.ExitCode == 0) { Close(); return; }
            Show(); Activate(); meta.Enabled = steam.Enabled = true;
            status.Text = route == "Meta" && !preset.HasValue
                ? "If headset graphics did not open, choose Meta graphics on this PC to start from the desktop."
                : "The game could not start or finish normally. Check the message for details.";
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
                Application.Run(new LaunchWindow(root, options.Route, options.DesktopGraphics));
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
