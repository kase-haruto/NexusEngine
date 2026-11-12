# Projectの値だけを検証する。Build・DLLロード・Project書換えは行わない。
function Get-NexusModuleOrder([object]$Project) {
    if(-not $Project.PSObject.Properties['modules']) { return }
    $modules = $Project.modules
    if($modules -isnot [Array] -or $modules.Count -gt 256) { throw 'Invalid Project modules array.' }
    $lookup = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($module in $modules) {
        if(-not $module -or $module.id -isnot [string] -or $module.id -cnotmatch '\A[A-Za-z0-9._-]{1,128}\z' -or $lookup.ContainsKey($module.id)) { throw 'Invalid or duplicate Module ID.' }
        if($module.language -cnotin @('Cpp','CSharp') -or $module.type -cne 'Game') { throw 'Unsupported Module language/type.' }
        $lookup.Add($module.id, $module)
    }
    foreach($module in $modules) {
        if(-not $module.PSObject.Properties['dependencies']) { continue }
        if($module.dependencies -isnot [Array] -or $module.dependencies.Count -gt $modules.Count) { throw "Invalid dependencies: $($module.id)" }
        $unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach($dependency in $module.dependencies) {
            if($dependency -isnot [string] -or -not $lookup.ContainsKey($dependency) -or $dependency -ceq $module.id -or -not $unique.Add($dependency)) { throw "Unknown, self or duplicate dependency: $($module.id) -> $dependency" }
        }
    }
    $states = [Collections.Generic.Dictionary[string,int]]::new([StringComparer]::Ordinal)
    $order = [Collections.Generic.List[string]]::new()
    # 最大256 Moduleへ深さを制限する。保存順を変更せず、依存先を先に列挙する。
    function Visit-NexusModule([string]$Id) {
        if($states.ContainsKey($Id)) {
            if($states[$Id] -eq 1) { throw "Cyclic Module dependency at: $Id" }
            return
        }
        $states[$Id] = 1
        foreach($dependency in $lookup[$Id].dependencies) { Visit-NexusModule $dependency }
        $states[$Id] = 2
        $order.Add($Id)
    }
    foreach($module in $modules) { Visit-NexusModule $module.id }
    return $order.ToArray()
}
# 保存IDを維持したままC#識別子へ変換する。置換によるa-b / a_bの衝突と数字始まりを避ける。
function Get-NexusModuleCodeIdentifier([string]$Id) {
    if($Id -cnotmatch '\A[A-Za-z0-9._-]{1,128}\z') { throw 'Invalid Module ID for generated source.' }
    if($Id -cmatch '\A[A-Za-z][A-Za-z0-9_]*\z') { return $Id }
    # 通常IDは先頭underscoreを使わないため、hex表現とも衝突しない。短いIDの可読性は保持する。
    return '_NexusModule_'+[BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($Id)).Replace('-','')
}
# 現在のRuntime実行契約を、宣言だけの依存グラフと区別する。
function Assert-NexusRuntimeModules([object]$Project) {
    $native = @($Project.modules | Where-Object { $_.language -ceq 'Cpp' })
    $managed = @($Project.modules | Where-Object { $_.language -ceq 'CSharp' })
    foreach($module in $native) {
        foreach($dependency in $module.dependencies) {
            foreach($target in $managed) {
                if($target.id -ceq $dependency) { throw 'Current Runtime starts Native before Managed; a Native Module cannot depend on Managed initialization.' }
            }
        }
    }
}

