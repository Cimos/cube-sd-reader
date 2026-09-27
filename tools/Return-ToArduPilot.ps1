# Run elevated. The Python helper verifies disk/COM identity before locking the volume.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z]:$')][string]$Drive,
 [string]$Python='python'
)
$ErrorActionPreference='Stop'
& $Python (Join-Path $PSScriptRoot 'windows_eject.py') --port $Port --drive $Drive
if ($LASTEXITCODE -ne 0) { throw 'Eject or bootloader verification failed; inspect the helper output.' }
