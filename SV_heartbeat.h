// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

/*
 * SV_heartbeat.h - outbound status heartbeat for the OnlineCTR dedicated server.
 *
 * Publishes player counts to a web endpoint so the site can show live server
 * status. Outbound only: the server never listens on an extra port, so this
 * adds no inbound attack surface.
 *
 * The whole module compiles to nothing unless OCTR_HEARTBEAT is defined, which
 * CMake sets only when it actually finds libcurl. A build without libcurl is
 * byte-for-byte what it was before.
 *
 * Threading: Heartbeat_Publish() is called from the main tick loop and only
 * copies a small struct under a lock. All network I/O happens on a worker
 * thread. This matters more than it looks - the tick loop is
 * "while (!g_stopSignal) { SleepMicro(1); ServerState_Tick(); }", and stalling it delays
 * windowsClientSync updates, which is exactly what trips the PS1-side
 * frames_unsynced disconnect detector. A blocking TLS handshake in the tick
 * loop would kick players.
 *
 * Players seen in the past 24 hours: for the site's "N seen in the past 24h",
 * which it used to guess from how the occupancy counts moved. Each player is
 * one keyed hash - HMAC-SHA256 of the UTC day number, the address and (once
 * known) the name, first 8 bytes as hex - kept in memory for 24 hours and
 * sent with every heartbeat as "seen":[...], so the site can union the lists
 * across servers and count. The key is OCTR_HEARTBEAT_SEEN_KEY, the same
 * value on every server, so one player is one hash whichever region they
 * are on; left unset, each server keys with its own heartbeat secret and a
 * player who visits two regions in a day counts twice. No address is stored
 * or sent, nothing survives a restart, and the day in the key means a hash
 * cannot be matched to the same player tomorrow. Two people behind one
 * address count as one until they name themselves, then as two; a player
 * whose address changes during the day counts twice. Heartbeat off means
 * nothing is recorded at all.
 */

#ifndef SV_HEARTBEAT_H
#define SV_HEARTBEAT_H

#include "SV_config.h"   /* HEARTBEAT_INTERVAL_*, OCTR_HEARTBEAT_SEEN_MAX */

#define OCTR_HEARTBEAT_ROOMS 16

typedef struct
{
	/* Hole-aware player counts, matching what SendRoomData() sends to the PS1.
	   Do NOT populate these from roomInfos[i].clientCount - see the warning at
	   the top of SendRoomData(); clientCount does not account for empty holes,
	   so it would disagree with the SERVER TOTAL players see in game. */
	int rooms[OCTR_HEARTBEAT_ROOMS];
	int locked[OCTR_HEARTBEAT_ROOMS];

	int total;        /* sum of rooms[] */
	int version;      /* SERVER_VERSION from global.h: the number clients are
	                     checked against, so the site can flag a server that
	                     is behind. Sent as "version". */
	int build;        /* VERSION from global.h: the release this server was
	                     built from. Sent as "build"; the site may ignore it. */
	long uptimeSec;
} OctrStatus;

/* Reads config from the environment and starts the worker thread.
   Returns 1 if the heartbeat is running, 0 if it is disabled or failed to
   start. Disabled is not an error: no URL or no secret configured means no
   heartbeat. Nothing here is compiled in: each host sets them in its own
   octr-server.env (from octr-server.env.example), which git never sees.

     OCTR_HEARTBEAT_URL       required, the site's heartbeat endpoint
     OCTR_HEARTBEAT_SECRET    required, this server's secret for the HMAC (the
                              site keeps one per server ID)
     OCTR_HEARTBEAT_ID        default "unknown", e.g. "aus1" - identifies this server
     OCTR_HEARTBEAT_INTERVAL  default HEARTBEAT_INTERVAL_SECONDS (30), seconds
                              between posts (minimum HEARTBEAT_INTERVAL_MIN_SECONDS)
     OCTR_HEARTBEAT_SEEN_KEY  optional, key for the seen-24h hashes; set the
                              same value on every server (defaults to the secret)
*/
int Heartbeat_Init(void);

/* Cheap: copies the struct under a lock. Safe to call every tick. */
void Heartbeat_Publish(const OctrStatus* status);

/* At most OCTR_HEARTBEAT_SEEN_MAX distinct players are tracked (and sent per
   heartbeat; SV_config.h); beyond it the least recently seen record is
   reused. */

/* At CONNECT, with the peer's address (ENetAddress.host): records the
   address as seen now. Returns a handle for Heartbeat_SeenName - keep it in
   peer->data - or NULL when the heartbeat is off. */
void* Heartbeat_SeenConnect(unsigned int host);

/* At CG_NAME, with that handle and the sanitised, NUL-terminated name: the
   record becomes address + name, so two players sharing an address are two.
   A player already counted today under that address and name stays one. */
void  Heartbeat_SeenName(void* handle, unsigned int host, const char* name);

/* Optional; the process usually just exits. */
void Heartbeat_Shutdown(void);

extern void PrintPrefix(const int roomId);

#endif /* SV_HEARTBEAT_H */
