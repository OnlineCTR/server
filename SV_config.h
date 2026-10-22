// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

// SV_config.h - the server's own settings: connections, clocks, limits.
//
// Everything here is public: nothing secret, no addresses. What differs per
// host - the heartbeat's site address, this server's key and name - is read
// at run time from the environment (octr-server.env, from
// octr-server.env.example; SV_heartbeat.h), never compiled in. Any value
// here can be overridden at build time (-DNAME=value) without editing this
// file. What the clients must agree on - the seats, the protocol's numbers,
// the vote's and the kick ban's seconds, the votes a kick needs - is in
// global.h, shared with the ROM and the PC client, not here.

#ifndef SV_CONFIG_H
#define SV_CONFIG_H

// ------------------------------------------------------------ connections

// Connections allowed from one IP address at a time. One over it is refused
// at the CONNECT event, at once and logged (CountPeersLPL_FromHost); ENet
// itself refuses a handshake past one more than this (duplicatePeers, set at
// start-up), so half-open handshakes cannot pile up from one address either.
#ifndef MAX_PEERS_PER_IP
#define MAX_PEERS_PER_IP 12
#endif

// The kicks held at once (KickBan_*, SV_main.c): the addresses of the players
// a room voted out (VOTE TO KICK, KickVote_*; the host's KICK PLAYER up to
// 1109), each kept out of that room for KICK_BAN_SECONDS (global.h).
#ifndef KICK_BANS
#define KICK_BANS 64
#endif

// The port the prompt offers when no -p is given, taken by Enter alone
// (AskPort, SV_main.c). Above 1023, which needs root on Linux and macOS,
// and clear of the blocks Windows reserves from 49152 up for Hyper-V, WSL
// and Docker (a player's 64001 was in one, 16 Oct 2026) and of Linux's
// ephemeral ports (32768 to 60999). The launcher's Help > Private Server
// recommends the same. The official servers keep theirs, given with -p.
#ifndef DEFAULT_PORT
#define DEFAULT_PORT 5501
#endif

// ----------------------------------------------------------------- clocks

// Seconds a finished room sits on the results screen before everybody is sent
// back to the lobby. Travels to the clients in SG_LOBBYCOUNTDOWN so the game
// counts down the same number; this define is the only place it lives.
#ifndef RACE_OVER_COUNTDOWN_SECONDS
#define RACE_OVER_COUNTDOWN_SECONDS 10
#endif

// Lobby-return timer. Starts when the FIRST racer crosses the line; if anyone
// is still out on the track when it runs out, the whole room is sent back to
// the lobby anyway, so nobody sits through several minutes of somebody else's
// slow laps. Once everybody has finished this no longer applies - the
// RACE_OVER_COUNTDOWN_SECONDS results countdown takes over from there.
// Announced to the clients (SG_LOBBYCOUNTDOWN) the moment it starts, so the
// finished can see how long they wait and the rest how long they have.
#ifndef LOBBY_RETURN_SECONDS
#define LOBBY_RETURN_SECONDS 60
#endif

// Lobby idle budget: how long, in total, a player may spend on their picks
// in one lobby round. The clock runs only while the player has a pick to
// make - the host's from the moment they hold the role, through the track,
// lap and character picks; a guest's from the moment the track is in and
// there is a character to pick - and pauses otherwise, so stepping back or
// unlocking never refills it. Whoever runs out is disconnected, which also
// turns over a host who has wandered off: the next player is promoted and
// their own clock starts. Goes to the clients in SG_IDLECOUNTDOWN, so this
// define is the only place it lives. And no clock runs - this one or the
// vote's - while the room holds one player (Room_Solo, Matt's call, 1102):
// there is nobody to wait on, so nobody is removed for it; the clocks start
// when a second player comes in, and pause again if the room is left to one.
#ifndef LOBBY_IDLE_SECONDS
#define LOBBY_IDLE_SECONDS 60
#endif

