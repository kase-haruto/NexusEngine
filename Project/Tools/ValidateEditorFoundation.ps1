# Static inspection only: this script does not invoke a compiler or launch the Editor.
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectPath = Join-Path $projectRoot 'NexusEngine.vcxproj'
[xml]$projectXml = Get-Content -LiteralPath $projectPath -Raw
$namespace = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
$namespace.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$compiled = @{}
foreach ($node in $projectXml.SelectNodes('//m:ClCompile[@Include]', $namespace)) {
    $relativePath = $node.GetAttribute('Include')
    if ($compiled.ContainsKey($relativePath)) { throw "Duplicate compile item: $relativePath" }
    $compiled[$relativePath] = $true
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relativePath))) { throw "Missing compile source: $relativePath" }
}
# C++20以降のMSVCでは非推奨APIが/WXでエラーになるため、再混入を検出する。
foreach ($file in Get-ChildItem -LiteralPath $projectRoot -Recurse -File -Include '*.h', '*.cpp') {
    if ($file.FullName -match '[/\\]ThirdParty[/\\]') { continue }
    $source = Get-Content -LiteralPath $file.FullName -Raw
    if ($source -match '\bstd::filesystem::u8path\s*\(') { throw "Deprecated filesystem API: $($file.FullName)" }
}
$sourceRoots = @('Editor', 'Foundation/IPC', 'Runtime/Reflection', 'Graphics/Backend/DirectX12/Viewport')
foreach ($sourceRoot in $sourceRoots) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot $sourceRoot) -Recurse -File -Filter '*.cpp') {
        $relativePath = [System.IO.Path]::GetRelativePath($projectRoot, $file.FullName)
        if (-not $compiled.ContainsKey($relativePath)) { throw "Unregistered source: $relativePath" }
    }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Runtime') -Recurse -File) {
    if ($file.Extension -notin '.h', '.cpp') { continue }
    $source = Get-Content -LiteralPath $file.FullName -Raw
    if ($source -match '(?m)^\s*#\s*include\s*[<"][^>"\r\n]*(imgui|Editor[/\\])') { throw "Runtime UI dependency: $($file.FullName)" }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Editor/Gui/Panels') -File) {
    $source = Get-Content -LiteralPath $file.FullName -Raw
    if ($source -match '\b(ID3D12\w*|D3D12_\w*|CreateSharedHandle|OpenSharedHandle)\b') { throw "GPU API in Panel: $($file.Name)" }
}
$details = Get-Content -LiteralPath (Join-Path $projectRoot 'Editor/Gui/Panels/DetailsPanel.cpp') -Raw
if ($details -match 'GetComponent|Components[/\\]') { throw 'DetailsPanel depends on a concrete Component' }
# Styleはユーザー定義を唯一の基準とし、上位UIが標準themeへ戻さないことを確認する。
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Editor') -Recurse -File -Filter '*.cpp') {
    $source = Get-Content -LiteralPath $file.FullName -Raw
    if ($source -match 'ImGui::StyleColors(?:Dark|Light|Classic)\s*\(') { throw "Default theme overrides editor style: $($file.FullName)" }
}
foreach ($relativePath in @('Editor/Gui/Panels/OutlinerPanel.cpp', 'Editor/Gui/Panels/UtilityPanels.cpp')) {
    $source = Get-Content -LiteralPath (Join-Path $projectRoot $relativePath) -Raw
    if ($source -notmatch 'UI::SearchBar\(') { throw "Shared SearchBar not used: $relativePath" }
    if ($source -match 'ImGui::InputText\([^\r\n]*"search"') { throw "Panel has an independent search field: $relativePath" }
}
$drawers = Get-Content -LiteralPath (Join-Path $projectRoot 'Editor/Gui/PropertyDrawerRegistry.cpp') -Raw
if ($drawers -match 'ImGui::(?:Checkbox|Drag\w+|InputText|BeginCombo)\s*\(') { throw 'Property Drawer duplicates shared PropertyWidgets' }
$propertyHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'Editor/Gui/Widgets/PropertyWidgets.h') -Raw
if ($propertyHeader -match '(?m)^\s*#\s*include\s*[<"][^>"\r\n]*(imgui|Runtime[/\\]|Graphics[/\\])') { throw 'Property Widget header exposes backend or Runtime dependencies' }
$dictionaries = @{}
foreach ($language in @('en', 'ja')) {
    $dictionary = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $projectRoot "Resources/Editor/Localization/$language.lang") -Encoding utf8) {
        if ($line -notmatch '^(\S+)\s+".*"$') { throw "Malformed translation: $language / $line" }
        $translationKey = $Matches[1]
        if ($dictionary.ContainsKey($translationKey)) { throw "Duplicate translation: $language / $translationKey" }
        $dictionary[$translationKey] = $true
    }
    $dictionaries[$language] = $dictionary
}
foreach ($translationKey in $dictionaries['en'].Keys) {
    if (-not $dictionaries['ja'].ContainsKey($translationKey)) { throw "Missing Japanese translation: $translationKey" }
}
foreach ($translationKey in $dictionaries['ja'].Keys) {
    if (-not $dictionaries['en'].ContainsKey($translationKey)) { throw "Missing English translation: $translationKey" }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Editor') -Recurse -File) {
    if ($file.Extension -notin '.h', '.cpp') { continue }
    $source = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in [regex]::Matches($source, '(?:\.Text|\.Label)\("([^"\r\n]+)"')) {
        $translationKey = $match.Groups[1].Value
        if (-not $dictionaries['en'].ContainsKey($translationKey)) { throw "Missing translation key: $translationKey" }
    }
}
$metadataFiles = @(
    (Join-Path $projectRoot 'Runtime/Reflection/TypeRegistry.cpp'),
    (Join-Path $projectRoot 'Editor/Gui/MainMenuBar.cpp'),
    (Join-Path $projectRoot 'Editor/Gui/Panels/UtilityPanels.cpp')
)
foreach ($metadataFile in $metadataFiles) {
    $source = Get-Content -LiteralPath $metadataFile -Raw
    foreach ($match in [regex]::Matches($source, '"((?:component|property|menu|file|edit|panel|window|help|tools|log|runtime)\.[A-Za-z]+)"')) {
        $translationKey = $match.Groups[1].Value
        if (-not $dictionaries['en'].ContainsKey($translationKey)) { throw "Missing metadata translation: $translationKey" }
    }
}
"PASS: project XML, source registration, deprecated filesystem API check, Runtime/UI dependency, Panel GPU boundary, generic Details, shared style/widgets, $($dictionaries['en'].Count) English/Japanese translation pairs. No build or application launch performed."
