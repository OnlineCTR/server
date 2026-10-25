# OnlineCTR Server

Official Website: [http://online-ctr.com/](http://online-ctr.com/)

The dedicated server for OnlineCTR. Online races for Crash Team Racing on the PlayStation.
It hosts the rooms, relays the karts and weapons between the players, and referees the game modes.

![The OnlineCTR server's console when it starts](screenshot.png)

### Compiling

For Windows open `Server.sln` with Visual Studio 2022 (x64). The heartbeat needs libcurl, which
vcpkg supplies from `vcpkg.json` (one-time `vcpkg integrate install`); without
vcpkg, build with `/p:OctrHeartbeat=false` and the heartbeat is left out.

For [Mingw](https://www.mingw-w64.org/)/Linux/Darwin use the `CMakeLists.txt`:

```sh
cmake -S . -B build
cmake --build build
```

The heartbeat is built when CMake finds libcurl (`libcurl4-openssl-dev` on Debian/Ubuntu; on macOS
the system's own libcurl, with the Xcode Command Line Tools from `xcode-select --install` and CMake
from Homebrew, `brew install cmake`).
[Nix](https://nixos.org/download/#download-nix) users can use `default.nix` or `./build-native-gcc.nix` instead.

With MinGW-w64 on Windows, use an [MSYS2](https://www.msys2.org/) UCRT64 shell. The same two
`cmake` commands build `build/ctr_srv.exe` once the tools are in:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-curl
```

MSYS2's cmake brings curl with it, so the heartbeat is always built there, and the exe needs
libcurl's DLLs beside it: they are in `/ucrt64/bin`, and `ldd build/ctr_srv.exe` lists them. For a
self-contained exe without the heartbeat, configure with `cmake -S . -B build -DOCTR_HEARTBEAT=OFF`.

From Linux, cross-compile with Debian/Ubuntu's `gcc-mingw-w64-x86-64` and `g++-mingw-w64-x86-64`:

```sh
cmake -S . -B build-win -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build-win
```

That leaves the heartbeat out: the build machine's own Linux libcurl is no use for Windows, and
CMake says so. For the heartbeat, unzip curl's own MinGW build (`curl-<version>-win64-mingw.zip`
from <https://curl.se/windows/>) and add `-DCMAKE_PREFIX_PATH=<that folder>` to the first command,
in a fresh build folder; then put its `bin/libcurl-x64.dll` beside the exe, the only DLL it needs.

A MinGW build without the heartbeat needs no DLL beyond Windows' own. `run.ps1` starts
`Server.exe`: rename the exe, or change the script.

You can also serve via Docker with the following command (run from this folder), once
`octr-server.env` exists (see Configuration):

```sh
docker compose up --build
```

### Running

```sh
./ctr_srv -p 64001
```

The port is UDP; without `-p` the server asks for one, and Enter alone takes 5501 (`DEFAULT_PORT`
in `SV_config.h`). `--banlist <file>` reads IPv4 addresses to refuse, one per line, in a plain text
or UTF-8 file. Blank lines and lines starting with `#` are ignored, and so is a `#` note after an
address. Ranges such as `10.0.0.0/8` are not supported; the log names every line it skipped.
`--nospectate` stops players from joining a race in progress as spectators (on by default:
`SPECTATE_ENABLED` in `SV_config.h`).

### Configuration

Nothing secret is in this repository, and nothing is compiled in. Each host keeps its own
settings in `octr-server.env`, which git ignores:

```sh
cp octr-server.env.example octr-server.env
chmod 600 octr-server.env
```

Docker reads it through `compose.yml`; under systemd, point `EnvironmentFile=` at it. It sets the
heartbeat - the server's status on the site and in the launcher: the site's address, this
server's ID and its own secret (issued per server by the OnlineCTR team), and the key all
servers share for the site's "seen in the past 7 days" count. With no address or no secret the
heartbeat is off and the server runs unlisted.

The server's own settings - connections per address, the lobby and race clocks, limits - are in
`SV_config.h`.

### Running on Windows Servers

Build `Server.sln` (Release, x64) from a release tag, then copy these into one folder on the server,
for example `C:\OnlineCTR`:

- `Server.exe`, `libcurl.dll` and `z.dll` from `x64\Release`
- `run.ps1` from this repository
- `octr-server.env`, made from `octr-server.env.example` (see Configuration)

The server also needs the Microsoft Visual C++ 2015-2022 Redistributable (x64), which most Windows
installs already have. Allow its UDP port through Windows Firewall once (as an administrator):

```powershell
New-NetFirewallRule -DisplayName "OnlineCTR server" -Direction Inbound -Protocol UDP -LocalPort 64001 -Action Allow
```

Then start it with your port:

```powershell
powershell -ExecutionPolicy Bypass -File C:\OnlineCTR\run.ps1 -Port 64001
```

`run.ps1` loads the settings from `octr-server.env`, the same file the Linux hosts use, and starts
`Server.exe` on that port (64001 when no `-Port` is given). With the heartbeat set up, the console
shows `Heartbeat: Enabled (updating as "<your server ID>" ...)`.

### Automatic Updates (Linux)

`autodeploy.sh` keeps a Docker host on the newest release. Every few minutes it checks GitHub for a
new release tag (`v1113`, `v1114`, ...) signed by the OnlineCTR team, checks the code out at exactly
that tag, and rebuilds and restarts the server. Releases are tags, not `main`: `main` can be ahead
of what players are running. Set up the release key first (Signed releases, below): without it the
script deploys nothing.

Setups that follow a branch rather than a tag (a Nix input, for example) should use `stable`: it
always points at the newest release.

Copy the script out of the server's folder and run the copy, giving it that folder. Inside the
folder, checking out an older release would delete it and a newer release would refuse to
overwrite it.

```sh
cp autodeploy.sh ~/octr-autodeploy.sh
bash ~/octr-autodeploy.sh /home/ubuntu/ctronline        # first deploy (your server folder)
crontab -e
```

Then add this line to the crontab (with your server folder):

```
*/5 * * * * flock -n /tmp/octr-autodeploy.lock bash $HOME/octr-autodeploy.sh /home/ubuntu/ctronline >> $HOME/octr-autodeploy.log 2>&1
```

- `octr-server.env` must be in the server folder first (see Configuration).
- The script runs `sudo docker compose`, so the user needs sudo without a password (Ubuntu cloud
  servers' `ubuntu` user has it). Otherwise add the user to the `docker` group and remove `sudo`
  from your copy.
- Local edits in the server folder stop the checkout: undo them with `git checkout -- .`.
- `--force` after the folder redeploys the current release, if it is a signed one and its tag still
  exists; otherwise the log says why not.
- A deploy restarts the server, ending any races in progress.
- A host already running an older copy of the script keeps that copy, which checks no signatures:
  take the new copy from the release tag to get the check (Signed releases, "Hosts, once").

#### Signed releases

`autodeploy.sh` deploys a tag only when it is a signed tag under its own name with a good signature
from the OnlineCTR team's release key, named by its fingerprint in `RELEASE_KEY_FINGERPRINT` at the
top of the script. It looks at the release tags from the newest down and deploys the first one that
passes; any above it that do not are named in the log on every run. It never goes back to a release
older than the one deployed. Without this, anyone who could push a tag to GitHub, with a stolen
token say, would have every server rebuild from it, as root, within five minutes. The fingerprint is
pinned in each host's own copy of the script, so a change to the script on GitHub does not change
what an existing host trusts.

Signing starts with `v1113`. The tags before it are unsigned, and the script never deploys them.
The release key's public half is `octr-release-key.asc` in this repository, fingerprint
`67A0 CFA6 CD94 39CA 1C10  E3B3 94BD 15F6 A659 1B53`; check it against the one the team posts on
the Discord before trusting it.

Maintainers, once: make a signing key with no expiry date, so that hosts never stop deploying when
it lapses, and keep its revocation certificate offline. Put its whole fingerprint in
`RELEASE_KEY_FINGERPRINT`, and publish the public key and the fingerprint where hosts can find them
(the Discord, the site). Keep the key away from GitHub tokens and CI secrets: the check only helps
while a stolen GitHub credential does not come with the signing key.

```sh
gpg --quick-gen-key "OnlineCTR release key <336368996+OnlineCTR@users.noreply.github.com>" ed25519 sign never
gpg --fingerprint "OnlineCTR release key"
git config --global user.signingkey <fingerprint>
gpg --armor --export <fingerprint> > octr-release-key.asc
```

Then sign every release tag (a placeholder build number here):

```sh
git tag -s v<build> -m "OnlineCTR server, build <build>"
git push origin v<build>
```

Never move, re-sign or delete and recreate a release tag once it is pushed. Every host's fetch then
fails on every run; each would have to run `git fetch --tags --force origin` once by hand. A key
that has to be replaced means a new fingerprint in every host's copy of the script.

Hosts, once, as the user that runs the cron job, before the first deploy (install `gpg` first if
the server has none: `sudo apt install gnupg`):

```sh
git -C /home/ubuntu/ctronline fetch --tags origin
git -C /home/ubuntu/ctronline show v<build>:octr-release-key.asc | gpg --import   # the newest release
gpg --fingerprint "OnlineCTR release key"          # must match the fingerprint the team published
git -C /home/ubuntu/ctronline verify-tag v<build>  # "Good signature"
git -C /home/ubuntu/ctronline show v<build>:autodeploy.sh > ~/octr-autodeploy.sh
grep RELEASE_KEY_FINGERPRINT= ~/octr-autodeploy.sh  # the same fingerprint
```

If the fetch stops with "would clobber existing tag" (a release tag that was moved on GitHub in the
past), run `git -C /home/ubuntu/ctronline fetch --tags --force origin` once, then carry on.

The script is taken from the tag rather than copied out of the folder: until the old copy has
deployed the new release, the folder still holds the old script. From `v1113` on it has the
fingerprint filled in already: check that it is the one the team published. A warning that the key
"is not certified with a trusted signature" is expected and harmless: the script checks the
fingerprint itself.

On GitHub, add a tag ruleset for `v*` (Settings > Rules > Rulesets) so only the release maintainers
can create, move or delete release tags, and require two-factor authentication for the
organisation, which stops a stolen password from being used to sign in and make new tokens or keys.
Neither stops a maintainer's existing token or SSH key from pushing a tag if it leaks, since a git
push never asks for the second factor: keep tokens fine-grained, limited to this repository and
short-lived. The signature check is what keeps such a tag from being deployed.

### Versions

`global.h` is the protocol header the server shares with the game (ROM) and the PC client. It
carries two numbers. `VERSION` is the build and steps every release. `SERVER_VERSION` is what the
client and server check each other on, and steps only when a release changes the protocol: a
message struct or type in `global.h`, what the server does in reply to one, or the seats in a room
(`MAX_NUM_PLAYERS`, though no message changes size). A release that leaves `SERVER_VERSION` alone
needs no server update; every server already running keeps accepting the new client. Both show in
the server's console banner, as `Build 1114, Server 1114` for this release.

`SV_main.c` and `global.h` always come from the same release: `SV_main.c` names the protocol it
was written for (`SV_PROTOCOL`), and a `global.h` from another release refuses to build with it.

### Licence

AGPL-3.0 (`LICENSE`). ENet, bundled in `enet/`, is MIT-licensed (`enet/LICENSE`).

You must follow the license terms and additional agreement in `ADDITIONAL-TERMS`

### Credits

Engine, core, server, launcher, website and overall design overhauled in 2026 by Matt.
Special thanks to Niko. Extra thanks to ClaudioBo, BlizzrdBall and PedroHLC.

Built on [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK). Original Design and Source Code Copyright (C) Naughty Dog.

OnlineCTR Copyright (C) 2024-2026 OnlineCTR Team.
