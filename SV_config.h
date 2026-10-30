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

// The main loop's wait on the network, in milliseconds (ProcessNewMessages,
// SV_main.c; next release - until then it slept a microsecond between
// passes, about 17,000 a second on Linux, which had a server nobody was on
// use 11% of a desktop's core and 28% of a one-core VPS). A packet ends the
// wait the moment it arrives, so this is not how long a message waits: it is
// how late, at most, a clock is seen to run out while nothing comes in - a
// vote's, a load's, a held weapon touch's, and ENet's own resends and pings,
// which move on only while the server services the network - and how long a
// stop (SIGTERM) can take. This one while anybody is connected; with nobody
// connected, NET_IDLE_WAIT_MS (below). 1: the resolution of those clocks
// (enet_time_get) and the pass a Windows server already ran (its timer's
// period, which ENet sets to 1 ms). A longer wait does save CPU: a quiet
// pass's cost is ENet's walk over every one of the host's MAX_PEERS slots,
// taken or not, each time the server services the network
// (enet_protocol_send_outgoing_commands, enet/protocol.c), so a quiet
// server's CPU goes with how many passes it makes (the numbers are at
// NET_IDLE_WAIT_MS) - but it would have those clocks run late for everybody
// playing. 1 to 10, checked as the server compiles.
#ifndef NET_WAIT_MS
#define NET_WAIT_MS 1
#endif

// The same wait while nobody is connected (ENet's connectedPeers is 0;
// ProcessNewMessages, SV_main.c, says which connections it counts; next
// release). Longer, because then no room has anybody to play, and a
// connecting player's first packet ends the wait at once, as any packet
// does. What it can make late is only the server's own housekeeping - a kick
// ban's end, the heartbeat's sample (once a second) - ENet's resend of a
// handshake's or a disconnect's step, and a stop (SIGTERM) whose signal
// misses the wait. Measured on an i9 under WSL (runs on three occasions), a
// server nobody was on used about 1.2 to 3% of a core with the 1 ms wait
// and 0.2 to 0.7% with 10 ms (11.6% with v1119's microsecond sleep), most
// of it ENet's walk over the slots: a tenth of the passes, each dearer after
// the longer sleep. NET_WAIT_MS to 100, checked as the server compiles.
#ifndef NET_IDLE_WAIT_MS
#define NET_IDLE_WAIT_MS 10
#endif

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

// ---------------------------------------- one warp orb on the track (BALANCED)

// One warp orb on the track at a time in a BALANCED race in a room that
// rolls items (Matt, 30 Oct 2026; Orb_*, SV_main.c). A go-ahead is its
// asker's to use for their round trip and ORB_GRANT_MARGIN_MS - the GO is
// half a round trip on its way, the ROM takes it up to 5 s after its ask and
// fires within a second of it once the kart can (ORB_ASK_STEPS,
// ORB_FIRE_STEPS: netWeapons.c), the shot is half a round trip on its way
// back - never longer than ORB_GRANT_MAX_MS; unused by then it is called off
// (NET_ORB_CANCEL to the room) and the track is free again. Both outlast the
// ROM's 6 s: its own client drops the server's CANCEL as its echo, so a
// go-ahead called off any sooner could still be fired, on nobody else's
// screen. A ROM that cannot use its go-ahead calls it off itself at once.
// An orb shot is taken as out until its end is relayed, ORB_LIVE_MAX_MS at
// most (an end never heard: its shot refused on its own machine, its copies
// gone quietly) - the ROM's own safety clock on it runs a few seconds longer
// - and ORB_LEFT_MS more at most once its owner has left (its copies fly on
// and can still hit).
#ifndef ORB_GRANT_MARGIN_MS
#define ORB_GRANT_MARGIN_MS 7000
#endif
#ifndef ORB_GRANT_MAX_MS
#define ORB_GRANT_MAX_MS    8000
#endif
#ifndef ORB_LIVE_MAX_MS
#define ORB_LIVE_MAX_MS     60000
#endif
#ifndef ORB_LEFT_MS
#define ORB_LEFT_MS         15000
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
// 2048 since 1114 (a week of players; 512 for a day): about 39 KB of
// hashes a heartbeat at most - the site's OCTR_MAX_BODY allows 48 KB.
#ifndef OCTR_HEARTBEAT_SEEN_MAX
#define OCTR_HEARTBEAT_SEEN_MAX 2048
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
// never sends. The largest message in the protocol is the 68-byte join
// (CG_MessageRoom with its signature, 1115; 34 bytes before it, the room
// list with its nonce); every message struct is checked against this at
// compile time (SV_main.c, beside SG_BUFFER_BYTES), so one that outgrows it
// fails the build, not the wire.
#ifndef MAX_PACKET_BYTES
#define MAX_PACKET_BYTES 72
#endif

// Received data ENet may queue for one connection before the tick reads it
// (ServerState_Tick runs about every millisecond while anybody is connected,
// NET_WAIT_MS). Past this, that connection's further packets are dropped
// until the queue drains - a reliable one is sent again by its sender. 4 KB
// is over 200 kart messages.
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

