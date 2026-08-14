using System.Diagnostics;
using System.Drawing;

namespace FortyWinksSetup;

internal sealed class SetupForm : Form
{
    private readonly LauncherConfig config;
    private readonly TextBox romPathBox = new();
    private readonly TextBox outputPathBox = new();
    private readonly Button browseRomButton = new();
    private readonly Button browseOutputButton = new();
    private readonly Button buildButton = new();
    private readonly Button installToolsButton = new();
    private readonly Button openFolderButton = new();
    private readonly Button detailsButton = new();
    private readonly ProgressBar progressBar = new();
    private readonly Label statusLabel = new();
    private Process? buildProcess;
    private string missingTools = "";

    public SetupForm(LauncherConfig config)
    {
        this.config = config;
        Text = "40 Winks PC Port";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(680, 410);
        ClientSize = new Size(720, 430);
        Font = new Font("Segoe UI", 9F);
        BackColor = Color.FromArgb(246, 247, 249);
        AutoScaleMode = AutoScaleMode.Dpi;

        BuildInterface();
        romPathBox.Text = config.RomPath;
        outputPathBox.Text = string.IsNullOrWhiteSpace(config.OutputDirectory)
            ? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                "40 Winks PC Port")
            : config.OutputDirectory;

        Shown += (_, _) =>
        {
            if (string.IsNullOrWhiteSpace(romPathBox.Text))
            {
                BeginInvoke(BrowseForRom);
            }
        };
        FormClosing += OnFormClosing;
    }

    private void BuildInterface()
    {
        TableLayoutPanel root = new()
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(28, 24, 28, 24),
            ColumnCount = 1,
            RowCount = 8,
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 16));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 14));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        Label title = new()
        {
            AutoSize = true,
            Text = "40 Winks PC Port",
            Font = new Font("Segoe UI Semibold", 20F),
            ForeColor = Color.FromArgb(29, 34, 41),
            Margin = new Padding(0),
        };
        root.Controls.Add(title, 0, 0);

        TableLayoutPanel fields = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 1,
            RowCount = 3,
            Margin = new Padding(0),
        };
        fields.Controls.Add(CreatePathField(
            "40 Winks ROM", romPathBox, browseRomButton, BrowseForRom), 0, 0);
        fields.Controls.Add(CreatePathField(
            "Playable build folder", outputPathBox, browseOutputButton, BrowseForOutput), 0, 1);
        root.Controls.Add(fields, 0, 2);

        TableLayoutPanel progressPanel = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 1,
            RowCount = 2,
            Margin = new Padding(0),
        };
        progressBar.Dock = DockStyle.Top;
        progressBar.Height = 20;
        progressBar.Minimum = 0;
        progressBar.Maximum = 100;
        progressBar.Style = ProgressBarStyle.Continuous;
        statusLabel.AutoSize = true;
        statusLabel.Text = "Ready";
        statusLabel.ForeColor = Color.FromArgb(75, 82, 92);
        statusLabel.Margin = new Padding(0, 8, 0, 0);
        progressPanel.Controls.Add(progressBar, 0, 0);
        progressPanel.Controls.Add(statusLabel, 0, 1);
        root.Controls.Add(progressPanel, 0, 4);

        FlowLayoutPanel actions = new()
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = true,
            Margin = new Padding(0),
        };
        ConfigureButton(buildButton, "Build & Play", true);
        buildButton.Click += async (_, _) => await BuildAndPlayAsync();
        ConfigureButton(installToolsButton, "Install Build Tools", false);
        installToolsButton.Click += async (_, _) => await InstallBuildToolsAsync(false);
        ConfigureButton(openFolderButton, "Open Build Folder", false);
        openFolderButton.Visible = false;
        openFolderButton.Click += (_, _) => OpenOutputFolder();
        ConfigureButton(detailsButton, "Build Log", false);
        detailsButton.Click += (_, _) => OpenBuildLog();
        actions.Controls.Add(buildButton);
        actions.Controls.Add(openFolderButton);
        actions.Controls.Add(installToolsButton);
        actions.Controls.Add(detailsButton);
        root.Controls.Add(actions, 0, 6);

        Label footer = new()
        {
            AutoSize = true,
            Text = "F1 opens the in-game debug and display menu.",
            ForeColor = Color.FromArgb(98, 104, 113),
            Margin = new Padding(0, 14, 0, 0),
        };
        root.Controls.Add(footer, 0, 7);
        Controls.Add(root);
    }

    private static Control CreatePathField(
        string labelText,
        TextBox textBox,
        Button browseButton,
        Action browseAction)
    {
        TableLayoutPanel field = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 2,
            RowCount = 2,
            Margin = new Padding(0, 0, 0, 12),
        };
        field.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        field.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        Label label = new()
        {
            AutoSize = true,
            Text = labelText,
            Font = new Font("Segoe UI Semibold", 9F),
            ForeColor = Color.FromArgb(43, 48, 56),
            Margin = new Padding(0, 0, 0, 6),
        };
        field.SetColumnSpan(label, 2);
        field.Controls.Add(label, 0, 0);

        textBox.Dock = DockStyle.Fill;
        textBox.Margin = new Padding(0, 0, 8, 0);
        textBox.Height = 30;
        browseButton.Text = "Browse...";
        browseButton.AutoSize = true;
        browseButton.Height = 30;
        browseButton.Margin = new Padding(0);
        browseButton.Click += (_, _) => browseAction();
        field.Controls.Add(textBox, 0, 1);
        field.Controls.Add(browseButton, 1, 1);
        return field;
    }

    private static void ConfigureButton(Button button, string text, bool primary)
    {
        button.Text = text;
        button.AutoSize = true;
        button.MinimumSize = new Size(primary ? 124 : 0, 36);
        button.Padding = new Padding(10, 0, 10, 0);
        button.Margin = new Padding(8, 0, 0, 0);
        if (primary)
        {
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.BackColor = Color.FromArgb(34, 103, 209);
            button.ForeColor = Color.White;
        }
    }

    private void BrowseForRom()
    {
        using OpenFileDialog dialog = new()
        {
            Title = "Choose your 40 Winks N64 ROM",
            Filter = "Nintendo 64 ROMs (*.z64;*.n64;*.v64)|*.z64;*.n64;*.v64|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
        };
        if (!string.IsNullOrWhiteSpace(romPathBox.Text))
        {
            dialog.InitialDirectory = Path.GetDirectoryName(romPathBox.Text);
        }
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            romPathBox.Text = dialog.FileName;
        }
    }

    private void BrowseForOutput()
    {
        using FolderBrowserDialog dialog = new()
        {
            Description = "Choose where to put the playable build",
            UseDescriptionForTitle = true,
            SelectedPath = outputPathBox.Text,
            ShowNewFolderButton = true,
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            outputPathBox.Text = dialog.SelectedPath;
        }
    }

    private async Task BuildAndPlayAsync()
    {
        string romPath = romPathBox.Text.Trim();
        string outputDirectory = outputPathBox.Text.Trim();
        if (!File.Exists(romPath))
        {
            ShowError("Choose your supported 40 Winks ROM first.");
            return;
        }
        if (string.IsNullOrWhiteSpace(outputDirectory))
        {
            ShowError("Choose a folder for the playable build.");
            return;
        }

        SetBusy(true);
        missingTools = "";
        progressBar.Value = 1;
        statusLabel.Text = "Verifying ROM";
        try
        {
            bool validRom = await Task.Run(() => Program.RomHashMatches(romPath));
            if (!validRom)
            {
                throw new InvalidDataException(
                    "This ROM revision is not supported. Select the clean USA aftermarket ROM listed in the project README.");
            }

            string sourceDirectory = await Task.Run(Program.ExtractSourcePayload);
            Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
            int exitCode = await RunBuildProcessAsync(sourceDirectory, romPath, outputDirectory);
            if (exitCode != 0)
            {
                if (!string.IsNullOrWhiteSpace(missingTools))
                {
                    DialogResult choice = MessageBox.Show(
                        this,
                        $"Windows needs these build tools:\n\n{missingTools}\n\nInstall them now?",
                        "40 Winks PC Port",
                        MessageBoxButtons.YesNo,
                        MessageBoxIcon.Information);
                    if (choice == DialogResult.Yes)
                    {
                        SetBusy(false);
                        await InstallBuildToolsAsync(true);
                    }
                    return;
                }
                throw new InvalidOperationException(
                    $"The playable build did not complete. The build log is at:\n{Program.LogPath}");
            }

            Directory.CreateDirectory(outputDirectory);
            File.WriteAllText(Path.Combine(outputDirectory, ".builder-version"), Program.BuildId);
            Program.InstallLauncherCopy(outputDirectory);
            config.RomPath = romPath;
            config.OutputDirectory = outputDirectory;
            config.BuildId = Program.BuildId;
            config.Save(Program.ConfigPath);

            progressBar.Value = 100;
            statusLabel.Text = "Playable build ready";
            openFolderButton.Visible = true;
            Program.LaunchGame(outputDirectory, romPath);
        }
        catch (Exception exception)
        {
            statusLabel.Text = "Build stopped";
            ShowError(exception.Message);
        }
        finally
        {
            SetBusy(false);
        }
    }

    private async Task<int> RunBuildProcessAsync(
        string sourceDirectory,
        string romPath,
        string outputDirectory)
    {
        string scriptPath = Path.Combine(
            sourceDirectory, "packaging", "windows", "build_playable.ps1");
        string powerShell = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.System),
            "WindowsPowerShell", "v1.0", "powershell.exe");
        int jobs = Math.Max(1, Math.Min(Environment.ProcessorCount - 1, 4));
        ProcessStartInfo startInfo = new(powerShell)
        {
            WorkingDirectory = sourceDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        startInfo.ArgumentList.Add("-NoProfile");
        startInfo.ArgumentList.Add("-ExecutionPolicy");
        startInfo.ArgumentList.Add("Bypass");
        startInfo.ArgumentList.Add("-File");
        startInfo.ArgumentList.Add(scriptPath);
        startInfo.ArgumentList.Add("-RomPath");
        startInfo.ArgumentList.Add(romPath);
        startInfo.ArgumentList.Add("-OutputDirectory");
        startInfo.ArgumentList.Add(outputDirectory);
        startInfo.ArgumentList.Add("-Jobs");
        startInfo.ArgumentList.Add(jobs.ToString());

        using StreamWriter log = new(Program.LogPath, false);
        using Process process = new() { StartInfo = startInfo };
        buildProcess = process;
        object logLock = new();
        DataReceivedEventHandler receiveLine = (_, eventArgs) =>
        {
            if (eventArgs.Data is null)
            {
                return;
            }
            lock (logLock)
            {
                log.WriteLine(eventArgs.Data);
                log.Flush();
            }
            BeginInvoke(() => HandleBuildLine(eventArgs.Data));
        };
        process.OutputDataReceived += receiveLine;
        process.ErrorDataReceived += receiveLine;
        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        await process.WaitForExitAsync();
        process.WaitForExit();
        buildProcess = null;
        return process.ExitCode;
    }

    private void HandleBuildLine(string line)
    {
        if (line.StartsWith("FORTY_WINKS_PROGRESS|", StringComparison.Ordinal))
        {
            string[] pieces = line.Split(new[] { '|' }, 3);
            if (pieces.Length == 3 && int.TryParse(pieces[1], out int percent))
            {
                progressBar.Value = Math.Clamp(percent, 0, 100);
                statusLabel.Text = $"{progressBar.Value}% - {pieces[2]}";
            }
        }
        else if (line.StartsWith("FORTY_WINKS_MISSING_TOOLS|", StringComparison.Ordinal))
        {
            missingTools = line[(line.IndexOf('|') + 1)..];
        }
    }

    private async Task InstallBuildToolsAsync(bool resumeBuild)
    {
        try
        {
            SetBusy(true);
            statusLabel.Text = "Installing Windows build tools";
            string sourceDirectory = await Task.Run(Program.ExtractSourcePayload);
            string scriptPath = Path.Combine(
                sourceDirectory, "packaging", "windows", "install_requirements.ps1");
            string powerShell = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System),
                "WindowsPowerShell", "v1.0", "powershell.exe");
            ProcessStartInfo startInfo = new(powerShell)
            {
                UseShellExecute = true,
                Verb = "runas",
                Arguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{scriptPath}\"",
            };
            using Process process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("The build-tool installer did not start.");
            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
            {
                throw new InvalidOperationException(
                    "The build-tool installation did not complete. You can run it again from this window.");
            }

            statusLabel.Text = "Build tools installed";
            if (resumeBuild)
            {
                SetBusy(false);
                await BuildAndPlayAsync();
            }
        }
        catch (Exception exception)
        {
            statusLabel.Text = "Build-tool setup stopped";
            ShowError(exception.Message);
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        romPathBox.Enabled = !busy;
        outputPathBox.Enabled = !busy;
        browseRomButton.Enabled = !busy;
        browseOutputButton.Enabled = !busy;
        buildButton.Enabled = !busy;
        installToolsButton.Enabled = !busy;
        UseWaitCursor = busy;
    }

    private void OpenOutputFolder()
    {
        string outputDirectory = outputPathBox.Text.Trim();
        if (Directory.Exists(outputDirectory))
        {
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{outputDirectory}\"")
            {
                UseShellExecute = true,
            });
        }
    }

    private void OpenBuildLog()
    {
        if (File.Exists(Program.LogPath))
        {
            Process.Start(new ProcessStartInfo("notepad.exe", $"\"{Program.LogPath}\"")
            {
                UseShellExecute = true,
            });
        }
        else
        {
            MessageBox.Show(this, "No build log has been created yet.", Text,
                MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private void ShowError(string message)
    {
        MessageBox.Show(this, message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs eventArgs)
    {
        if (buildProcess is null || buildProcess.HasExited)
        {
            return;
        }

        DialogResult choice = MessageBox.Show(
            this,
            "The playable game is still building. Stop the build and close?",
            Text,
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning);
        if (choice != DialogResult.Yes)
        {
            eventArgs.Cancel = true;
            return;
        }

        try
        {
            buildProcess.Kill(true);
        }
        catch
        {
            // The process may have exited while the confirmation was open.
        }
    }
}
