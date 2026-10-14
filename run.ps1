# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

# OnlineCTR server on Windows: starts Server.exe with this host's settings from
# octr-server.env (the same file the Linux/Docker hosts use; see
# octr-server.env.example). Keep this script next to Server.exe, libcurl.dll,
# z.dll and octr-server.env, then run:
#
#   powershell -ExecutionPolicy Bypass -File run.ps1 -Port 64001

param([int]$Port = 64001)

Set-Location -LiteralPath $PSScriptRoot

# octr-server.env: one OCTR_...=value per line; # starts a comment. An empty
# value leaves the setting unset.
foreach ($line in Get-Content -LiteralPath (Join-Path $PSScriptRoot 'octr-server.env')) {
    if ($line -match '^\s*(OCTR_[A-Z0-9_]+)\s*=\s*(.*?)\s*$' -and $Matches[2] -ne '') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

& (Join-Path $PSScriptRoot 'Server.exe') -p $Port
