# Managed Runtimeの選択と、対応するBuild / Metadata実行だけを共通化する。
# World / Script instanceの所有やEditorの操作状態は扱わない。
function Get-NexusManagedRuntime([object]$Targets) {
    $runtime=if($Targets.PSObject.Properties['managedRuntime']) { $Targets.managedRuntime } else { 'CoreCLR' }
    if($runtime -cnotin @('Mono','CoreCLR')) { throw 'managedRuntime must be Mono or CoreCLR.' }
    return $runtime
}
function Get-NexusManagedArguments([string]$Runtime) {
    if($Runtime -ceq 'Mono') {
        # Game側の既存csprojも、Engine APIと同じframeworkへ明示的にtargetする。
        return @('-p:NexusManagedRuntime=Mono','-p:TargetFramework=net48','-p:LangVersion=latest','-p:GenerateDependencyFile=true')
    }
    return @('-p:NexusManagedRuntime=CoreCLR','-p:TargetFramework=net10.0')
}
function Invoke-NexusMetadataTool([string]$Runtime,[string]$Tool,[string]$Assembly,[string]$Output) {
    if($Runtime -ceq 'Mono') {
        $mono=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($Tool))) 'Mono/bin/mono.exe'
        if(-not [IO.File]::Exists($mono)) { throw 'The deployed Mono Runtime is missing beside the Metadata tool.' }
        & $mono ([IO.Path]::ChangeExtension($Tool,'.exe')) $Assembly $Output
    } else { & dotnet $Tool $Assembly $Output }
    if($LASTEXITCODE -ne 0) { throw 'Managed Metadata export failed.' }
}