// A room closes to newcomers in the last LOBBY_JOIN_CLOSE_SECONDS of anybody's
// running pick clock (RoomOpenForJoin, SV_main.c): that player is about to
// be removed or the race is about to load, and a newcomer would only restart
// the wait.
#ifndef LOBBY_JOIN_CLOSE_SECONDS
#define LOBBY_JOIN_CLOSE_SECONDS 10
#endif

// How long a room waits on its players to load once the race is started
// (SG_STARTLOADING). Whoever has not reached the start line by then
// (CG_STARTRACE) is removed as an idle player is (DISCONNECT_REASON_IDLE),
// and the race goes on for the rest. A normal load - the start countdown,
// the track off the disc, its fly-in - takes a fraction of this. Without it
// one player who never loaded held the whole room on WAITING FOR PLAYERS
// until they left: the start is sent once, and on 16 Oct 2026 a player's
// CANCEL undid it on their machine.
#ifndef LOAD_TIMEOUT_SECONDS
#define LOAD_TIMEOUT_SECONDS 60
#endif

// ------------------------------------------------- a race in progress watched

// A race in progress can be watched (1110, Spectator_*, SV_main.c): a player
// on the room list picks a room that is racing - past its green, not over,
// with a seat that was empty when the race loaded - and is seated in it as a
// spectator, kept out of the race and its rules, caught up once its own load
// is done, and a player of the room from its reset on. 0 here, or
// --nospectate at launch, turns it off: no room is ever offered to watch
// (SG_ROOMS' spectateMask is 0) and a join that asks is refused as a closed
// room's.
#ifndef SPECTATE_ENABLED
#define SPECTATE_ENABLED 1
#endif

// No race is offered to watch in the last SPECTATE_CLOSE_SECONDS before the
// lobby-return timer forces the room back (LOBBY_RETURN_SECONDS, from the
// first finish): a spectator's own load takes a good part of that, and one
// arriving as the room resets would come into a lobby still loading.
#ifndef SPECTATE_CLOSE_SECONDS
#define SPECTATE_CLOSE_SECONDS 20
#endif

// A spectator that has not reached its race SPECTATE_LOAD_SECONDS after it
// sat down - its name never came, or its load never ended - is removed as an
// idle player is (DISCONNECT_REASON_IDLE), so its seat is not held for good.
// A load takes a fraction of it, as LOAD_TIMEOUT_SECONDS says.
#ifndef SPECTATE_LOAD_SECONDS
#define SPECTATE_LOAD_SECONDS 60
#endif

// ------------------------------------------------- contested weapon touches

// A touch marked as waiting is held (WeaponHold_*, SV_main.c) until a touch
// by another player as early could have come in: the slowest other player's
// one way, a client step and a margin, never longer than HOLD_MAX_MS.
#ifndef HOLD_STEP_MS
#define HOLD_STEP_MS   34    // a client's step: its send waits for the ROM's (CL_main.cpp FrameStall)
#endif
#ifndef HOLD_MARGIN_MS
#define HOLD_MARGIN_MS 10
#endif
#ifndef HOLD_MAX_MS
#define HOLD_MAX_MS    250   // never longer: a player that slow is not waited on
#endif

// -------------------------------------------------------------- heartbeat

// Seconds between posts to the site, unless OCTR_HEARTBEAT_INTERVAL says
// otherwise - and never fewer than the minimum: a tight interval would hammer
// the site and spend the worker's life in TLS handshakes.
#ifndef HEARTBEAT_INTERVAL_SECONDS
#define HEARTBEAT_INTERVAL_SECONDS     30
#endif
#ifndef HEARTBEAT_INTERVAL_MIN_SECONDS
#define HEARTBEAT_INTERVAL_MIN_SECONDS 5
#endif

// At most this many distinct players are tracked (and sent per heartbeat);
// beyond it the least recently seen record is reused.
#ifndef OCTR_HEARTBEAT_SEEN_MAX
#define OCTR_HEARTBEAT_SEEN_MAX 512
#endif