// The signed join (1115; global.h's OCTR_AUTH_*, SV_auth.c). The release's
// client public key: every join must carry a signature the launcher made
// with the matching private key, which only the OnlineCTR team holds. A new
// pair with a SERVER_VERSION as a rule (tools/Launcher/make_key.py in the
// game's tree prints the two lines), or the last release's kept - 1121 kept
// 1119's, so 1119's launchers sign its joins (Matt, 31 Oct 2026: no new
// launcher); OCTR_CLIENT_PUBKEY_SERVER_VERSION names the release either way.
// A server built with another release's key refuses every join, so this
// header and global.h come from one release.
// Running with OCTR_OPEN_SERVER=1 in the environment takes unsigned joins
// too (a private server for a client built by hand): said at start-up,
// and never on a public server.
#define OCTR_CLIENT_PUBKEY_VERSION 1119
#define OCTR_CLIENT_PUBKEY_SERVER_VERSION 1121   // the release this file is for (SV_auth.c checks it against SERVER_VERSION): 1121 kept 1119's key (Matt, 31 Oct 2026: no new launcher)
#define OCTR_CLIENT_PUBKEY { 0x87, 0xe2, 0xf0, 0x5a, 0x85, 0xac, 0x93, 0xbe, 0xbb, 0xe3, 0x5d, 0xf1, 0xef, 0x23, 0x42, 0x19, 0x65, 0x73, 0x3c, 0xff, 0xac, 0xe8, 0x5f, 0x9c, 0x92, 0x67, 0x33, 0x28, 0xb2, 0x63, 0xad, 0x59 }

// A join refused for its signature counts against its address: this many
// within AUTH_FAIL_WINDOW_SECONDS and the address is barred from the server
// (its connections refused at CONNECT, as a banned address's) for
// AUTH_BAR_SECONDS. An official client never fails: its launcher signs every
// join. The table holds AUTH_BARS addresses; past that the one with the
// least time left goes first.
#ifndef AUTH_FAILS_MAX
#define AUTH_FAILS_MAX 3
#endif
#ifndef AUTH_FAIL_WINDOW_SECONDS
#define AUTH_FAIL_WINDOW_SECONDS 60
#endif
#ifndef AUTH_BAR_SECONDS
#define AUTH_BAR_SECONDS 600
#endif
#ifndef AUTH_BARS
#define AUTH_BARS 128
#endif

// A message that no client of this release can send - a field past its
// range, a type the protocol has not got (Suspect, SV_auth.c) - counts
// against its connection: this many within SUSPECT_WINDOW_SECONDS and it is
// disconnected and its address barred as above. A real client's messages
// are never among them (a late one, a race's leftovers, are dropped without
// a count); an unofficial or a tampered one's are.
#ifndef SUSPECT_MAX
#define SUSPECT_MAX 10
#endif
#ifndef SUSPECT_WINDOW_SECONDS
#define SUSPECT_WINDOW_SECONDS 60
#endif

// Connection floods (1115; Guard_*, SV_auth.c). An address that opens
// CONNECT_PER_IP_PER_MINUTE connections within a minute is barred as above
// (a player reconnecting after a few drops is nowhere near it); past
// CONNECT_PER_SECOND_MAX new connections in one second, server-wide, the
// rest of that second's are refused (a server restart with everybody
// piling back in stays under it); a connection on the room list that has
// sent nothing for ROOMLIST_SILENT_SECONDS is disconnected (the client
// asks for the list every 5 s while it is there, so a player browsing
// never trips it - a bot holding a slot does); and an address sending
// more than QUERY_PER_IP_PER_SEC Quick Play status queries in a second
// has the rest dropped unanswered (Quick Play sends one per server per
// probe). CONNECT_TRACK and QUERY_TRACK are the addresses counted at
// once; past them the oldest count goes first.
#ifndef CONNECT_PER_IP_PER_MINUTE
#define CONNECT_PER_IP_PER_MINUTE 20
#endif
#ifndef CONNECT_PER_SECOND_MAX
#define CONNECT_PER_SECOND_MAX 40
#endif
#ifndef ROOMLIST_SILENT_SECONDS
#define ROOMLIST_SILENT_SECONDS 30
#endif
#ifndef QUERY_PER_IP_PER_SEC
#define QUERY_PER_IP_PER_SEC 10
#endif
#ifndef CONNECT_TRACK
#define CONNECT_TRACK 256
#endif
#ifndef QUERY_TRACK
#define QUERY_TRACK 64
#endif

// Anti-cheat (1115; CG_CHEAT, global.h). A player whose client's own checks
// tripped is removed (DISCONNECT_REASON_CHEAT: KICKED BY ANTI-CHEAT on their
// screen) and the address barred for CHEAT_BAN_SECONDS here - and, with the
// heartbeat on, on every server: the ban goes to the site as a keyed hash of
// the address (the shared OCTR_HEARTBEAT_SEEN_KEY, so every server makes
// the same hash and no address list leaves any of them) and comes back to
// all of them with their next heartbeat (SV_heartbeat.c). HEARTBEAT_BANS_MAX
// is the most a server keeps and sends at once.
#ifndef CHEAT_BAN_SECONDS
#define CHEAT_BAN_SECONDS 600
#endif
#ifndef HEARTBEAT_BANS_MAX
#define HEARTBEAT_BANS_MAX 256
#endif

#endif
