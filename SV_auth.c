// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

// SV_auth.c - the signed join (1115) and the suspect count. See SV_auth.h.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <bcrypt.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <enet/enet.h>

// (the PC side of the shared header, as SV_main.c and SV_heartbeat.c take it)
#ifndef WINDOWS_INCLUDE
#define WINDOWS_INCLUDE
#endif
#include "global.h"
#include "SV_config.h"
#include "SV_auth.h"
#include "monocypher/monocypher-ed25519.h"   // RFC 8032 Ed25519 (SHA-512): the optional half of Monocypher

extern ENetHost* server;            // SV_main.c
extern void PrintPrefix(const int roomId);

// SV_config.h's key from this release: the release it names, not the key's
// own number - a release may keep the last one's key (1121 kept 1119's).
#if OCTR_CLIENT_PUBKEY_SERVER_VERSION != SERVER_VERSION
#error "SV_config.h's OCTR_CLIENT_PUBKEY is another release's: this header, global.h and SV_main.c come from one release (see README)"
#endif

static const unsigned char clientPubkey[32] = OCTR_CLIENT_PUBKEY;

// The nonce of every connection slot (ENet's index for it, as peerRates in
// SV_main.c), drawn at CONNECT.
static unsigned char nonces[MAX_PEERS][OCTR_AUTH_NONCE_LEN];

// Messages no client of this release can send, by connection slot: how many
// in the window that began at firstAt.
typedef struct
{
	enet_uint32 firstAt;
	unsigned short count;
} SuspectCount;
static SuspectCount suspects[MAX_PEERS];

// The addresses refused for their joins' signatures, or disconnected for
// their messages: the failures in the window that began at firstFailAt, and
// until when the address is barred (0: not barred, only counted so far).
typedef struct
{
	enet_uint32 host;
	enet_uint32 firstFailAt;
	enet_uint32 until;
	unsigned char fails;
	unsigned char live;
} AuthBar;
static AuthBar bars[AUTH_BARS];

// When each connection slot last sent a message (Guard_Stamp; its CONNECT
// to begin with): the room list's silence (Guard_Silent).
static enet_uint32 heardAt[MAX_PEERS];

static int openServer = 0;

// ------------------------------------------------------------- randomness
//
// The nonces must not be guessable: a signature over a guessed one, got from
// an official client somehow, could be sent ahead. Windows' system RNG, or
// /dev/urandom elsewhere; should neither answer (it never happens on a host
// the server runs on), the clock and the stack, said in the log, so the
// server still runs.
static void Rand_Bytes(unsigned char* out, size_t len)
{
	static int warned = 0;
	int ok = 0;
#ifdef _WIN32
	ok = BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
	FILE* f = fopen("/dev/urandom", "rb");
	if (f)
	{
		ok = fread(out, 1, len, f) == len;
		fclose(f);
	}
#endif
	if (!ok)
	{
		size_t i;
		unsigned int x = (unsigned int)enet_time_get() ^ (unsigned int)(size_t)out ^ (unsigned int)rand();
		for (i = 0; i < len; i++)
		{
			x = x * 1103515245u + 12345u;
			out[i] = (unsigned char)(x >> 16);
		}
		if (!warned)
		{
			warned = 1;
			PrintPrefix(-1);
			printf("Warning: no system randomness for the join nonces (using the clock)\n");
		}
	}
}

// -------------------------------------------------------------------- bars

static AuthBar* Bar_Find(enet_uint32 host)
{
	int i;
	for (i = 0; i < AUTH_BARS; i++)
		if (bars[i].live && bars[i].host == host)
			return &bars[i];
	return 0;
}

// The address's entry, made if there is none: a free slot, or the one with
// the least time left (a counted-only entry counts as no time left).
static AuthBar* Bar_Take(enet_uint32 host)
{
	enet_uint32 now = enet_time_get();
	AuthBar* b = Bar_Find(host);
	int i, slot = 0;
	if (b) return b;
	for (i = 0; i < AUTH_BARS; i++)
	{
		if (!bars[i].live) { slot = i; break; }
		if ((int)(bars[i].until - bars[slot].until) < 0) slot = i;
	}
	b = &bars[slot];
	memset(b, 0, sizeof(*b));
	b->host = host;
	b->firstFailAt = now;
	b->live = 1;
	return b;
}

