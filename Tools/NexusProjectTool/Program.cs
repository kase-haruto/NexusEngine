using System.Security.Cryptography;
using System.Text;

namespace NexusProjectTool;

internal static class Program
{
    // 設定とコマンドを解析し、sync・check・watchの処理へ振り分ける。
    public static async Task<int> Main(string[] args)
    {
        try
        {
            ToolOptions options = ParseOptions(args);
            string configPath = Path.GetFullPath(options.ConfigPath);
            string configDirectory = Path.GetDirectoryName(configPath)!;
            ProjectSyncConfig config = ProjectSyncConfig.Load(configPath);
            string projectPath = Path.GetFullPath(config.Project, configDirectory);
            string projectDirectory = Path.GetDirectoryName(projectPath)!;
            string[] managedRoots = config.Roots
                .Select(root => Path.GetFullPath(root.Path, configDirectory))
                .ToArray();

            if (!File.Exists(projectPath))
            {
                throw new FileNotFoundException("The Visual Studio project was not found.", projectPath);
            }

            return options.Command switch
            {
                "sync" => RunOnce(
                    configDirectory,
                    projectDirectory,
                    projectPath,
                    managedRoots,
                    config,
                    writeChanges: !options.DryRun,
                    failOnChanges: false),
                "check" => RunOnce(
                    configDirectory,
                    projectDirectory,
                    projectPath,
                    managedRoots,
                    config,
                    writeChanges: false,
                    failOnChanges: true),
                "watch" => await WatchAsync(
                    configDirectory,
                    projectDirectory,
                    projectPath,
                    managedRoots,
                    config),
                _ => throw new ArgumentException($"Unknown command: {options.Command}")
            };
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"error: {exception.Message}");
            return 2;
        }
    }

    // ファイル走査と同期を1回実行し、check時は差分を終了コードへ反映する。
    private static int RunOnce(
        string configDirectory,
        string projectDirectory,
        string projectPath,
        IReadOnlyList<string> managedRoots,
        ProjectSyncConfig config,
        bool writeChanges,
        bool failOnChanges)
    {
        IReadOnlyList<SourceItem> items =
            FileScanner.Scan(configDirectory, projectDirectory, config);
        var synchronizer = new ProjectSynchronizer(projectPath, items, managedRoots);
        SyncResult result = synchronizer.Synchronize(writeChanges);
        PrintResult(result, writeChanges);
        return failOnChanges && result.HasChanges ? 1 : 0;
    }

    // 物理ファイルの変更を監視し、短時間の連続イベントをまとめて同期する。
    private static async Task<int> WatchAsync(
        string configDirectory,
        string projectDirectory,
        string projectPath,
        IReadOnlyList<string> managedRoots,
        ProjectSyncConfig config)
    {
        // 同じVisual Studioプロジェクトに対するwatchの多重起動を防止する。
        string mutexName = CreateWatchMutexName(projectPath);
        using var watchMutex = new Mutex(initiallyOwned: false, mutexName);
        bool ownsMutex;
        try
        {
            ownsMutex = watchMutex.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            // 前回のプロセスが異常終了していた場合は、放棄されたMutexの所有権を引き継ぐ。
            ownsMutex = true;
        }

        if (!ownsMutex)
        {
            Console.Error.WriteLine(
                $"error: A watcher is already running for '{projectPath}'.");
            return 2;
        }

        try
        {
            return await RunWatchLoopAsync(
                configDirectory,
                projectDirectory,
                projectPath,
                managedRoots,
                config);
        }
        finally
        {
            watchMutex.ReleaseMutex();
        }
    }

    // 実際のファイル監視ループを実行し、Ctrl+Cによる安全な終了を受け付ける。
    private static async Task<int> RunWatchLoopAsync(
        string configDirectory,
        string projectDirectory,
        string projectPath,
        IReadOnlyList<string> managedRoots,
        ProjectSyncConfig config)
    {
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            cancellation.Cancel();
        };

        var signal = new SemaphoreSlim(0, 1);
        var watchers = new List<FileSystemWatcher>();
        foreach (SourceRoot root in config.Roots)
        {
            var watcher = new FileSystemWatcher(
                Path.GetFullPath(root.Path, configDirectory))
            {
                IncludeSubdirectories = true,
                NotifyFilter = NotifyFilters.FileName |
                               NotifyFilters.DirectoryName |
                               NotifyFilters.LastWrite
            };
            FileSystemEventHandler changed = (_, _) => TrySignal(signal);
            RenamedEventHandler renamed = (_, _) => TrySignal(signal);
            watcher.Created += changed;
            watcher.Deleted += changed;
            watcher.Changed += changed;
            watcher.Renamed += renamed;
            watcher.EnableRaisingEvents = true;
            watchers.Add(watcher);
        }

        Console.WriteLine("Watching for project file changes. Press Ctrl+C to stop.");
        _ = RunOnce(
            configDirectory,
            projectDirectory,
            projectPath,
            managedRoots,
            config,
            writeChanges: true,
            failOnChanges: false);

        try
        {
            while (true)
            {
                // エディタ保存時などの連続イベントを400ms待って1回にまとめる。
                await signal.WaitAsync(cancellation.Token);
                await Task.Delay(400, cancellation.Token);
                while (signal.CurrentCount > 0)
                {
                    _ = signal.Wait(0);
                }

                try
                {
                    _ = RunOnce(
                        configDirectory,
                        projectDirectory,
                        projectPath,
                        managedRoots,
                        config,
                        writeChanges: true,
                        failOnChanges: false);
                }
                catch (IOException)
                {
                    await Task.Delay(500, cancellation.Token);
                    TrySignal(signal);
                }
            }
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("Stopped.");
            return 0;
        }
    }

    // 正規化したプロジェクトパスから、ユーザーセッション内で一意なMutex名を作る。
    private static string CreateWatchMutexName(string projectPath)
    {
        string normalizedPath = Path.GetFullPath(projectPath).ToUpperInvariant();
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(normalizedPath));
        return $@"Local\NexusProjectTool.{Convert.ToHexString(hash)}";
    }

    // 未処理の通知がない場合だけシグナルを送り、イベントの過剰蓄積を防ぐ。
    private static void TrySignal(SemaphoreSlim signal)
    {
        if (signal.CurrentCount == 0)
        {
            signal.Release();
        }
    }

    // 検出した変更を処理種別ごとにまとめてコンソールへ表示する。
    private static void PrintResult(SyncResult result, bool wroteChanges)
    {
        if (!result.HasChanges)
        {
            Console.WriteLine("Project files are already synchronized.");
            return;
        }

        foreach (IGrouping<string, SyncChange> group in result.Changes.GroupBy(
                     change => change.Action))
        {
            Console.WriteLine($"{group.Key}:");
            foreach (SyncChange change in group)
            {
                Console.WriteLine($"  {change.Path}");
            }
        }

        Console.WriteLine(wroteChanges
            ? "Project files synchronized."
            : "No files were written.");
    }

    // コマンド名、設定パス、dry-run指定をコマンドラインから解析する。
    private static ToolOptions ParseOptions(string[] args)
    {
        string command = args.FirstOrDefault(arg => !arg.StartsWith('-')) ?? "sync";
        string config = "ProjectSync.json";
        bool dryRun = args.Contains("--dry-run", StringComparer.OrdinalIgnoreCase);

        for (int index = 0; index < args.Length; ++index)
        {
            if (!string.Equals(args[index], "--config", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (++index >= args.Length)
            {
                throw new ArgumentException("--config requires a path.");
            }

            config = args[index];
        }

        if (args.Contains("--help", StringComparer.OrdinalIgnoreCase) ||
            args.Contains("-h", StringComparer.OrdinalIgnoreCase))
        {
            Console.WriteLine(
                """
                NexusProjectTool sync  [--config path] [--dry-run]
                NexusProjectTool check [--config path]
                NexusProjectTool watch [--config path]

                Exit codes: 0 = success, 1 = check found changes, 2 = error.
                """);
            Environment.Exit(0);
        }

        return new ToolOptions(command.ToLowerInvariant(), config, dryRun);
    }
}