// ------------------------------------------------------------------ limits
//
// What one connection may have the server hold, and send it. Set on the ENet
// host at start-up (ServerState_FirstBoot, SV_main.c) and in the message
// handler (Rate_*, SV_main.c).

// The largest packet ENet takes from, or sends to, a connection. ENet's own
// default is 32 MB, sized for file transfers, and a fragmented packet is set
// aside in full when its first fragment arrives: with the default, any
// connection could have the server allocate 32 MB for packets this game
// never sends. The largest message in the protocol is 18 bytes (global.h);
// every message struct is checked against this at compile time (SV_main.c,
// beside SG_BUFFER_BYTES), so one that outgrows it fails the build, not the
// wire.
#ifndef MAX_PACKET_BYTES
#define MAX_PACKET_BYTES 64
#endif

// Received data ENet may queue for one connection before the tick reads it
// (ServerState_Tick runs about every millisecond). Past this, that
// connection's further packets are dropped until the queue drains - a
// reliable one is sent again by its sender. 4 KB is over 200 kart messages.
// ENet's default is 32 MB.
#ifndef MAX_WAITING_BYTES
#define MAX_WAITING_BYTES 4096
#endif

// Connections the host holds at once: every seat in the 16 rooms, and the
// rest on the room list. Was 16 x 12 + 128 = 320; a slot is about a
// kilobyte, and a server with every slot taken is one nobody can join
// (ENet's own ceiling is 4095).
#ifndef MAX_PEERS
#define MAX_PEERS 1024
#endif

// Messages a connection may send, by kind (Rate_*, SV_main.c): the kart
// message (a client sends 30 a second, 60 with the 60 FPS disc), the weapon
// messages (CG_WEAPON, CG_WEAPONEVENT: they come in bursts) and everything
// else (the lobby's: a join, a name, a pick, a vote - and an empty message
// or one of a type no client sends). Each kind is a bucket: PER_SEC tokens a
// second flow in, up to BURST. A message takes a token, and one that finds
// none is dropped. The bursts are what a stall can bunch up: ENet hands a
// player's reliable messages over all at once when the one they were
// waiting on finally arrives, and every one of them has to get through (the
// lobby's: about 90 at most, the fastest a player can press, over a stall
// short of ENet's 5 s timeout). A connection with FLOOD_DROPS_PER_SEC
// dropped in one counted second (Rate_Take) is a flood, not a player, and is
// disconnected. Ranges, checked as the server compiles: PER_SEC 1 to BURST
// (0 does not turn a limit off), BURST up to 1000000, FLOOD_DROPS_PER_SEC 1
// to 65535.
#ifndef RATE_KART_PER_SEC
#define RATE_KART_PER_SEC 150
#endif
#ifndef RATE_KART_BURST
#define RATE_KART_BURST 300
#endif
#ifndef RATE_EVENT_PER_SEC
#define RATE_EVENT_PER_SEC 60
#endif
#ifndef RATE_EVENT_BURST
#define RATE_EVENT_BURST 240
#endif
#ifndef RATE_LOBBY_PER_SEC
#define RATE_LOBBY_PER_SEC 30
#endif
#ifndef RATE_LOBBY_BURST
#define RATE_LOBBY_BURST 120
#endif
#ifndef FLOOD_DROPS_PER_SEC
#define FLOOD_DROPS_PER_SEC 300
#endif

// The room list goes to every connection on the server when a room changes
// (a join, a leave, a track picked or cancelled, a race's load, start or end,
// a kick's end). The pushes are gathered per tick (RoomList_Flush), and at
// most one goes out every this many milliseconds: a host toggling the track
// pick would otherwise have a list sent to everybody for every toggle.
#ifndef ROOM_LIST_MIN_MS
#define ROOM_LIST_MIN_MS 100
#endif

#endif