static void Bar_Set(AuthBar* b)
{
	b->until = enet_time_get() + (enet_uint32)AUTH_BAR_SECONDS * 1000u;
	if (b->until == 0) b->until = 1;
}

static void Bar_Print(enet_uint32 host, const char* why)
{
	union { enet_uint32 ip; unsigned char b[4]; } u;
	AuthBar* b = Bar_Find(host);
	int secs = b ? (int)((b->until - enet_time_get()) / 1000u) : AUTH_BAR_SECONDS;
	u.ip = host;
	PrintPrefix(-1);
	printf("Address [%d.%d.%d.%d] barred for %ds: %s\n", u.b[0], u.b[1], u.b[2], u.b[3], secs, why);
}

int Auth_Barred(enet_uint32 host)
{
	AuthBar* b = Bar_Find(host);
	if (!b || b->until == 0) return 0;
	if ((int)(b->until - enet_time_get()) > 0) return 1;
	b->live = 0;   // its time up: forgotten, failures and all
	return 0;
}

// ---------------------------------------------------------------- the join

void Auth_Init(void)
{
	const char* open = getenv("OCTR_OPEN_SERVER");
	openServer = open && open[0] && strcmp(open, "0") != 0;
	memset(nonces, 0, sizeof(nonces));
	memset(suspects, 0, sizeof(suspects));
	memset(bars, 0, sizeof(bars));
	PrintPrefix(-1);   // (the log's time and General, as the lines around it: Matt)
	if (openServer)
	{
		printf("Joins: OPEN SERVER (OCTR_OPEN_SERVER): unsigned joins are taken - never on a public server\n");
	}
	else
	{
		printf("Joins: signed by the official launcher only (client key %d; OCTR_OPEN_SERVER=1 takes unsigned joins)\n",
			OCTR_CLIENT_PUBKEY_VERSION);
	}
}

static size_t Slot(const ENetPeer* peer)
{
	return (size_t)(peer - server->peers);
}

void Auth_Connect(ENetPeer* peer)
{
	size_t s = Slot(peer);
	Rand_Bytes(nonces[s], OCTR_AUTH_NONCE_LEN);
	memset(&suspects[s], 0, sizeof(suspects[s]));
	heardAt[s] = enet_time_get() | 1;
}

// ---------------------------------------------------------- the floods

// A counted address: how many in the window that began at windowAt
typedef struct
{
	enet_uint32 host;
	enet_uint32 windowAt;
	unsigned short count;
	unsigned char live;
} AddrCount;

// The address's entry in a table, made (over the oldest window) if there
// is none; its window begun afresh when the last one is older than windowMs
static AddrCount* Count_Take(AddrCount* table, int n, enet_uint32 host, enet_uint32 windowMs)
{
	enet_uint32 now = enet_time_get();
	int i, slot = -1, oldest = 0;
	AddrCount* c = 0;
	for (i = 0; i < n; i++)
	{
		if (table[i].live && table[i].host == host) { c = &table[i]; break; }
		if (!table[i].live) { if (slot < 0) slot = i; }
		else if ((int)(table[i].windowAt - table[oldest].windowAt) < 0) oldest = i;
	}
	if (!c)
	{
		c = &table[slot >= 0 ? slot : oldest];
		memset(c, 0, sizeof(*c));
		c->host = host;
		c->live = 1;
		c->windowAt = now;
	}
	if ((enet_uint32)(now - c->windowAt) > windowMs)
	{
		c->windowAt = now;
		c->count = 0;
	}
	return c;
}

static AddrCount connects[CONNECT_TRACK];
static AddrCount queries[QUERY_TRACK];

int Guard_Connect(enet_uint32 host)
{
	static enet_uint32 secondAt = 0;
	static int thisSecond = 0, said = 0;
	enet_uint32 now = enet_time_get();
	AddrCount* c;

	// everywhere, this second
	if ((enet_uint32)(now - secondAt) >= 1000)
	{
		secondAt = now;
		thisSecond = 0;
		said = 0;
	}
	if (++thisSecond > CONNECT_PER_SECOND_MAX)
	{
		if (!said)
		{
			said = 1;
			PrintPrefix(-1);
			printf("Connection flood: over %d new connections in a second, the rest of this second's refused\n", CONNECT_PER_SECOND_MAX);
		}
		return 2;
	}

	// this address, this minute
	c = Count_Take(connects, CONNECT_TRACK, host, 60000);
	if (++c->count < CONNECT_PER_IP_PER_MINUTE) return 0;
	Bar_Set(Bar_Take(host));
	Bar_Print(host, "connections opened this minute");
	c->count = 0;
	return 1;
}

