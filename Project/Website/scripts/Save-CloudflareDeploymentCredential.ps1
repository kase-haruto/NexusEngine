[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{32}$')][string]$AccountId)

$ErrorActionPreference = 'Stop'
$token = Read-Host 'Cloudflare API token' -AsSecureString
if ($token.Length -eq 0) { throw 'Enter a Cloudflare API token.' }
$directory = Join-Path $env:LOCALAPPDATA 'NexusEngine/Release'
[IO.Directory]::CreateDirectory($directory) | Out-Null
# Windows PowerShell encrypts the credential for this Windows user and machine.
[PSCredential]::new($AccountId, $token) | Export-Clixml -LiteralPath (Join-Path $directory 'CloudflareDeployment.xml')
Write-Output 'Saved the Cloudflare deployment credential for this Windows user.'
