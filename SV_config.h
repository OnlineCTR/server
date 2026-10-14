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
// the vote's and the kick ban's seconds - is in global.h, shared with the
// ROM and the PC client, not here.

#ifndef SV_CONFIG_H
#define SV_CONFIG_H

// ------------------------------------------------------------ connections

// Connections allowed from one IP address at a time (see the CONNECT event).
#ifndef MAX_PEERS_PER_IP
#define MAX_PEERS_PER_IP 12
#endif

// The host's kicks held at once (KickBan_*, SV_main.c): the addresses kept
// out of the room they were kicked from for KICK_BAN_SECONDS (global.h).
#ifndef KICK_BANS
#define KICK_BANS 64
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

#endif
