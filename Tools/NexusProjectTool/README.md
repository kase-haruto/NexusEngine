# NexusProjectTool

`NexusProjectTool` keeps a Visual Studio C++ project and its `.filters` file in
sync with the physical source tree.

## Commands

Run these commands from the repository root:

```powershell
dotnet run --project Tools/NexusProjectTool -- sync
dotnet run --project Tools/NexusProjectTool -- sync --dry-run
dotnet run --project Tools/NexusProjectTool -- check
dotnet run --project Tools/NexusProjectTool -- watch
```

Use `--config path/to/ProjectSync.json` when the configuration is not in the
current directory. `check` does not write files and returns exit code `1` when
synchronization is needed, making it suitable for CI.

## Configuration

```json
{
  "project": "Project/NexusEngine.vcxproj",
  "roots": [
    {
      "path": "Project",
      "filter": ""
    }
  ],
  "excludeDirectories": [".vs", "Generated", "obj", "bin"]
}
```

All paths are relative to the configuration file. A root's `filter` is an
optional Visual Studio filter prefix. Physical directories below a root become
filters below that prefix.

The following extensions are recognized:

- Compile: `.c`, `.cc`, `.cpp`, `.cxx`, `.ixx`, `.cppm`
- Include: `.h`, `.hh`, `.hpp`, `.hxx`, `.inl`, `.inc`, `.ipp`
- Resource: `.rc`
- Other: `.natvis`, `.json`, `.txt`, `.md`

Only source items located under configured roots are added or removed. Existing
MSBuild settings and items outside those roots are preserved.

## Visual Studioと同時に起動する

リポジトリルートから次のスクリプトを実行すると、ツールをビルドして
`watch`をバックグラウンド起動し、Visual Studioでソリューションを開きます。

```powershell
.\StartNexusDevelopment.ps1
```

ビルド済みツールをそのまま使う場合は、起動時間を短縮できます。

```powershell
.\StartNexusDevelopment.ps1 -NoBuild
```

Visual Studioを終了すると、このスクリプトが起動した`watch`も終了します。
同じプロジェクトに対して`watch`を複数起動しようとした場合は、後から起動した
プロセスがエラー終了します。

すでに起動しているVisual Studioへソリューションが引き渡された場合、
起動用のVisual Studioプロセスが先に終了し、`watch`も終了することがあります。
確実に連動させるには、Visual Studioを閉じた状態からこのスクリプトを実行して
ください。

## Tests

The integration test creates a temporary C++ project, verifies add/remove,
item-type classification, nested-filter generation, and idempotency:

```powershell
dotnet run --project Tools/NexusProjectTool.Tests
```