void Guard_Stamp(const ENetPeer* peer)
{
	heardAt[Slot(peer)] = enet_time_get() | 1;
}

int Guard_Silent(const ENetPeer* peer)
{
	enet_uint32 at = heardAt[Slot(peer)];
	return at != 0 && (enet_uint32)(enet_time_get() - at) > (enet_uint32)ROOMLIST_SILENT_SECONDS * 1000u;
}

void Auth_BarCheat(enet_uint32 host)
{
	AuthBar* b = Bar_Take(host);
	b->until = enet_time_get() + (enet_uint32)CHEAT_BAN_SECONDS * 1000u;
	if (b->until == 0) b->until = 1;
	Bar_Print(host, "anti-cheat tripped (CG_CHEAT)");
}

int Guard_Query(enet_uint32 host)
{
	AddrCount* c = Count_Take(queries, QUERY_TRACK, host, 1000);
	return ++c->count <= QUERY_PER_IP_PER_SEC;
}

const unsigned char* Auth_Nonce(const ENetPeer* peer)
{
	return nonces[Slot(peer)];
}

#ifdef OCTR_AUTH_TEST
// (the test harness alone: a nonce of its choosing, so it can sign it)
void Auth_TestSetNonce(const ENetPeer* peer, const unsigned char* nonce)
{
	memcpy(nonces[Slot(peer)], nonce, OCTR_AUTH_NONCE_LEN);
}
#endif

int Auth_Check(const ENetPeer* peer, const struct CG_MessageRoom* r)
{
	unsigned char msg[OCTR_AUTH_MSG_LEN];
	int i, zero = 1;

	for (i = 0; i < OCTR_AUTH_SIG_LEN; i++)
		if (r->sig[i] != 0) { zero = 0; break; }
	if (zero)
		return openServer;

	memcpy(msg, nonces[Slot(peer)], OCTR_AUTH_NONCE_LEN);
	msg[OCTR_AUTH_NONCE_LEN] = (unsigned char)(r->version & 0xff);
	msg[OCTR_AUTH_NONCE_LEN + 1] = (unsigned char)(r->version >> 8);
	return crypto_ed25519_check(r->sig, clientPubkey, msg, sizeof(msg)) == 0;
}

int Auth_Refused(const ENetPeer* peer)
{
	enet_uint32 now = enet_time_get();
	AuthBar* b = Bar_Take(peer->address.host);
	if (b->until != 0) return 1;   // barred already (its connection outlived the bar's start)
	if ((enet_uint32)(now - b->firstFailAt) > (enet_uint32)AUTH_FAIL_WINDOW_SECONDS * 1000u)
	{
		b->firstFailAt = now;
		b->fails = 0;
	}
	if (++b->fails < AUTH_FAILS_MAX) return 0;
	Bar_Set(b);
	Bar_Print(peer->address.host, "joins refused for their signature (unofficial client)");
	return 1;
}

int Suspect(ENetPeer* peer, const char* why)
{
	enet_uint32 now = enet_time_get();
	size_t s = Slot(peer);
	SuspectCount* c = &suspects[s];
	union { enet_uint32 ip; unsigned char b[4]; } u;
	u.ip = peer->address.host;

	if (c->count == 0 || (enet_uint32)(now - c->firstAt) > (enet_uint32)SUSPECT_WINDOW_SECONDS * 1000u)
	{
		c->firstAt = now;
		c->count = 0;
	}
	c->count++;
	PrintPrefix(-1);
	printf("Suspect message from [%d.%d.%d.%d]: %s (%d of %d)\n", u.b[0], u.b[1], u.b[2], u.b[3], why, c->count, SUSPECT_MAX);
	if (c->count < SUSPECT_MAX) return 0;

	Bar_Set(Bar_Take(peer->address.host));
	Bar_Print(peer->address.host, "messages no client of this release sends");
	c->count = 0;
	return 1;
}
