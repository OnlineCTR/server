// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

/*
 * SV_auth.h - the signed join (1115), and the count of messages no client of
 * this release can send.
 *
 * Every connection gets a nonce at CONNECT (Auth_Connect), sent to it in each
 * room list (SG_MessageRooms.nonce). A join (CG_MessageRoom) must carry an
 * Ed25519 signature over that nonce and the client's SERVER_VERSION
 * (OCTR_AUTH_MSG_LEN bytes, global.h), made by the official launcher with the
 * release's private key; Auth_Check verifies it with the release's public key
 * (OCTR_CLIENT_PUBKEY, SV_config.h). Monocypher does the arithmetic
 * (monocypher/, vendored, 4.0.2: public domain / BSD-2) - its optional
 * monocypher-ed25519, the RFC 8032 Ed25519 the key tool makes keys for, not
 * the core's own BLAKE2b variant.
 *
 * A join refused for its signature counts against its address (Auth_Refused):
 * AUTH_FAILS_MAX within AUTH_FAIL_WINDOW_SECONDS and the address is barred for
 * AUTH_BAR_SECONDS - its connections refused at CONNECT (Auth_Barred), as a
 * banned address's. So are connections that send messages no client of this
 * release can (Suspect: a field past its range, a type the protocol has not
 * got), SUSPECT_MAX within SUSPECT_WINDOW_SECONDS.
 *
 * OCTR_OPEN_SERVER=1 in the environment takes unsigned joins too, for a
 * private server and a client built by hand; Auth_Init says so at start-up.
 * It never makes a bad signature good: a join that carries one is refused
 * either way - only an all-zero signature (a client run without the
 * launcher) is let through on an open server.
 */

#ifndef SV_AUTH_H
#define SV_AUTH_H

#include <enet/enet.h>

struct CG_MessageRoom;

// At start-up, after ENet's host is made: reads OCTR_OPEN_SERVER and seeds
// the nonces. Prints the mode.
void Auth_Init(void);

// A new connection in its slot (CONNECT, beside Rate_Reset): a fresh nonce.
void Auth_Connect(ENetPeer* peer);

// The connection's nonce, OCTR_AUTH_NONCE_LEN bytes, for its room lists.
const unsigned char* Auth_Nonce(const ENetPeer* peer);

// 1: the join's signature is good (or the server is open and it is unsigned).
// 0: refused.
int Auth_Check(const ENetPeer* peer, const struct CG_MessageRoom* r);

// A join refused by Auth_Check, counted against the address. 1 when the
// address is barred by it: the caller disconnects the peer.
int Auth_Refused(const ENetPeer* peer);

// A message no client of this release can send, counted against the
// connection and its address; why names it for the log. 1 when the
// connection is to be disconnected (and its address is barred): the caller
// does that and drops the message.
int Suspect(ENetPeer* peer, const char* why);

// At CONNECT: 1 when the address is barred now (refuse the connection).
int Auth_Barred(enet_uint32 host);

// ---- connection floods (SV_config.h's CONNECT_*, ROOMLIST_SILENT_SECONDS,
// QUERY_PER_IP_PER_SEC)

// At CONNECT, after Auth_Connect: 0 take the connection; 1 the address has
// opened CONNECT_PER_IP_PER_MINUTE this minute (now barred: refuse); 2 the
// server has taken CONNECT_PER_SECOND_MAX this second (refuse, no bar).
int Guard_Connect(enet_uint32 host);

// A message from the connection (any, the rate bucket's take and all).
void Guard_Stamp(const ENetPeer* peer);

// 1 when the connection has sent nothing for ROOMLIST_SILENT_SECONDS.
int Guard_Silent(const ENetPeer* peer);

// A Quick Play status query from the address: 1 answer it, 0 drop it.
int Guard_Query(enet_uint32 host);

// The client's anti-cheat tripped (CG_CHEAT): the address barred for
// CHEAT_BAN_SECONDS (the bar table above, said in the log).
void Auth_BarCheat(enet_uint32 host);

#endif