# Projectの入力・出力参照をRoot内へ限定する。存在しない生成先も検査し、既存junctionを辿らない。
function Resolve-NexusProjectReference([string]$Root,[string]$Reference) {
    if([string]::IsNullOrWhiteSpace($Reference) -or [IO.Path]::IsPathRooted($Reference)) { throw 'Project reference must be nonempty and relative.' }
    $rootPath=[IO.Path]::GetFullPath($Root)
    $path=[IO.Path]::GetFullPath((Join-Path $rootPath $Reference))
    if(-not $path.StartsWith($rootPath.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Project reference escapes Root.' }
    $cursor=$path
    while($cursor -and $cursor.Length -ge $rootPath.Length) {
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Project reference cannot traverse a reparse point.' }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}

# Build / Reloadで同じModule計画を使用する。未登録のmappingや出力衝突はコンパイル開始前に拒否する。
function Get-NexusModuleBuildPlan([object]$Project,[object]$Targets,[string]$Root,[string]$Configuration,[string]$BuildProfile='Standard',[object]$Engine=$null) {
    $order=@(Get-NexusModuleOrder $Project); Assert-NexusRuntimeModules $Project
    if($Targets.schemaVersion -ne 1 -or $Configuration -cnotin @('Debug','Develop','Release') -or $BuildProfile -cnotin @('Standard','NativeLivePatch')) { throw 'Invalid Module Build schema / configuration / profile.' }
    $native=@($Project.modules | Where-Object { $_.language -ceq 'Cpp' })
    $managed=@($Project.modules | Where-Object { $_.language -ceq 'CSharp' })
    if([bool]$managed.Count -ne [bool]$Targets.managedBootstrap -or ($Targets.nativeModule -and -not $native.Count) -or ($Targets.gameScripts -and -not $managed.Count) -or ($managed.Count -gt 1 -and -not $Targets.gameScripts)) { throw 'Module languages and BuildTargets backend differ.' }
    if($BuildProfile -ceq 'NativeLivePatch' -and ($native.Count -ne 1 -or -not $Targets.nativeModule -or $Configuration -ceq 'Release' -or $Project.engine.association -cne 'development')) { throw 'NativeLivePatch requires one independent Native DLL in development Debug / Develop.' }
    function Resolve-NexusModuleOutput([string]$Reference) {
        $value=$Reference.Replace('{configuration}',$Configuration).Replace('\','/')
        if($BuildProfile -ceq 'NativeLivePatch') {
            if(-not $value.StartsWith('Build/Bin/x64/',[StringComparison]::Ordinal)) { throw 'Invalid NativeLivePatch output layout.' }
            $value='Build/Bin/LivePP/x64/'+$value.Substring('Build/Bin/x64/'.Length)
        }
        return Resolve-NexusProjectReference $Root $value
    }
    $runtime=Resolve-NexusModuleOutput $Targets.runtimeExecutable
    $editor=Resolve-NexusModuleOutput $Targets.editorExecutable
    $bin=[IO.Path]::GetDirectoryName($runtime)
    if([IO.Path]::GetDirectoryName($editor) -ine $bin) { throw 'Project hosts must share their Module deployment directory.' }
    $outputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($reserved in @('Managed/Nexus.Managed.dll','Managed/Nexus.Managed.Bootstrap.dll')) { [void]$outputs.Add([IO.Path]::GetFullPath((Join-Path $bin $reserved))) }
    foreach($reserved in @('nethost.dll','dxcompiler.dll','dxil.dll')+@($Engine.runtimeFiles | Where-Object { $_ } | ForEach-Object { [IO.Path]::GetFileName($_) })) { [void]$outputs.Add([IO.Path]::GetFullPath((Join-Path $bin $reserved))) }
    if($Targets.managedBootstrap -and (Resolve-NexusModuleOutput $Targets.managedBootstrap) -ine [IO.Path]::GetFullPath((Join-Path $bin 'Managed/Nexus.Managed.Bootstrap.dll'))) { throw 'Invalid stable Bootstrap deployment.' }
    if($Targets.nativeModule -and (Resolve-NexusModuleOutput $Targets.nativeModule) -ine (Join-Path $bin 'Game.dll')) { throw 'Primary Native Module must be Game.dll beside its host.' }
    if($Targets.gameScripts -and (Resolve-NexusModuleOutput $Targets.gameScripts) -ine [IO.Path]::GetFullPath((Join-Path $bin 'Managed/GameScripts.dll'))) { throw 'Primary Managed Module must be Managed/GameScripts.dll.' }
    if($native.Count -and -not $Targets.nativeModule -and @($native[0].dependencies | Where-Object { $_ }).Count) { throw 'Legacy static Game setup cannot depend on dynamic Module initialization.' }
    $mappings=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    if($Targets.PSObject.Properties['moduleProjects'] -and ($Targets.moduleProjects -isnot [Array] -or $Targets.moduleProjects.Count -gt 256)) { throw 'Invalid additional Module mappings.' }
    foreach($mapping in $Targets.moduleProjects) {
        if(-not $mapping -or $mapping.id -isnot [string] -or $mappings.ContainsKey($mapping.id) -or $mapping.id -cnotin $order) { throw 'Unknown or duplicate additional Module mapping.' }
        $mappings.Add($mapping.id,$mapping)
    }
    $projects=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($hostProject in @($Targets.runtimeProject,$Targets.editorProject)) { [void]$projects.Add((Resolve-NexusProjectReference $Root $hostProject)) }
    $plan=[Collections.Generic.List[object]]::new()
    foreach($id in $order) {
        $module=@($Project.modules | Where-Object { $_.id -ceq $id })[0]
        $primary=if($module.language -ceq 'Cpp') { $id -ceq $native[0].id } else { $id -ceq $managed[0].id }
        if($primary) {
            if($mappings.ContainsKey($id)) { throw 'Primary Modules use the existing BuildTargets fields, not moduleProjects.' }
            $reference=if($module.language -ceq 'Cpp') { $Targets.nativeModule } else { $Targets.gameScripts }
            $projectReference=if($module.language -ceq 'Cpp') { 'GameModule.vcxproj' } else { 'GameScripts.csproj' }
        } else {
            if(-not $mappings.ContainsKey($id)) { throw "Missing Module mapping: $id" }
            $reference=$mappings[$id].output; $projectReference=$mappings[$id].project
        }
        $source=''
        if(-not $primary -or $reference) {
            $source=Resolve-NexusProjectReference $Root $projectReference
            if(-not $projects.Add($source) -or [IO.Path]::GetExtension($source) -cne $(if($module.language -ceq 'Cpp') { '.vcxproj' } else { '.csproj' })) { throw 'Module Projects must be unique and match their language.' }
        }
        $output=''
        if($reference) {
            $output=Resolve-NexusModuleOutput $reference
            $directory=if($module.language -ceq 'Cpp') { $bin } else { Join-Path $bin 'Managed' }
            if([IO.Path]::GetFileName($output) -imatch '\A(Nexus\.|LPP_)') { throw 'Module output cannot use an Engine / Hosting / Live++ reserved filename.' }
            if([IO.Path]::GetDirectoryName($output) -ine $directory -or [IO.Path]::GetExtension($output) -cne '.dll' -or -not $outputs.Add($output)) { throw 'Module output collision or invalid deployment layout.' }
        }
        $plan.Add([pscustomobject]@{id=$id;language=$module.language;primary=$primary;project=$source;output=$output})
    }
    if($Targets.PSObject.Properties['runtimeDependencies'] -and ($Targets.runtimeDependencies -isnot [Array] -or $Targets.runtimeDependencies.Count -gt 256)) { throw 'Invalid runtime dependency list.' }
    foreach($dependency in $Targets.runtimeDependencies) {
        if($dependency -isnot [string]) { throw 'Runtime dependency must be a relative DLL reference.' }
        $file=Resolve-NexusProjectReference $Root $dependency.Replace('{configuration}',$Configuration)
        $name=[IO.Path]::GetFileName($file)
        if([IO.Path]::GetExtension($file) -cne '.dll' -or $name -imatch '\A(Nexus\.|LPP_)' -or -not $outputs.Add((Join-Path $bin $name))) { throw 'Runtime dependency conflicts with a Module / Engine / Hosting DLL.' }
    }
    return $plan.ToArray()
}
