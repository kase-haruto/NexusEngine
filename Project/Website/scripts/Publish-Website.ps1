[CmdletBinding()]
param(
    [uri]$SiteOrigin = 'https://nexus-engine.nexus-engine-website.workers.dev',
    [string]$ReleaseRoot,
    [string]$LauncherOutput,
    [string]$LauncherSetup
)
$ErrorActionPreference = 'Stop'
$siteRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repository = [IO.Path]::GetFullPath((Join-Path $siteRoot '../..'))
if($SiteOrigin.Scheme -ne 'https' -or $SiteOrigin.UserInfo -or $SiteOrigin.AbsolutePath -ne '/' -or $SiteOrigin.Query -or $SiteOrigin.Fragment) { throw 'SiteOrigin must be the HTTPS origin only.' }
if(-not $ReleaseRoot) { $ReleaseRoot = Join-Path $repository 'Generated/EngineReleases' }
if(-not $LauncherOutput) { $LauncherOutput = Join-Path $repository ('Generated/LauncherDistribution/Website-' + [guid]::NewGuid().ToString('N')) }
$previousRoot = $env:NEXUS_RELEASE_ROOT
$previousSetup = $env:NEXUS_LAUNCHER_SETUP
$previousOrigin = $env:NEXUS_SITE_ORIGIN
$previousCloudflareToken = $env:CLOUDFLARE_API_TOKEN
$previousCloudflareAccount = $env:CLOUDFLARE_ACCOUNT_ID
Push-Location $siteRoot
try {
    # CI can supply its own credentials. Locally, use the encrypted Windows credential if configured.
    if (!$env:CLOUDFLARE_API_TOKEN -and $env:LOCALAPPDATA) {
        $credentialPath = Join-Path $env:LOCALAPPDATA 'NexusEngine/Release/CloudflareDeployment.xml'
        if (Test-Path -LiteralPath $credentialPath -PathType Leaf) {
            $credential = Import-Clixml -LiteralPath $credentialPath
            $env:CLOUDFLARE_ACCOUNT_ID = $credential.UserName
            $env:CLOUDFLARE_API_TOKEN = $credential.GetNetworkCredential().Password
        }
    }
    if(-not $LauncherSetup) {
        & (Join-Path $repository 'Project/Tools/Project/Build-Distribution.ps1') -CatalogUrl ([uri]::new($SiteOrigin, '/releases.json')) -OutputDirectory $LauncherOutput
        if($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
        $LauncherSetup = Join-Path $LauncherOutput 'Setup/launcherSetup.exe'
    }
    $env:NEXUS_RELEASE_ROOT = [IO.Path]::GetFullPath($ReleaseRoot)
    $env:NEXUS_LAUNCHER_SETUP = (Resolve-Path -LiteralPath $LauncherSetup).Path
    $env:NEXUS_SITE_ORIGIN = $SiteOrigin.AbsoluteUri
    & node scripts/build.mjs
    if($LASTEXITCODE -ne 0) { throw 'Website build failed.' }
    & node --test 'tests/*.test.mjs'
    if($LASTEXITCODE -ne 0) { throw 'Website tests failed.' }
    & node scripts/github-releases.mjs
    if($LASTEXITCODE -ne 0) { throw 'GitHub release publication failed.' }
    & node scripts/build.mjs
    if($LASTEXITCODE -ne 0) { throw 'Published catalog generation failed.' }
    & node node_modules/wrangler/bin/wrangler.js deploy
    if($LASTEXITCODE -ne 0) { throw 'Website deployment failed.' }
    & node scripts/verify-public.mjs
    if($LASTEXITCODE -ne 0) { throw 'Public download verification failed.' }
    Write-Output "Published and verified: $($SiteOrigin.AbsoluteUri)"
} finally {
    $env:NEXUS_RELEASE_ROOT = $previousRoot
    $env:NEXUS_LAUNCHER_SETUP = $previousSetup
    $env:NEXUS_SITE_ORIGIN = $previousOrigin
    $env:CLOUDFLARE_API_TOKEN = $previousCloudflareToken
    $env:CLOUDFLARE_ACCOUNT_ID = $previousCloudflareAccount
    Pop-Location
}
