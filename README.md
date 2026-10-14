# OnlineCTR Server

Official Website: [http://online-ctr.com/](http://online-ctr.com/)

The dedicated server for OnlineCTR. Online races for Crash Team Racing on the PlayStation.
It hosts the rooms, relays the karts and weapons between the players, and referees the game modes.

### Compiling

For Windows open `Server.sln` with Visual Studio 2022 (x64). The heartbeat needs libcurl, which
vcpkg supplies from `vcpkg.json` (one-time `vcpkg integrate install`); without
vcpkg, build with `/p:OctrHeartbeat=false` and the heartbeat is left out.

For [Mingw](https://www.mingw-w64.org/)/Linux/Darwin use the `CMakeLists.txt`:

```sh
cmake -S . -B build
cmake --build build
```

The heartbeat is built when CMake finds libcurl (`libcurl4-openssl-dev` on Debian/Ubuntu).
[Nix](https://nixos.org/download/#download-nix) users can use `default.nix` or `./build-native-gcc.nix` instead.

You can also serve via Docker with the following command (run from this folder), once
`octr-server.env` exists (see Configuration):

```sh
docker compose up --build
```

### Running

```sh
./ctr_srv -p 64001
```

The port is UDP; without `-p` the server asks for one. `--banlist <file>` reads IPv4 addresses
to refuse, one per line.

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
servers share for the site's "seen in the past 24h" count. With no address or no secret the
heartbeat is off and the server runs unlisted.

The server's own settings - connections per address, the lobby and race clocks, limits - are in
`SV_config.h`.

### Versions

`global.h` is the protocol header the server shares with the game (ROM) and the PC client. It
carries two numbers. `VERSION` is the build and steps every release. `SERVER_VERSION` is what the
client and server check each other on, and steps only when a release changes the protocol: a
message struct or type in `global.h`, what the server does in reply to one, or the seats in a room
(`MAX_NUM_PLAYERS`, though no message changes size). A release that leaves `SERVER_VERSION` alone
needs no server update; every server already running keeps accepting the new client. Both show in
the server's console banner as `Build 1103, Server 1103`.

### Licence

GPL-3.0 (`LICENSE`). ENet, bundled in `enet/`, is MIT-licensed (`enet/LICENSE`).

You must follow the license terms and additional agreement in `ADDITIONAL-TERMS`

### Credits

Engine, core, server, launcher, website and overall design overhauled in 2026 by Matt.
Special thanks to Niko. Extra thanks to ClaudioBo, BlizzrdBall and PedroHLC.

Built on [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK). Original Design and Source Code Copyright (C) Naughty Dog.

OnlineCTR Copyright (C) 2024-2026 OnlineCTR Team
