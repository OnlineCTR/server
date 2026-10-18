// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

#ifdef __WINDOWS__
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h> // why a port could not be opened (PrintPortTrouble)
#ifndef __WINDOWS__
#include <unistd.h> // _exit
#endif
#include <enet/enet.h>
#include "malloc.h"

// Shared protocol header: the global.h next to these sources (the release
// zip ships it; CMakeLists.txt and Server.vcxproj add no other include path
// for it), a copy of decompile/General/AltMods/OnlineCTR/global.h in the
// ModSDK.
// Also set by the project (vcxproj), so that IntelliSense reads global.h
// the way it is compiled here; guarded for the builds that do not.
#ifndef WINDOWS_INCLUDE
#define WINDOWS_INCLUDE
#endif
// The protocol this file was written for: global.h refuses to build with
// another release's (SERVER_VERSION), which would have this relay messages
// of a size and meaning it does not know.
#define SV_PROTOCOL 1107
#include "global.h"

// The header has to be the one these sources were written against. A stale
// copy - an earlier release's global.h left next to the sources - would fail
// a page further down with undeclared message types and incomplete structs;
// this says what actually went wrong. NET_WEAPON_EVENTS is the newest
// thing this file needs from the header (8: a masked kart's bump claimed,
// NET_EV_KART_CLAIM, 1107; 5: the game modes' events,
// NET_EV_LAP / NET_EV_OUT / NET_EV_IT, and SG_STARTLOADING's round code;
// 4: 4-bit IDs, 12-bit identity, the rooms' closed mask; 3: the weapons'
// arbitration, Tiger Temple's door, the item-less rooms' ghost bombs).
#if !defined(NET_WEAPON_EVENTS) || (NET_WEAPON_EVENTS < 8)
#error "global.h is older than SV_main.c: replace it with the global.h from this release's zip (see README)"
#endif
// The game modes' rounds (Round_*: the ROUND_* causes and codes, TAG_*,
// ROOM_IS_MODE): 1101, inside the same unreleased build as the events above;
// 2, BOMBARDMENT's lives (BOMB_LIVES, NET_EV_LIVES): 1102.
#if !defined(NET_ROUND_MODES) || (NET_ROUND_MODES < 2)
#error "global.h is older than SV_main.c: replace it with the global.h from this release's zip (see README)"
#endif
// The seats changed with no message changing size (10 to 12 before 1101): a
// 10-seat global.h would still build, as a server that seats 10, and nothing
// on the wire would say so.
// The host's OPTIONS (CG_HOSTACTION, DISCONNECT_REASON_KICKED) and the
// players' vote (SG_VOTE, CG_VOTE): 1101.
#if !defined(NET_HOST_ACTIONS) || (NET_HOST_ACTIONS < 1) || !defined(NET_VOTING) || (NET_VOTING < 1)
#error "global.h is older than SV_main.c: replace it with the global.h from this release's zip (see README)"
#endif
#if !defined(MAX_NUM_PLAYERS) || (MAX_NUM_PLAYERS != 12)
#error "global.h is not the 12-seat header SV_main.c was written against: replace it with the global.h from this release's zip (see README)"
#endif
// The kart message's lap word and number (CG/SG_EverythingKart.lap, .seq,
// NET_LAP_*): 1104. Without it the relay below names fields an older
// header does not have.
#if !defined(NET_KART_LAPS) || (NET_KART_LAPS < 1)
#error "global.h is older than SV_main.c: replace it with the global.h from this release's zip (see README)"
#endif

// The server's own settings: connections, clocks, limits (public; nothing
// secret is compiled in - the heartbeat's address and key come from the
// environment at run time, SV_heartbeat.h).
#include "SV_config.h"

#include "SV_heartbeat.h"

#pragma comment (lib, "Ws2_32.lib")

// A room's seats: MAX_NUM_PLAYERS (global.h), the ROM's and the client's.
#define MAX_CLIENTS MAX_NUM_PLAYERS

// Connections per address (MAX_PEERS_PER_IP), the results countdown
// (RACE_OVER_COUNTDOWN_SECONDS), the lobby-return timer
// (LOBBY_RETURN_SECONDS) and the lobby idle budget (LOBBY_IDLE_SECONDS):
// SV_config.h.


#ifdef __WINDOWS__
#define CLOCKS_PER_SEC_FIX CLOCKS_PER_SEC
#else
#define CLOCKS_PER_SEC_FIX ((clock_t)100000) // Original value (1000000), I removed one zero
#endif

//SV_helper.c forward decls
void PrintTime();
//int GetWeekDay(); //not used currently
void AddPeerLPL(ENetPeer* peer);
int RemovePeerLPL(ENetPeer* peer);
int ForeachPeerLPL(void (*lambda)(ENetPeer*));
int CountPeersLPL_FromHost(unsigned int host);
void AddIPBanL(unsigned int IP);
int AnyMatchIPBanL(unsigned int IP);

typedef struct {
	ENetPeer* peer;

	char name[NAME_LEN];
	char characterID;
	char boolLoadSelf;
	char boolRaceSelf;
	char boolEndSelf;

	// Their vote in the room's vote running (Vote_*): the option + 1, 0
	// none - so the join's memset is no vote.
	unsigned char vote;

	// Lobby idle budget (IdleRun and friends): ms left, and enet_time_get()
	// when the clock last started running, 0 while it is paused. Server-only,
	// never on the wire.
	int idleLeftMs;
	enet_uint32 idleStart;

	// The game modes' round this race (Round_*): this player's place (0
	// still in, 1-12, ROUND_NONE for a leaver), how they got it (ROUND_CAUSE_*,
	// 0 still in), and in ELIMINATION the laps they have reported done
	// (NET_EV_LAP), in BOMBARDMENT the lives they have left (BOMB_LIVES at
	// the load). Cleared as the race loads (Round_Begin) and after it
	// (ResetRoomAfterRace); a join's memset clears them too. Server-only.
	unsigned char place;
	unsigned char cause;
	unsigned char lapsDone;
	unsigned char lives;

	// A masked kart's bump claims (KartClaim_Relay): when this player last
	// fired a mask (CG_WEAPON), and last claimed a bump on each other player
	// (enet_time_get() | 1; 0 never). Cleared as each race loads; a join's
	// memset clears them too. Server-only.
	enet_uint32 maskShotAt;
	enet_uint32 claimAt[MAX_CLIENTS];
} PeerInfo;

typedef struct
{
	PeerInfo peerInfos[MAX_CLIENTS];

	unsigned char clientCount;
	unsigned char levelPlayed;

	// Slot that currently owns the lobby. Took a byte from the padding, so
	// RoomInfo's size is unchanged; it is server-only and never goes on the
	// wire, so nothing else cares. Validity is decided by
	// peerInfos[hostID].peer != 0 rather than a sentinel, because the whole
	// struct is memset to 0 when a room dies and any sentinel would be lost.
	unsigned char hostID;

	// The lap row the host picked (CG_TRACK), kept for late joiners, who are
	// sent the track and laps on the way in. Was the padding byte.
	unsigned char lapPlayed;

	// "the track is in": set by CG_TRACK, cleared by CG_TRACKCANCEL and the
	// post-race reset. NOT whether the room can be joined - that is
	// RoomOpenForJoin(), which stays open through the picks.
	char boolRoomLocked;
	char boolLoadAll;
	char boolRaceAll;
	char boolEndAll;

	// What SendRoomData last said about joining, so ServerState_Tick can
	// push a fresh room list the moment it changes (a clock crossing the
	// close line, and back when the player it belonged to is gone).
	char boolJoinClosed;

	// The players' vote for the track, then the laps (Vote_*): the vote
	// running (VOTE_*), whether the room votes at all - the host's PLAYER
	// VOTING, off is set, so the room's memset leaves a new room voting -
	// the track the track vote put in, for the lap vote, and when the vote
	// running ends at the latest (enet_time_get() ms).
	char votePhase;
	char votingOff;
	unsigned char voteTrack;
	char trackVoted;   // the track in now came from the vote (Vote_Finish), not the host (SG_TRACK's voted)
	enet_uint32 voteDeadline;

	// A player alone in the room has no clock (Room_Solo, Solo_Tick): soloHold
	// is whether the room was held as alone at the last tick; voteHeldMs a
	// vote's time left while it waits for a second player (its deadline set
	// when one comes), 0 for a vote whose deadline runs.
	char soloHold;
	int voteHeldMs;

	// The deadline (enet_time_get() ms) of the longest clock still running
	// in the room, as last shown to the locked-in players (RoomWatchUpdate);
	// 0 with none. Server-only.
	enet_uint32 watchDeadline;

	// enet_time_get() when the last racer finished; the room resets
	// RACE_OVER_COUNTDOWN_SECONDS after this. Unsigned ms so the subtraction
	// stays correct across the 49-day wrap.
	enet_uint32 endTime;

	// enet_time_get() when the FIRST racer finished, 0 until then; the room is
	// forced back to the lobby LOBBY_RETURN_SECONDS after this if the race is
	// still going. Same wrap-safe arithmetic as endTime.
	enet_uint32 firstEndTime;

	// enet_time_get() when SG_STARTLOADING went out, 0 before it and once the
	// load's time is up: whoever has not loaded LOAD_TIMEOUT_SECONDS after it
	// is removed (ServerState_Tick). Same wrap-safe arithmetic as endTime.
	enet_uint32 loadStartAt;

	// The game modes' round this race (Round_*), server-only. roundOn: the
	// race loaded as a round (SG_STARTLOADING's code was not 0); roundGreenAt:
	// when its green light shows (SG_STARTRACE + the lights, enet_time_get()
	// ms; 0 none pending); roundLive: it is being refereed, from that green
	// (Round_Tick) with 2 or more still in; roundOver: its last verdict is
	// out. roundDone: the places given
	// from the top so far (finishers). roundLaps: its laps (ELIMINATION's
	// players - 1; the voted laps or TAG_LAPS otherwise, for the log), and
	// elimLap the lap ELIMINATION judges next (1 the first). TAG: who is it
	// (a client ID, ROUND_NONE nobody), when its clock starts (the green,
	// enet_time_get() ms; 0 once it has) and when "it" is out (0 while no
	// clock runs). The players still in are counted when needed
	// (Round_InCount), never kept, so a leaver cannot leave a count behind.
	char roundOn;
	char roundLive;
	char roundOver;
	enet_uint32 roundGreenAt;
	unsigned char roundDone;
	unsigned char roundLaps;
	unsigned char elimLap;
	unsigned char itID;
	enet_uint32 itGreenAt;
	enet_uint32 itDeadline;

	// Each weapon's fate this race, by its identity (the shooter's client ID
	// << 8 | its shot count: NET_EV_*, global.h): WEAPON_UNKNOWN, LIVE,
	// ENDED, or WEAPON_HELD + the client ID of the kart carrying it on its
	// head. Every machine tests only its own kart against the weapons and
	// claims what it saw; this keeps the first claim on each and drops the
	// rest (CG_WEAPONEVENT), so two machines that each saw their own kart
	// take the same weapon get the same answer back. Zero is "unknown", so
	// the room's memset clears it too; cleared at each race's loading.
	unsigned char weaponFate[MAX_CLIENTS * 256];

	// Contested touches held for the judging on the players' pings
	// (WeaponHold_*): a placed weapon's claim whose kart waits on the
	// verdict, kept until an earlier touch by another player could have
	// arrived. Cleared with weaponFate.
	struct WeaponHold
	{
		unsigned char used;
		unsigned char peer;       // the claimant
		unsigned char kind;
		unsigned char arg;
		unsigned short id;        // the weapon: owner << 8 | serial
		enet_uint32 at;           // when its kart touched it: arrival less half its round trip
		enet_uint32 until;        // judged then, with nothing earlier in
	} weaponHolds[8];

} RoomInfo;

#define WEAPON_UNKNOWN 0
#define WEAPON_LIVE    1
#define WEAPON_ENDED   2
#define WEAPON_HELD    3

ENetHost* server;

RoomInfo roomInfos[16] = { NULL };

// Defined with the idle clock helpers further down; the room list needs it.
static int RoomOpenForJoin(RoomInfo* ri);

// ------------------------------------------------------------- kick bans
//
// The host's KICK PLAYER (CG_HOSTACTION) keeps that player out of the room
// they were kicked from for KICK_BAN_SECONDS, by their address - the one
// thing a reconnect keeps (a household behind one NAT shares it, and is kept
// out of that room along with them). The room shows closed to them, on their
// room list and in Quick Play's query (KickBan_Mask), and a join is refused;
// every other room is theirs. Kept apart from RoomInfo, so the room emptying
// (its memset) does not lift it. Never on the wire; KICK_BAN_SECONDS is in
// global.h, as the client goes by it too (ServerLive_Kicked, CL_main.cpp).
// How many at once (KICK_BANS): SV_config.h.

static struct
{
	enet_uint32 ip;
	enet_uint32 until;   // enet_time_get() ms; wrap-safe compares
	unsigned char room;
	unsigned char live;
} kickBans[KICK_BANS];

static int kickBanSeconds = KICK_BAN_SECONDS;   // --kickban: shorter, for testing
static int voteSeconds = VOTE_SECONDS;           // --votesecs: shorter, for testing
static int tagSeconds = TAG_SECONDS;             // --tagsecs: TAG's clock, shorter for testing
static int loadSeconds = LOAD_TIMEOUT_SECONDS;   // --loadsecs: the wait on a load, shorter for testing
static int tagFirst = -1;                        // --tagfirst: TAG's first "it" by client ID, for testing (-1 at random)

static void KickBan_Add(enet_uint32 ip, int room)
{
	enet_uint32 until = enet_time_get() + (enet_uint32)kickBanSeconds * 1000;
	int slot = -1;
	int k;

	// the same address and room again: its time starts over; otherwise a
	// free entry, or, all in use, the one nearest its end makes way
	for (k = 0; k < KICK_BANS; k++)
		if (kickBans[k].live && kickBans[k].ip == ip && kickBans[k].room == room)
			slot = k;
	for (k = 0; slot < 0 && k < KICK_BANS; k++)
		if (!kickBans[k].live)
			slot = k;
	if (slot < 0)
	{
		slot = 0;
		for (k = 1; k < KICK_BANS; k++)
			if ((int)(kickBans[k].until - kickBans[slot].until) < 0)
				slot = k;
	}

	kickBans[slot].ip = ip;
	kickBans[slot].until = until;
	kickBans[slot].room = (unsigned char)room;
	kickBans[slot].live = 1;
}

// The rooms closed to this address by a kick, as a closed mask (bit n, room n).
static unsigned short KickBan_Mask(enet_uint32 ip)
{
	enet_uint32 now = enet_time_get();
	unsigned short mask = 0;

	for (int k = 0; k < KICK_BANS; k++)
		if (kickBans[k].live && kickBans[k].ip == ip && (int)(kickBans[k].until - now) > 0)
			mask |= (unsigned short)(1u << kickBans[k].room);
	return mask;
}

// Ends the bans whose time is up: 1 if any did, so the room lists go out
// again with those rooms open (ServerState_Tick).
static int KickBan_Expire(void)
{
	enet_uint32 now = enet_time_get();
	int ended = 0;

	for (int k = 0; k < KICK_BANS; k++)
		if (kickBans[k].live && (int)(kickBans[k].until - now) <= 0)
		{
			kickBans[k].live = 0;
			ended = 1;
		}
	return ended;
}

// A room as the ROM's room list names it (menu.c, RoomKind): its kind and
// its row on its page, 1 to 8, the second page's marked. Both pages have the
// same eight names, so a player's "CLASSIC 2" is room 2 or room 10 here.
static void PrintRoomName(int rn)
{
	static const char* const kinds[ROOM_PAGE_ROWS] =
		{ "NO ITEMS", "CLASSIC", "POTION GAME", "BOMBARDMENT", "ELIMINATION", "TAG YOU'RE IT", "NO ITEMS USF", "CLASSIC USF" };
	printf("%s %d%s", kinds[ROOM_ROW(rn)], ROOM_ROW(rn) + 1,
		ROOM_NO_SHORTCUTS(rn) ? ", Shortcuts Disabled" : "");
}

// roomId is 1-16 (the room's index + 1), or -1 for none
void PrintPrefix(const int roomId)
{
	printf("[");
	PrintTime();
	if (roomId != -1) {
		printf(" | Room %d: ", roomId);
		PrintRoomName(roomId - 1);
		printf("] ");
	} else {
		printf(" | General] ");
	}
}

// similar to enet_host_broadcast
void broadcastToPeersUnreliable(RoomInfo* ri, const void* data, size_t size)
{
	ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_UNSEQUENCED);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		ENetPeer* currentPeer = ri->peerInfos[i].peer;

		if (currentPeer == 0)
			continue;

		if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
			continue;

		enet_peer_send(currentPeer, 0, packet);
	}

	if (packet->referenceCount == 0)
		enet_packet_destroy(packet);
}

// similar to enet_host_broadcast
void broadcastToPeersReliable(RoomInfo* ri, const void* data, size_t size)
{
	ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		ENetPeer* currentPeer = ri->peerInfos[i].peer;

		if (currentPeer == 0)
			continue;

		if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
			continue;

		enet_peer_send(currentPeer, 0, packet);
	}

	if (packet->referenceCount == 0)
		enet_packet_destroy(packet);
}

// ENet only takes ownership of a packet once it is queued. A send to a peer
// that is no longer connected returns -1 and leaves the packet with us; the
// broadcast helpers above already handle that, these two did not, and every
// SendRoomData to a peer that had quietly gone away leaked one packet.
void sendToPeerUnreliable(ENetPeer* peer, const void* data, size_t size) {
	ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_UNSEQUENCED);
	if (enet_peer_send(peer, 0, packet) < 0) //To do: have a look at the channels, maybe we want to use them better to categorize messages
		enet_packet_destroy(packet);
}

void sendToPeerReliable(ENetPeer* peer, const void* data, size_t size) {
	ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);
	if (enet_peer_send(peer, 0, packet) < 0) //To do: have a look at the channels, maybe we want to use them better to categorize messages
		enet_packet_destroy(packet);
}

void SendRoomData(ENetPeer* peer)
{
	struct SG_MessageRooms mr;
	memset(&mr, 0, sizeof(mr));
	mr.type = SG_ROOMS;
	mr.numRooms = 16;
	mr.version = SERVER_VERSION;


// The players in each room, and in closedMask the rooms that cannot be
// joined (RoomOpenForJoin), which the ROM shows as locked and, inside the
// room, as ROOM CLOSED on the PLAYERS box (lobbyGrid.c).
#define SETUP(x, index) \
	x = roomCount[index]; \
	if (!RoomOpenForJoin(&roomInfos[index])) \
		mr.closedMask |= (unsigned short)(1u << (index));

	// Do NOT use roomInfos[index].clientCount,
	// cause that doesnt account for empty holes

	int roomCount[16];
	memset(&roomCount[0], 0, sizeof(roomCount));

	for(int i = 0; i < 16; i++)
		for(int j = 0; j < MAX_CLIENTS; j++)
			if (roomInfos[i].peerInfos[j].peer != 0)
				roomCount[i]++;

	// required for bit packing :4
	SETUP(mr.numClients01, 0x0);
	SETUP(mr.numClients02, 0x1);
	SETUP(mr.numClients03, 0x2);
	SETUP(mr.numClients04, 0x3);
	SETUP(mr.numClients05, 0x4);
	SETUP(mr.numClients06, 0x5);
	SETUP(mr.numClients07, 0x6);
	SETUP(mr.numClients08, 0x7);
	SETUP(mr.numClients09, 0x8);
	SETUP(mr.numClients10, 0x9);
	SETUP(mr.numClients11, 0xa);
	SETUP(mr.numClients12, 0xb);
	SETUP(mr.numClients13, 0xc);
	SETUP(mr.numClients14, 0xd);
	SETUP(mr.numClients15, 0xe);
	SETUP(mr.numClients16, 0xf);

	// and, to a player kicked from a room a short while ago, that room - but
	// not to one seated in it (a housemate on the kicked player's address),
	// whose lobby would read ROOM CLOSED from it (lobbyGrid.c)
	{
		unsigned short ban = KickBan_Mask(peer->address.host);
		for (int i = 0; i < 16; i++)
			for (int j = 0; j < MAX_CLIENTS; j++)
				if (roomInfos[i].peerInfos[j].peer == peer)
					ban &= (unsigned short)~(1u << i);
		mr.closedMask |= ban;
	}

	sendToPeerReliable(peer, &mr, sizeof(struct SG_MessageRooms));
}

// Quick Play's status query (struct OctrQuery, global.h): a client asking
// every server at once how full its rooms are, and timing the answer, to
// find a game (QuickPlay_ProbeThread, CL_main.cpp). A datagram of its own
// to this port, answered here from ENet's intercept hook - before ENet
// reads it as one of its own, so no peer is made: the asker is no player
// anywhere, not in the seen-24h hashes, not against the per-address cap.
// The answer carries the rooms as SendRoomData counts them, and is no
// bigger than the query (both 32 bytes), so it is no use for amplifying
// traffic at a forged address. Anything else goes on to ENet.
static int OctrQuery_Intercept(ENetHost* host, ENetEvent* event)
{
	struct OctrQuery q;
	struct OctrQueryReply r;
	ENetBuffer b;
	(void)event;

	if (host->receivedDataLength != sizeof(q))
		return 0;
	memcpy(&q, host->receivedData, sizeof(q));
	if (memcmp(q.magic, OCTR_QUERY_MAGIC, OCTR_MAGIC_LEN) != 0)
		return 0;

	memset(&r, 0, sizeof(r));
	memcpy(r.magic, OCTR_REPLY_MAGIC, OCTR_MAGIC_LEN);
	r.version = SERVER_VERSION;
	r.token = q.token;
	unsigned short banned = KickBan_Mask(host->receivedAddress.host);
	for (int i = 0; i < 16; i++)
	{
		// a room the asker was kicked from: closed and empty to them, so
		// Quick Play neither joins it nor offers it as a game to wait for
		// (QuickPlay_Find skips an empty room)
		if (banned & (1u << i))
		{
			r.closedMask |= (unsigned short)(1u << i);
			continue;
		}
		for (int j = 0; j < MAX_CLIENTS; j++)
			if (roomInfos[i].peerInfos[j].peer != 0)
				r.counts[i]++;
		if (!RoomOpenForJoin(&roomInfos[i]))
			r.closedMask |= (unsigned short)(1u << i);
	}

	b.data = &r;
	b.dataLength = sizeof(r);
	enet_socket_send(host->socket, &host->receivedAddress, &b, 1);
	return 1;
}

void GetDriverFromRace(ENetPeer* peer, RoomInfo** ri, int* peerID)
{
	for (int r = 0; r < 16; r++)
	{
		*ri = &roomInfos[r];

		for (int i = 0; i < MAX_CLIENTS; i++)
			if ((*ri)->peerInfos[i].peer == peer)
				*peerID = i;

		if (*peerID != -1)
			break;
	}
}

void ProcessConnectEvent(ENetPeer* peer)
{
	if (!peer) {
		return;
	}

	// Debug only, also prints client name from CG_MessageName
	// printf("Connection from: %u:%u.\n", peer->address.host, peer->address.port);

	AddPeerLPL(peer);

	SendRoomData(peer);

	// Set the timeout settings for the host
	// now 800 for 1.5s timeout, should detect closed clients
	enet_peer_timeout(peer, 1000000, 1000000, 2000);

}

// ---------------------------------------------------------------- host role

// Does the room still have a host who is actually connected?
// Checked against peer rather than a flag so a host that vanished without a
// clean disconnect still counts as gone.
static int HasValidHost(RoomInfo* ri)
{
	return (ri->hostID < MAX_CLIENTS) && (ri->peerInfos[ri->hostID].peer != 0);
}

// Lowest occupied slot, or -1 if the room is empty.
// Scans peer, NOT clientCount: clientCount is never decremented on disconnect
// (only the whole-room memset clears it), which is the same trap SendRoomData
// warns about - "Do NOT use roomInfos[index].clientCount, cause that doesnt
// account for empty holes".
static int PickNewHost(RoomInfo* ri)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0)
			return i;

	return -1;
}

static void SendHost(RoomInfo* ri)
{
	struct SG_MessageHost sg;
	sg.type   = SG_HOST;
	sg.hostID = ri->hostID & 0xF;

	broadcastToPeersReliable(ri, &sg, sizeof(struct SG_MessageHost));
}

// ------------------------------------------------------------- idle clock
//
// One LOBBY_IDLE_SECONDS budget per player per lobby round, see the define.
// The server's clock is the one that removes a player (ServerState_Tick);
// the clients are only told the seconds whenever a clock starts or resumes,
// and count down on their own in between. A player who has locked in has no
// clock of their own any more; they are shown the longest one still running
// in the room instead - how long the wait for the others can last
// (RoomWatchUpdate) - and a 0 once none is.

static void SendIdleCountdown(ENetPeer* peer, int seconds)
{
	struct SG_MessageIdleCountdown sg;
	sg.type    = SG_IDLECOUNTDOWN;
	sg.padding = 0;
	sg.seconds = (unsigned char)seconds;
	sendToPeerReliable(peer, &sg, sizeof(struct SG_MessageIdleCountdown));
}

static void BroadcastIdleCountdown(RoomInfo* ri, int seconds)
{
	struct SG_MessageIdleCountdown sg;
	sg.type    = SG_IDLECOUNTDOWN;
	sg.padding = 0;
	sg.seconds = (unsigned char)seconds;
	broadcastToPeersReliable(ri, &sg, sizeof(struct SG_MessageIdleCountdown));
}

// Never 0: that is the "not running" value of idleStart.
static enet_uint32 IdleNow(void)
{
	enet_uint32 t = enet_time_get();
	return t ? t : 1;
}

// Milliseconds left in this player's budget, running or paused.
static int IdleLeftMs(PeerInfo* pi)
{
	if (pi->idleStart == 0)
		return pi->idleLeftMs;
	return pi->idleLeftMs - (int)(enet_time_get() - pi->idleStart);
}

// Whole seconds left, rounded up, never below 1: the removal in
// ServerState_Tick is what ends a clock, not the number reaching 0.
static int IdleLeftSeconds(PeerInfo* pi)
{
	int ms = IdleLeftMs(pi);
	return ms <= 0 ? 1 : (ms + 999) / 1000;
}

// A new round for this player: a full budget, paused.
static void IdleReset(PeerInfo* pi)
{
	pi->idleLeftMs = LOBBY_IDLE_SECONDS * 1000;
	pi->idleStart = 0;
}

// The clock runs; no-op if it already does.
static void IdleRun(PeerInfo* pi)
{
	if (pi->idleStart == 0)
		pi->idleStart = IdleNow();
}

// The clock stops, keeping what is left; no-op if it is not running.
static void IdlePause(PeerInfo* pi)
{
	if (pi->idleStart == 0)
		return;
	pi->idleLeftMs = IdleLeftMs(pi);
	pi->idleStart = 0;
}

// The players in the room now: the seats with a connection, not one on its
// way out (a kick's, an idle removal's: enet_peer_disconnect).
static int Room_Players(RoomInfo* ri)
{
	int n = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED)
			n++;
	return n;
}

// One player (or none) in the room: no clock runs, see LOBBY_IDLE_SECONDS.
static int Room_Solo(RoomInfo* ri)
{
	return Room_Players(ri) < 2;
}

// This player's own clock runs (on from where it was), and they are told
// where it stands. Alone in the room, it waits for a second player
// (Solo_Tick) and they are told there is none.
static void ArmIdle(RoomInfo* ri, int id)
{
	if (Room_Solo(ri))
	{
		SendIdleCountdown(ri->peerInfos[id].peer, 0);
		return;
	}
	IdleRun(&ri->peerInfos[id]);
	SendIdleCountdown(ri->peerInfos[id].peer, IdleLeftSeconds(&ri->peerInfos[id]));
}

// The number the locked-in players watch while they wait: the longest clock
// still running in the room, which is how long the wait can last - when it
// runs out, its owner is removed and, everybody left having locked in, the
// race loads. Everybody unlocked on the character pick is on a running
// clock, so it only ever reads 0 in the instant before SG_STARTLOADING.
//
// Sent to every locked-in player (their own clock paused) whenever the
// deadline it stands for changes: a lock-in or an unlock, a late joiner, a
// removal. `newcomer` is a slot that has just locked in and has no number
// yet: it is sent one whether or not the deadline moved.
static void RoomWatchUpdate(RoomInfo* ri, int newcomer)
{
	enet_uint32 deadline = 0;
	int left = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (pi->peer == 0 || pi->idleStart == 0)
			continue;
		int ms = IdleLeftMs(pi);
		if (deadline == 0 || ms > left)
		{
			left = ms;
			deadline = pi->idleStart + (enet_uint32)pi->idleLeftMs;
		}
	}

	// Rounded up, never below 1 while a clock runs: the removal ends it.
	int seconds = deadline == 0 ? 0 : (left <= 0 ? 1 : (left + 999) / 1000);
	int changed = deadline != ri->watchDeadline;
	ri->watchDeadline = deadline;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (pi->peer == 0 || !pi->boolLoadSelf || pi->idleStart != 0)
			continue;
		if (changed || i == newcomer)
			SendIdleCountdown(pi->peer, seconds);
	}
}

// Can somebody join this room right now? Not once it is loading or racing,
// and not in the last LOBBY_JOIN_CLOSE_SECONDS of anybody's running pick
// clock: that player is about to be removed or the race is about to load,
// and a newcomer would only restart the wait. Otherwise the room stays open
// through the track, lap and character picks - a late joiner is sent the
// track (CG_JOINROOM) and lands straight on the character pick.
// (LOBBY_JOIN_CLOSE_SECONDS: SV_config.h)
static int RoomOpenForJoin(RoomInfo* ri)
{
	// a page of rooms not on offer yet (ROOM_PAGES_OPEN, global.h: the
	// shortcutless rooms 9-16): shut, so it shows closed and empty everywhere
	// and nobody is let in
	if (!ROOM_OPEN_INDEX((int)(ri - roomInfos)))
		return 0;

	if (ri->boolLoadAll || ri->boolRaceAll)
		return 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (pi->peer == 0 || pi->idleStart == 0)
			continue;
		if (IdleLeftMs(pi) < LOBBY_JOIN_CLOSE_SECONDS * 1000)
			return 0;
	}
	return 1;
}

// The host's clock runs for the track and lap pick, and the whole room is
// told where it stands: the guests sit on "waiting for the host" and get to
// see how long that can last. Only while the room is still open (no track
// yet); after CG_TRACK everybody is on their own clock instead.
static void ArmHostIdle(RoomInfo* ri)
{
	// (while the room votes, nobody's clock runs: Vote_*)
	if (!HasValidHost(ri) || ri->boolRoomLocked || ri->votePhase != VOTE_NONE)
		return;
	// (nor for a host on the way out: their promotion arms the next one's)
	if (ri->peerInfos[ri->hostID].peer->state != ENET_PEER_STATE_CONNECTED)
		return;
	// (and alone, the host's waits for a second player: Solo_Tick)
	if (Room_Solo(ri))
	{
		BroadcastIdleCountdown(ri, 0);
		return;
	}
	IdleRun(&ri->peerInfos[ri->hostID]);
	BroadcastIdleCountdown(ri, IdleLeftSeconds(&ri->peerInfos[ri->hostID]));
}

// Give the room a host if it has none. Called when a player joins, so the
// first person into an empty room becomes host, and so a room that somehow
// lost its host recovers on the next join.
// Sets hostID only; does not announce. It fires only when the room had no
// valid host, which means there is nobody already in the room to tell -
// WelcomeNewClient delivers the announcement to the joiner itself.
static void EnsureHost(RoomInfo* ri, int joiningID)
{
	if (HasValidHost(ri))
		return;

	ri->hostID = (unsigned char)joiningID;
}

// ---------------------------------------------------------- the track pick
//
// ELIMINATION and TAG YOU'RE IT have no lap pick: a round's laps are fixed
// as the race loads (players - 1, or TAG_LAPS: Round_Begin, sent in
// SG_STARTLOADING's round code), so the room's vote skips the lap vote
// (Vote_Finish) and the host's pick goes in as lap row 0 whatever it said
// (CG_TRACK). Row 0, 1 lap, is also what a room with too few players for a
// round plays (round code 0: the client keeps SG_TRACK's laps), so a lone
// player in TAG races one lap, not TAG_LAPS.
static int Room_NoLapPick(RoomInfo* ri)
{
	int row = ROOM_ROW((int)(ri - roomInfos));
	return row == ROOM_ROW_ELIMINATION || row == ROOM_ROW_TAG;
}

// The track and laps are in: the room goes on to the character pick. The
// host's pick (CG_TRACK) or the players' vote (Vote_Finish).
static void Room_TrackIn(RoomInfo* ri, int trackID, int lapID, int voted)
{
	struct SG_MessageTrack s;

	// The track is in. (This no longer closes the room to joiners - see
	// RoomOpenForJoin; a late joiner is sent this track.) voted: by the
	// room's vote, which the host cannot take back (CG_TRACKCANCEL).
	ri->boolRoomLocked = 1;
	ri->trackVoted = voted ? 1 : 0;
	ri->lapPlayed = (unsigned char)lapID;
	ri->levelPlayed = (unsigned char)trackID;

	s.type = SG_TRACK;
	s.voted = ri->trackVoted;
	s.padding = 0;
	s.trackID = trackID;
	s.lapID = lapID;

	PrintPrefix((int)(ri - roomInfos) + 1);
	if (Room_NoLapPick(ri))
		printf("Track #%d was selected (the laps are set as the race loads)\n", trackID);
	else
	{
		static const int lapsByID[8] = { LAP_COUNT_BY_ID };
		printf("Track #%d and %d laps were selected\n", trackID, lapsByID[lapID & 7]);
	}

	broadcastToPeersReliable(ri, &s, sizeof(struct SG_MessageTrack));
	ForeachPeerLPL(SendRoomData);

	// Track and laps are in: the host's clock simply runs on into the
	// character pick, and every guest's starts - or resumes, after a
	// cancelled track. Each is told their own number.
	// (not one on the way out: a kick's, an idle removal's)
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED)
			ArmIdle(ri, i);
}

// ---------------------------------------------------------------- the vote
//
// Unless the host has turned PLAYER VOTING off (CG_HOSTACTION), the room's
// players vote the track and then the laps, in place of the host's pick: a
// vote opens with each lobby round (the first player in, the room back
// from a race) and closes when every player has voted or voteSeconds are
// up (Vote_Tick). The most votes win; a tie, or no vote at all, is drawn
// at random among the options level with the most. Players can change
// their vote until it closes, and see everybody's as it stands (SG_VOTE).
// Nobody's idle clock runs while the room votes - not voting is no reason
// to be removed - and every player's starts with the character pick, as
// it does after the host's pick (Room_TrackIn). A player alone in the room
// votes with no clock at all: the vote closes on their vote, and its time
// starts only when a second player comes in (Vote_Begin, Solo_Tick).

// (0 too while the vote waits for a second player: the client shows no clock)
static int Vote_SecondsLeft(RoomInfo* ri)
{
	int ms;
	if (ri->votePhase == VOTE_NONE || ri->voteHeldMs > 0)
		return 0;
	ms = (int)(ri->voteDeadline - enet_time_get());
	return ms <= 0 ? 1 : (ms + 999) / 1000;
}

// A player still to be heard: connected (not on the way out - a kick).
static int Vote_Voter(PeerInfo* pi)
{
	return pi->peer != 0 && pi->peer->state == ENET_PEER_STATE_CONNECTED;
}

// (only the votes the tally counts: Vote_Finish)
static void Vote_Fill(RoomInfo* ri, struct SG_MessageVote* sg)
{
	memset(sg, 0, sizeof(*sg));
	sg->type = SG_VOTE;
	sg->phase = ri->votePhase;
	sg->enabled = !ri->votingOff;
	sg->seconds = (unsigned char)Vote_SecondsLeft(ri);
	sg->track = ri->voteTrack;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (Vote_Voter(&ri->peerInfos[i]))
			sg->votes[i] = ri->peerInfos[i].vote;
}

// To one player: on the way in (WelcomeNewClient).
static void Vote_Send(RoomInfo* ri, ENetPeer* peer)
{
	struct SG_MessageVote sg;
	Vote_Fill(ri, &sg);
	sendToPeerReliable(peer, &sg, sizeof(sg));
}

static void Vote_Broadcast(RoomInfo* ri)
{
	struct SG_MessageVote sg;
	Vote_Fill(ri, &sg);
	broadcastToPeersReliable(ri, &sg, sizeof(sg));
}

// A vote open, with nobody's vote in yet; nobody told (the caller does).
// Alone in the room, its time waits for a second player (Solo_Tick): it
// closes on the one vote.
static void Vote_Begin(RoomInfo* ri, int phase)
{
	ri->votePhase = (char)phase;
	ri->voteHeldMs = Room_Solo(ri) ? voteSeconds * 1000 : 0;
	ri->voteDeadline = enet_time_get() + (enet_uint32)voteSeconds * 1000;
	for (int i = 0; i < MAX_CLIENTS; i++)
		ri->peerInfos[i].vote = 0;
}

// ...and the room told, the host's clock stopped: nobody's runs while it
// votes.
static void Vote_Start(RoomInfo* ri, int phase)
{
	Vote_Begin(ri, phase);
	if (HasValidHost(ri))
		IdlePause(&ri->peerInfos[ri->hostID]);

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("%s vote open for %ds\n", phase == VOTE_TRACK ? "Track" : "Lap", voteSeconds);

	Vote_Broadcast(ri);
}


// The vote closed: the option with the most votes, a tie drawn at random.
static void Vote_Finish(RoomInfo* ri)
{
	int counts[VOTE_TRACKS];
	int options = ri->votePhase == VOTE_TRACK ? VOTE_TRACKS : VOTE_LAP_ROWS;
	int best = 0, tied = 0, pick, i;

	memset(counts, 0, sizeof(counts));
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (Vote_Voter(pi) && pi->vote != 0 && pi->vote <= options)
			counts[pi->vote - 1]++;
	}
	for (i = 0; i < options; i++)
	{
		if (counts[i] > best) { best = counts[i]; tied = 0; }
		if (counts[i] == best) tied++;
	}

	// one of the options level with the most, at random (with no vote at
	// all, every option is)
	pick = 0;
	{
		int r = rand() % tied;
		for (i = 0; i < options; i++)
			if (counts[i] == best && r-- == 0) { pick = i; break; }
	}

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("%s vote closed (option %d with %d vote%s%s)\n",
		ri->votePhase == VOTE_TRACK ? "Track" : "Lap", pick, best, best == 1 ? "" : "s",
		tied > 1 ? " (drawn from a tie)" : "");

	if (ri->votePhase == VOTE_TRACK && Room_NoLapPick(ri))
	{
		// No lap vote in ELIMINATION and TAG (Room_NoLapPick): the track goes
		// straight in, on lap row 0. The phase is cleared first, as the lap
		// vote's close does below: left on VOTE_TRACK, the next tick's
		// Vote_Tick would see everybody voted and close this vote again -
		// a fresh Room_TrackIn every tick, a tie drawn anew each time.
		ri->voteTrack = (unsigned char)pick;
		ri->votePhase = VOTE_NONE;
		Room_TrackIn(ri, pick, 0, 1);
	}
	else if (ri->votePhase == VOTE_TRACK)
	{
		ri->voteTrack = (unsigned char)pick;
		Vote_Start(ri, VOTE_LAPS);
	}
	else
	{
		ri->votePhase = VOTE_NONE;
		Room_TrackIn(ri, ri->voteTrack, pick, 1);
	}
}

// Each tick (ServerState_Tick): the vote closed when every player has voted
// or its time is up.
static void Vote_Tick(RoomInfo* ri)
{
	int all = 1, anybody = 0;

	if (ri->votePhase == VOTE_NONE || ri->boolLoadAll)
		return;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (!Vote_Voter(pi))
			continue;
		anybody = 1;
		if (pi->vote == 0)
			all = 0;
	}

	// (a vote waiting for a second player has no time to run out)
	if ((anybody && all) || (ri->voteHeldMs == 0 && (int)(ri->voteDeadline - enet_time_get()) <= 0))
		Vote_Finish(ri);
}

// Each tick, ahead of the vote's (ServerState_Tick): the room's clocks put
// right for how many are in it (Room_Solo) - every tick, not only when the
// count changes, since a join and a leave (or two joins into an empty room)
// can land in one ProcessNewMessages batch and leave a clock or a vote as
// the moment between them had it. Alone: every running clock pauses,
// keeping what is left, a vote's time with them, and the room is told there
// is no clock (0: the client hides TIME LEFT). Two or more: the clocks the
// room's stage runs, running - the vote's on from what it had, the host's
// for the track and lap pick, every unlocked player's on the character pick
// (the locked-in players' number follows: RoomWatchUpdate). Each only if it
// is not as it should be, so the room is told only what changes; only
// connected players' clocks (Vote_Voter: not one on its way out, a kick or
// an idle removal). Not while the race loads or runs: no clock runs then,
// and the reset after it (the vote, the host's clock) asks Room_Solo itself.
static void Solo_Tick(RoomInfo* ri)
{
	int solo, changed = 0;

	if (ri->boolLoadAll)
		return;
	solo = Room_Solo(ri);

	if (solo != ri->soloHold)
	{
		ri->soloHold = (char)solo;
		PrintPrefix((int)(ri - roomInfos) + 1);
		printf(solo ? "One player in the room: its clocks wait for a second\n"
		            : "A second player in the room: its clocks run\n");
	}

	if (solo)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
			if (ri->peerInfos[i].idleStart != 0)
			{
				IdlePause(&ri->peerInfos[i]);
				changed = 1;
			}
		if (ri->votePhase != VOTE_NONE && ri->voteHeldMs == 0)
		{
			int ms = (int)(ri->voteDeadline - enet_time_get());
			ri->voteHeldMs = ms > 1000 ? ms : 1000;
			changed = 1;
		}
		if (changed)
		{
			ri->watchDeadline = 0;
			BroadcastIdleCountdown(ri, 0);
			if (ri->votePhase != VOTE_NONE)
				Vote_Broadcast(ri);
		}
		return;
	}

	if (ri->votePhase != VOTE_NONE)
	{
		if (ri->voteHeldMs > 0)
		{
			ri->voteDeadline = enet_time_get() + (enet_uint32)ri->voteHeldMs;
			ri->voteHeldMs = 0;
			Vote_Broadcast(ri);
		}
	}
	else if (!ri->boolRoomLocked)
	{
		if (HasValidHost(ri) && ri->peerInfos[ri->hostID].idleStart == 0 && Vote_Voter(&ri->peerInfos[ri->hostID]))
			ArmHostIdle(ri);
	}
	else
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			PeerInfo* pi = &ri->peerInfos[i];
			if (Vote_Voter(pi) && !pi->boolLoadSelf && pi->idleStart == 0)
				ArmIdle(ri, i);
		}
}

// --------------------------------------------------------------- the rounds
//
// The game modes of room rows 2-5 (ROOM_IS_MODE: POTION GAME, BOMBARDMENT,
// ELIMINATION, TAG YOU'RE IT; the rules are in global.h, under "the game
// modes' rounds"). The server is the referee. Each ROM reports only what
// happened to its own kart - every hurt, with its damage type, reason,
// attacker and whether a shield stopped it (NET_EV_KART_HURT, relayed to the
// room as it always was), and in ELIMINATION each lap it completes
// (NET_EV_LAP, taken here and relayed to nobody) - and this decides who is
// out, numbers the places and tells the room (NET_EV_OUT, NET_EV_IT, which
// the client writes into octr->roundPlace and roundIt for gameModes.c). Why
// here: two machines can each see their own near-simultaneous knock-out
// first, and the server's arrival order is one everybody can agree on; TAG
// needs one clock and ELIMINATION one crossing order; and an out player's
// CG_ENDRACE must not start the 60 s lobby clock the way a finish does.
//
// The players are seated as the race loads (Round_Begin, at SG_STARTLOADING,
// whose spare nibble carries the round's code to the clients), but nothing
// is judged before the race runs (Round_Green, at SG_STARTRACE): a player
// who leaves during the 5 s countdown or the load just leaves the count, and
// with fewer than 2 left at the green there is no round, a plain race. (A
// round settled during the load would set off the everybody-ended reset
// below while the room is still loading, and its verdicts would reach
// clients not yet in the race.) From the green a player is in until placed:
// out, from the bottom (Round_Out); finished, from the top (Round_Finish);
// or out with no place for leaving (Round_Leave). The round ends the moment
// 1 or none are still in (Round_Check): the last one standing takes the best
// place left (1st if nobody finished), and every player's race is over, so
// the ordinary everybody-ended path (ServerState_Tick, boolEndAll) shows the
// results for RACE_OVER_COUNTDOWN_SECONDS and takes the room back to the
// lobby. The room is closed to joiners from the load to that reset
// (RoomOpenForJoin), so the players in it are the round's.
//
// Places are numbered as the verdicts come: an out player takes the worst
// place nobody has (finished + still in), a finisher the best. A leaver takes
// none and leaves the count, so the places given can skip one (4 players: A
// out 4th, B leaves, C out 2nd, D 1st); the ROM's results list the rows in
// place order and number them by row (endOfRaceUI.c).
//
// One log line per verdict, as the other room events: "Player BOT4 (5) out:
// POTION by BOT2 (1), place 7".

// A round is refereed from the green, not from SG_STARTRACE, and TAG's clock
// starts there: from that message on the ROM's traffic lights count
// trafficLightsTimer down from 0xf40 (StatePS1_Game_WaitForRace, states.c)
// at 960 a second, 4.07 s.
#ifndef TAG_LIGHTS_MS
#define TAG_LIGHTS_MS 4070
#endif

// ROUND_CAUSE_* for the log
static const char* const roundCauseNames[8] =
	{ "?", "POISONED", "BOMBED", "LAST", "TIME UP", "LEFT", "FINISHED", "END" };

static int Round_Row(RoomInfo* ri)
{
	return ROOM_ROW((int)(ri - roomInfos));
}

// One word to the whole room, the player it concerns included (their own
// machine acts on it too): an SG_WEAPONEVENT of a kind only the server
// sends (NET_EV_SERVER_MASK, refused from a client in CG_WEAPONEVENT),
// reliable on channel 0, so it lands after the KART_HURT it answers.
static void Round_Send(RoomInfo* ri, int kind, int id, int owner, int serial, int arg)
{
	struct SG_MessageWeaponEvent s;
	s.type = SG_WEAPONEVENT;
	s.clientID = id & 0xF;
	s.owner = owner & 0xF;
	s.kind = kind & 0xF;
	s.serial = (unsigned char)serial;
	s.arg = (unsigned char)arg;
	broadcastToPeersReliable(ri, &s, sizeof(struct SG_MessageWeaponEvent));
}

// A player the round still has: in the room and not placed. (Nobody joins
// mid-round, so everybody in the room was seated as it loaded; a leaver's
// seat is empty.) Any ID: out of range is nobody, so a client's 4-bit field
// can be passed as it came.
static int Round_IsIn(RoomInfo* ri, int id)
{
	return id >= 0 && id < MAX_CLIENTS &&
		ri->peerInfos[id].peer != 0 && ri->peerInfos[id].place == 0;
}

static int Round_InCount(RoomInfo* ri)
{
	int n = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (Round_IsIn(ri, i))
			n++;
	return n;
}

// A player placed, the room told: NET_EV_OUT with the player, the attacker
// (ROUND_NONE none), the cause - with ROUND_LAST on the verdict that ends
// the round - and the place (ROUND_NONE for a leaver). And the log's line.
static void Round_Place(RoomInfo* ri, int id, int place, int cause, int attacker, int last)
{
	PeerInfo* pi;

	if (id < 0 || id >= MAX_CLIENTS)
		return;   // (every caller's is a seat)
	pi = &ri->peerInfos[id];

	if (attacker < 0 || attacker >= MAX_CLIENTS)
		attacker = ROUND_NONE;
	pi->place = (unsigned char)place;
	pi->cause = (unsigned char)cause;
	Round_Send(ri, NET_EV_OUT, id, attacker, cause | (last ? ROUND_LAST : 0), place);

	PrintPrefix((int)(ri - roomInfos) + 1);
	if (cause == ROUND_CAUSE_FINISH)
		printf("Player %.*s (%d) finished in place %d\n", NAME_LEN, &pi->name[0], id, place);
	else if (cause == ROUND_CAUSE_END)
		printf("Player %.*s (%d) still in at the round's end, place %d%s\n",
			NAME_LEN, &pi->name[0], id, place, place == 1 ? ": the winner" : "");
	else
	{
		printf("Player %.*s (%d) out: %s", NAME_LEN, &pi->name[0], id, roundCauseNames[cause & 7]);
		if (attacker != ROUND_NONE)
			printf(" by %.*s (%d)", NAME_LEN, &ri->peerInfos[attacker].name[0], attacker);
		if (place == ROUND_NONE)
			printf(", no place\n");
		else
			printf(", place %d\n", place);
	}
}

// TAG: the clock from now, tagSeconds - every tag and every new "it" starts
// it over (Matt). Before the green it stays stopped: Tag_Tick starts it
// there.
static void Tag_Arm(RoomInfo* ri)
{
	if (ri->itGreenAt != 0)
		return;
	ri->itDeadline = enet_time_get() + (enet_uint32)tagSeconds * 1000;
	if (ri->itDeadline == 0)
		ri->itDeadline = 1;   // 0 is "no clock"
}

// TAG: the room told who is it (ROUND_NONE nobody), who tagged them
// (ROUND_NONE: drawn, or only the clock starting), whether the clock runs,
// and its seconds - always the full tagSeconds, as every message here goes
// with a clock just started over. The clients count it down themselves.
static void Tag_Send(RoomInfo* ri, int tagger)
{
	int none = ri->itID >= MAX_CLIENTS;
	int running = !none && ri->itGreenAt == 0 && ri->itDeadline != 0;

	Round_Send(ri, NET_EV_IT, none ? ROUND_NONE : ri->itID, tagger,
		running ? 1 : 0, none ? 0 : tagSeconds);
}

// TAG: a new "it", drawn among the players still in, on a fresh clock: the
// round's first (first: --tagfirst can name it), and after "it" went out on
// the clock or left. With fewer than 2 in there is nobody to chase: nobody
// is it, and Round_Check, which always follows, ends the round (and tells
// the room).
static void Tag_NewIt(RoomInfo* ri, int first)
{
	int in[MAX_CLIENTS];
	int n = 0;
	int named;

	for (int i = 0; i < MAX_CLIENTS; i++)
		if (Round_IsIn(ri, i))
			in[n++] = i;

	ri->itDeadline = 0;
	if (n < 2)
	{
		ri->itID = ROUND_NONE;
		return;
	}

	named = first && Round_IsIn(ri, tagFirst);
	ri->itID = (unsigned char)(named ? tagFirst : in[rand() % n]);
	Tag_Arm(ri);
	Tag_Send(ri, ROUND_NONE);

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %.*s (%d) is it (%s), %ds on the clock%s\n",
		NAME_LEN, &ri->peerInfos[ri->itID].name[0], ri->itID,
		named ? "--tagfirst" : "drawn", tagSeconds,
		ri->itGreenAt != 0 ? " from the green" : "");
}

// The round's end, the moment 1 or none are still in: the last one standing
// takes the best place left (1st if nobody finished: the winner), on the
// verdict that carries ROUND_LAST - or, nobody left, a bare ROUND_LAST.
// Every player's race is then over (boolEndSelf): their ROMs go to the
// results on the verdicts (gameModes.c), and the everybody-ended path counts
// RACE_OVER_COUNTDOWN_SECONDS down and resets the room (ServerState_Tick).
// In TAG nobody is it any more.
static void Round_Check(RoomInfo* ri)
{
	int last = -1;

	if (!ri->roundLive || ri->roundOver || Round_InCount(ri) > 1)
		return;
	ri->roundOver = 1;

	if (Round_Row(ri) == ROOM_ROW_TAG)
	{
		ri->itID = ROUND_NONE;
		ri->itGreenAt = 0;
		ri->itDeadline = 0;
		Tag_Send(ri, ROUND_NONE);
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
		if (Round_IsIn(ri, i))
			last = i;

	if (last >= 0)
	{
		ri->roundDone++;
		Round_Place(ri, last, ri->roundDone, ROUND_CAUSE_END, ROUND_NONE, 1);
	}
	else
	{
		Round_Send(ri, NET_EV_OUT, ROUND_NONE, ROUND_NONE, ROUND_LAST, ROUND_NONE);
		PrintPrefix((int)(ri - roomInfos) + 1);
		printf("Round over with nobody left in\n");
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0)
			ri->peerInfos[i].boolEndSelf = 1;
}

// Out, placed from the bottom: the worst place nobody has yet - below
// everybody still in, above everybody out before. Nothing for a player
// already placed (the second report of one knock-out, a hurt on the way to
// the results), before the green or once the round is over. In TAG, "it"
// out (the clock) makes way for a new one.
static void Round_Out(RoomInfo* ri, int id, int cause, int attacker)
{
	int place;

	if (!ri->roundLive || ri->roundOver || !Round_IsIn(ri, id))
		return;

	place = ri->roundDone + Round_InCount(ri);
	Round_Place(ri, id, place, cause, attacker, 0);

	if (Round_Row(ri) == ROOM_ROW_TAG && id == ri->itID)
		Tag_NewIt(ri, 0);
	Round_Check(ri);
}

// ELIMINATION: each lap, from the first, is decided the moment every player
// still in but one has completed it. That one is out, last across (LAST),
// without having to cross - a stalled player never stalls the race - and
// ties go by the server's arrival order. A finish or a leave can decide one
// too, and more than one lap at a time: this runs until the reports decide
// no more, or the round is over. A lap everybody still in has completed
// (the one behind left or finished meanwhile) puts nobody out.
static void Elim_Judge(RoomInfo* ri)
{
	while (ri->roundLive && !ri->roundOver && ri->elimLap <= ri->roundLaps)
	{
		int in = 0, done = 0, straggler = -1;

		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (!Round_IsIn(ri, i))
				continue;
			in++;
			if (ri->peerInfos[i].lapsDone >= ri->elimLap)
				done++;
			else
				straggler = i;
		}
		if (in < 2 || done < in - 1)
			return;

		// the lap first: Round_Out can end the round, and this loop with it
		ri->elimLap++;
		if (straggler >= 0)
			Round_Out(ri, straggler, ROUND_CAUSE_LAST, ROUND_NONE);
	}
}

// Finished: across the last lap's line (CG_ENDRACE from a player still in;
// in ELIMINATION also their last NET_EV_LAP, whichever comes first), placed
// from the top. In ELIMINATION a finish can decide a lap the others are on
// (Elim_Judge); in TAG, which nobody finishes (TAG_LAPS), a finish from "it"
// would leave nobody it, so a new one is drawn.
static void Round_Finish(RoomInfo* ri, int id)
{
	if (!ri->roundLive || ri->roundOver || !Round_IsIn(ri, id))
		return;

	ri->roundDone++;
	Round_Place(ri, id, ri->roundDone, ROUND_CAUSE_FINISH, ROUND_NONE, 0);

	if (Round_Row(ri) == ROOM_ROW_ELIMINATION)
		Elim_Judge(ri);
	if (Round_Row(ri) == ROOM_ROW_TAG && id == ri->itID)
		Tag_NewIt(ri, 0);
	Round_Check(ri);
}

// A player gone from the room (ProcessDisconnectEvent, their seat already
// emptied). Before the green they only leave the count (Round_Green decides
// whether 2 are left for a round). In a live round a player still in is out
// with no place (LEFT) - the empty SG_NAME that follows wipes their name, so
// the results skip them - "it" leaving makes way for a new one on a fresh
// clock, and in ELIMINATION the lap they were holding up may now be decided.
// A player already placed keeps their place.
static void Round_Leave(RoomInfo* ri, int id)
{
	if (id < 0 || id >= MAX_CLIENTS)
		return;
	if (!ri->roundLive || ri->roundOver || ri->peerInfos[id].place != 0)
		return;

	Round_Place(ri, id, ROUND_NONE, ROUND_CAUSE_LEFT, ROUND_NONE, 0);

	if (Round_Row(ri) == ROOM_ROW_TAG && id == ri->itID)
		Tag_NewIt(ri, 0);
	if (Round_Row(ri) == ROOM_ROW_ELIMINATION)
		Elim_Judge(ri);
	Round_Check(ri);
}

// ELIMINATION: a player's lap done (NET_EV_LAP, arg the lap just reached:
// their kart's lapIndex). Any lap higher than the last one heard from them
// counts, not only the next: a report lost to a full event ring (NetEv_Send,
// netWeapons.c) must not leave them behind on that lap for good, and so out
// as its straggler. Their last lap is their finish; any other may decide a
// lap (Elim_Judge).
static void Elim_OnLap(RoomInfo* ri, int id, int lap)
{
	if (Round_Row(ri) != ROOM_ROW_ELIMINATION || !ri->roundLive || ri->roundOver || !Round_IsIn(ri, id))
		return;
	if (lap <= ri->peerInfos[id].lapsDone)
		return;
	ri->peerInfos[id].lapsDone = (unsigned char)lap;

	if (lap >= ri->roundLaps)
		Round_Finish(ri, id);
	else
	{
		Elim_Judge(ri);
		Round_Check(ri);
	}
}

// A kart's hurt (NET_EV_KART_HURT from the victim's own machine, already
// relayed to the room): the mode's rule. Only a hurt that took counts - a
// shield's block (NET_KART_BLOCKED) never does - and only on a player still
// in. arg's bits 0-2 are the damage type, 3-5 the reason
// (VehPickState_NewState; the ROM's gate, netWeapons.c); the attacker is the
// owner field when serial says there is one.
//   POTION GAME: type 1, reason 2 (a beaker driven into) or 4 (a thrown one
//     still moving): a beaker, and only a beaker hurts that way - the
//     victim's own too.
//   BOMBARDMENT: type 2, reason 1: a bomb's blast (or a thrown shield's
//     burst, which a room of bombs only cannot have) costs a life: the room
//     is told the lives left (NET_EV_LIVES), and the last one puts the
//     player out.
//   TAG YOU'RE IT: type 2, reason 3, a missile, from "it" itself, at a player
//     still in: the victim is "it" now, and the clock starts over. A missile
//     from anybody else is just a blast. (One "it" fired before becoming it
//     counts too: the hurt names its attacker, not the shot.)
//   ELIMINATION: hurts put nobody out.
static void Round_OnHurt(RoomInfo* ri, int victim, const struct CG_MessageWeaponEvent* r)
{
	int type = r->arg & 7;
	int reason = (r->arg >> 3) & 7;
	int attacker = ((r->serial & 1) && r->owner < MAX_CLIENTS) ? r->owner : ROUND_NONE;   // count bit 0 (bits 1-4 the hurt's number)
	int row = Round_Row(ri);

	if (!ri->roundLive || ri->roundOver || !Round_IsIn(ri, victim))
		return;
	if (r->arg & NET_KART_BLOCKED)
		return;

	if (row == ROOM_ROW_POTION_GAME && type == 1 && (reason == 2 || reason == 4))
		Round_Out(ri, victim, ROUND_CAUSE_POTION, attacker);
	else if (row == ROOM_ROW_BOMBARDMENT && type == 2 && reason == 1)
	{
		PeerInfo* pv = &ri->peerInfos[victim];
		if (pv->lives > 1)
		{
			pv->lives--;
			Round_Send(ri, NET_EV_LIVES, victim, attacker, 0, pv->lives);
			PrintPrefix((int)(ri - roomInfos) + 1);
			printf("Player %.*s (%d) lost a life, %d left\n",
				NAME_LEN, &pv->name[0], victim, pv->lives);
		}
		else
		{
			pv->lives = 0;
			Round_Out(ri, victim, ROUND_CAUSE_BOMB, attacker);
		}
	}
	else if (row == ROOM_ROW_TAG && type == 2 && reason == 3 &&
		attacker != ROUND_NONE && attacker == ri->itID && victim != attacker)
	{
		ri->itID = (unsigned char)victim;
		Tag_Arm(ri);
		Tag_Send(ri, attacker);

		PrintPrefix((int)(ri - roomInfos) + 1);
		printf("Player %.*s (%d) tagged by %.*s (%d): is it, %ds on the clock\n",
			NAME_LEN, &ri->peerInfos[victim].name[0], victim,
			NAME_LEN, &ri->peerInfos[attacker].name[0], attacker, tagSeconds);
	}
}

// TAG, as the race runs (Round_Green): the first "it", its clock pending
// until the green.
static void Tag_Start(RoomInfo* ri)
{
	ri->itGreenAt = enet_time_get() + TAG_LIGHTS_MS;
	if (ri->itGreenAt == 0)
		ri->itGreenAt = 1;   // 0 is "started"
	Tag_NewIt(ri, 1);
}

// Each tick (ServerState_Tick), TAG: the clock starts at the green, and
// "it" is out when it runs out (Round_Out draws the next). Nothing once the
// round is over.
static void Tag_Tick(RoomInfo* ri)
{
	enet_uint32 now;

	if (!ri->roundLive || ri->roundOver || Round_Row(ri) != ROOM_ROW_TAG)
		return;
	now = enet_time_get();

	if (ri->itGreenAt != 0 && (int)(now - ri->itGreenAt) >= 0)
	{
		ri->itGreenAt = 0;
		if (Round_IsIn(ri, ri->itID))
		{
			Tag_Arm(ri);
			Tag_Send(ri, ROUND_NONE);
		}
		else
			Tag_NewIt(ri, 0);   // (never: "it" leaving draws a new one)
	}

	if (ri->itDeadline != 0 && (int)(now - ri->itDeadline) >= 0)
	{
		ri->itDeadline = 0;
		if (Round_IsIn(ri, ri->itID))
			Round_Out(ri, ri->itID, ROUND_CAUSE_TIME, ROUND_NONE);
		else
			Tag_NewIt(ri, 0);   // (never, as above)
	}
}

// The race loads (SG_STARTLOADING): a round if this is a mode room with 2 or
// more players seated - those in the room now, every one locked in (not
// clientCount, which never shrinks: SendRoomData). Returns the round's code
// for SG_STARTLOADING's nibble (global.h, ROUND_CODE_*): 0 no round, and the
// client keeps SG_TRACK's laps; ELIMINATION its laps, players - 1 (2 players
// race 1 lap, the final; 12 race 11); POTION GAME and BOMBARDMENT the laps
// the room picked (ROUND_CODE_VOTED); TAG endless (ROUND_CODE_ENDLESS,
// TAG_LAPS).
static int Round_Begin(RoomInfo* ri)
{
	static const int lapsByID[8] = { LAP_COUNT_BY_ID };
	int rn = (int)(ri - roomInfos);
	int row = ROOM_ROW(rn);
	int seated = 0;
	int code;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		pi->place = 0;
		pi->cause = 0;
		pi->lapsDone = 0;
		pi->lives = (row == ROOM_ROW_BOMBARDMENT) ? BOMB_LIVES : 0;
		if (pi->peer != 0)
			seated++;
	}

	ri->roundOn = (char)(ROOM_IS_MODE(rn) && seated >= 2);
	ri->roundLive = 0;
	ri->roundOver = 0;
	ri->roundGreenAt = 0;
	ri->roundDone = 0;
	ri->roundLaps = 0;
	ri->elimLap = 1;
	ri->itID = ROUND_NONE;
	ri->itGreenAt = 0;
	ri->itDeadline = 0;

	if (!ri->roundOn)
	{
		if (ROOM_IS_MODE(rn))
		{
			PrintPrefix(rn + 1);
			printf("No round: %d player loading a plain race\n", seated);
		}
		return 0;
	}

	if (row == ROOM_ROW_ELIMINATION)
	{
		code = seated - 1;
		if (code < 1)
			code = 1;
		if (code >= ROUND_CODE_VOTED)
			code = ROUND_CODE_VOTED - 1;
		ri->roundLaps = (unsigned char)code;
	}
	else if (row == ROOM_ROW_TAG)
	{
		code = ROUND_CODE_ENDLESS;
		ri->roundLaps = TAG_LAPS;
	}
	else
	{
		code = ROUND_CODE_VOTED;
		ri->roundLaps = (unsigned char)lapsByID[ri->lapPlayed & 7];
	}

	PrintPrefix(rn + 1);
	if (row == ROOM_ROW_TAG)
		printf("Round: %d players, endless laps, %ds clock, refereed from the green\n", seated, tagSeconds);
	else
		printf("Round: %d players over %d lap%s, refereed from the green\n",
			seated, ri->roundLaps, ri->roundLaps == 1 ? "" : "s");
	return code;
}

// The race runs (SG_STARTRACE): its green light shows TAG_LIGHTS_MS later,
// and the round is decided there (Round_Tick) - a leaver during the load or
// the lights just leaves the count, and nothing is judged or placed while
// the lights still show (a verdict there would put a player on the results
// before their race had begun). TAG's first "it" is drawn now, to be seen
// through the lights, its clock pending until the green.
static void Round_Green(RoomInfo* ri)
{
	if (!ri->roundOn)
		return;

	ri->roundGreenAt = enet_time_get() + TAG_LIGHTS_MS;
	if (ri->roundGreenAt == 0)
		ri->roundGreenAt = 1;   // 0 is "none pending"
	if (Round_Row(ri) == ROOM_ROW_TAG)
		Tag_Start(ri);
}

// Each tick (ServerState_Tick), at the round's green: refereed from here on
// with 2 or more still in. Otherwise there is no round after all - a plain
// race, nothing judged - and the room is told (NET_EV_OUT about nobody, no
// cause, not the last: the "round off" marker), so the clients take the
// round flag and, in ELIMINATION and TAG, its laps back (1 lap: their lap
// pick is row 0) and the lone player's race ends at a line it can reach.
static void Round_Tick(RoomInfo* ri)
{
	int in;

	if (!ri->roundOn || ri->roundLive || ri->roundGreenAt == 0)
		return;
	if ((int)(enet_time_get() - ri->roundGreenAt) < 0)
		return;
	ri->roundGreenAt = 0;

	in = Round_InCount(ri);
	if (in < 2)
	{
		ri->roundOn = 0;
		ri->itID = ROUND_NONE;
		ri->itGreenAt = 0;
		ri->itDeadline = 0;
		Round_Send(ri, NET_EV_OUT, ROUND_NONE, ROUND_NONE, 0, 0);
		PrintPrefix((int)(ri - roomInfos) + 1);
		printf("No round: %d player left at the green, a plain race\n", in);
		return;
	}

	ri->roundLive = 1;
	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Round live at the green: %d players in\n", in);
}

void ProcessDisconnectEvent(ENetPeer* peer)
{
	//identify which client ID this came from
	int peerID = -1;
	int numAlive = 0;

	RoomInfo* ri = &roomInfos[0];

	//identify which client ID this came from
	GetDriverFromRace(peer, &ri, &peerID);

	// driver is not in a room, just quit
	if (peerID == -1)
	{
		// This return used to skip the list removal, so every connection that
		// came and went without joining a room left a node behind - pointing at
		// a peer slot ENet reuses - and the list only ever grew.
		RemovePeerLPL(peer);
		enet_peer_disconnect_now(peer, 0);
		//printf("Disconnection from Room Selection\n");
		return;
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0)
			numAlive++;

	// subtract one, cause one of the peers was still
	// alive in the for-loop that is about to be nullified
	numAlive -= 1;

	int didRemove = RemovePeerLPL(peer);
	PrintPrefix((int)(ri - roomInfos) + 1);
	if (!didRemove)
		printf("Player %.*s (%d) disconnected from room, and yet LPL did not contain them, how did they avoid being added to LPL?\n",
			NAME_LEN, &ri->peerInfos[peerID].name[0],
			peerID);
	else
		printf("Player %.*s (%d) disconnected from room\n",
			NAME_LEN, &ri->peerInfos[peerID].name[0],
			peerID);

	// no players are left
	int noneAlive = numAlive == 0;
	//race is in session and (1 or less players AND non-race map)
	int oneOrLessOrNonRaceMap = (ri->boolRaceAll == 1) && ((numAlive <= 1) && (ri->levelPlayed > 18));
	// Kill lobby under either of these conditions
	int killLobby = noneAlive || oneOrLessOrNonRaceMap;
	if (killLobby)
	{
		PrintPrefix((int)(ri - roomInfos) + 1);
		if (noneAlive)
			printf("Room has been killed as no players remain\n");
		else if (oneOrLessOrNonRaceMap)
			printf("Room has been killed as there are either one or less players or it's a non-race map\n");
		else
			printf("Room has been killed\n");

		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (ri->peerInfos[i].peer == 0)
				continue;

			// off the room list's mailing list too: disconnect_now resets
			// the peer with no DISCONNECT event to take it off later, and
			// SendRoomData would go on writing to the dead peer
			RemovePeerLPL(ri->peerInfos[i].peer);
			enet_peer_disconnect_now(ri->peerInfos[i].peer, 0);
		}

		memset(ri, 0, sizeof(RoomInfo));
	}
	// Only disconnect one player as long as
	// more racers remain on Arcade track,
	// or if disconnected from Battle/Adv during the race
	else
	{
		enet_peer_disconnect_now(ri->peerInfos[peerID].peer, 0);
		ri->peerInfos[peerID].peer = NULL;

		// Host migration. Must sit ABOVE the boolEndSelf early return below:
		// a host who quits after crossing the finish line takes that return,
		// and the promotion would be skipped every single time - which is the
		// most likely way to lose a host in practice.
		if (peerID == ri->hostID)
		{
			int newHost = PickNewHost(ri);

			if (newHost != -1)
			{
				ri->hostID = (unsigned char)newHost;
				SendHost(ri);

				// Same room number the surrounding logging uses, via pointer
				// subtraction rather than the (unsigned int) cast used above -
				// that cast is what raises C4311 on x64.
				PrintPrefix((int)(ri - &roomInfos[0]) + 1);
				printf("The host (player %d) left so the room was migrated to player %d\n", peerID, newHost);

				// The new host is on the clock for the track pick now (a
				// no-op once the track is in - then they already hold the
				// character clock like everybody else).
				ArmHostIdle(ri);
			}
			// If the room is now empty there is nobody to promote. Leave
			// hostID as it is: HasValidHost() reports false because the peer
			// is gone, so the next player to join claims it.
		}

		// A game mode's round (Round_*): a player still in is out with no
		// place, which can end the round. Above the boolEndSelf early return
		// below, like the host's promotion, so that no leaver can slip past
		// the round whatever their race's state; Round_Leave itself leaves a
		// player already placed alone. Their seat is already empty, so the
		// verdict goes to the others only, they no longer count as in, and
		// a round's end here (Round_Check) does not mark their race ended:
		// the empty SG_NAME below still goes out for them.
		Round_Leave(ri, peerID);

		// if this client ended race, dont tell
		// anyone that the client disconnected - but the room list is
		// told: their seat is free, and everybody on it saw them in it
		if (ri->peerInfos[peerID].boolEndSelf != 0)
		{
			ForeachPeerLPL(SendRoomData);
			return;
		}

		// dont block gameplay for everyone else
		ri->peerInfos[peerID].boolLoadSelf = 1;
		ri->peerInfos[peerID].boolRaceSelf = 1;

		struct SG_MessageName sgBuffer;
		struct SG_MessageName* s = &sgBuffer;

		// send NULL name to all OTHER clients
		s->type = SG_NAME;
		s->clientID = peerID;
		s->numClientsTotal = ri->clientCount;
		memset(&s->name[0], 0, sizeof(s->name));

		broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageName));

		// (The old "if (peerID == 0)" TODO lived here. Its plan was to call
		// WelcomeNewClient to re-seat players, but that would have wiped the
		// lobby: the client's SG_NEWCLIENT handler memsets nameBuffer,
		// boolLockedInCharacters and raceStats and forces the state back to
		// LOBBY_ASSIGN_ROLE. Migrating the host role instead of renumbering
		// players avoids that entirely - see the block above.)
	}

	// a vote running: the leaver's vote is gone from it (and, everybody
	// left having voted, the tick closes it)
	if (!killLobby && ri->votePhase != VOTE_NONE)
		Vote_Broadcast(ri);

	ForeachPeerLPL(SendRoomData); //someone left a room, notify everyone else.
}

void WelcomeNewClient(RoomInfo* ri, int id)
{
	// Send acceptance to client
	struct SG_MessageClientStatus mw;
	mw.type = SG_NEWCLIENT;
	mw.clientID = id;
	mw.numClientsTotal = ri->clientCount;

	// ordinary day
	mw.special = 0;
#if 0
	if (GetWeekDay() == 1) mw.special = 1; // Monday
	if (GetWeekDay() == 3) mw.special = 2; // Wednesday
	if (GetWeekDay() == 5) mw.special = 3; // Friday
#endif

	sendToPeerReliable(ri->peerInfos[id].peer, &mw, sizeof(struct SG_MessageClientStatus));

	// SG_NEWCLIENT makes this client forget who the host is - it clears hostID
	// so a stale value can never leave somebody believing they are in charge.
	// Re-announcing here, rather than at each call site, is deliberate: the
	// post-race room reset also calls this function, and when the follow-up
	// lived at the call sites that path was missed, so a room came back from a
	// race with no host and everybody sat on "waiting for the host to select a
	// track". Keeping the pair together makes that impossible to forget.
	if (HasValidHost(ri))
	{
		struct SG_MessageHost sgHost;
		sgHost.type   = SG_HOST;
		sgHost.hostID = ri->hostID & 0xF;

		sendToPeerReliable(ri->peerInfos[id].peer, &sgHost, sizeof(struct SG_MessageHost));
	}

	// and the room's vote: whether it votes, and the vote running if one is
	// (SG_NEWCLIENT clears that too)
	Vote_Send(ri, ri->peerInfos[id].peer);
}

// The smallest packet each client message can legitimately be. Every handler
// in ProcessReceiveEvent casts the payload to its struct and reads fields from
// it; a packet shorter than the struct would have those reads run off the end
// of ENet's buffer. Unknown types get SIZE_MAX, so they are dropped too.
static size_t MinPacketSize(int type)
{
	switch (type)
	{
		case CG_JOINROOM:  return sizeof(struct CG_MessageRoom);
		case CG_NAME:      return sizeof(struct CG_MessageName);
		case CG_TRACK:     return sizeof(struct CG_MessageTrack);
		case CG_CHARACTER: return sizeof(struct CG_MessageCharacter);
		case CG_STARTRACE: return sizeof(struct CG_Header);
		case CG_RACEDATA:  return sizeof(struct CG_EverythingKart);
		case CG_WEAPON:    return sizeof(struct CG_MessageWeapon);
		case CG_ENDRACE:   return sizeof(struct CG_MessageEndRace);
		case CG_TRACKCANCEL: return sizeof(struct CG_Header);
		case CG_WEAPONEVENT: return sizeof(struct CG_MessageWeaponEvent);
		case CG_HOSTACTION: return sizeof(struct CG_MessageHostAction);
		case CG_VOTE:       return sizeof(struct CG_MessageVote);
		default:           return (size_t)-1;
	}
}

// PeerInfo.name is NAME_LEN bytes with no terminator, so it must always be
// printed with a width ("%.*s", NAME_LEN, ...). The launcher lets letters,
// digits and the ROM font's pad-button glyphs through ('*' cross, '@'
// circle, '[' square, '^' triangle, its Glyphs buttons; '&' a space, '<' an
// arrow), but the server cannot rely on that: anything outside printable
// ASCII becomes '_', so a crafted name cannot push terminal control
// sequences into the log, nor the ROM's kana icons (0x80 and up) into a
// screen. The glyphs pass (1102, as they did in 1040: 1101 turned them into
// '_' too) - the ROM lays out a name with them by its measured width
// (NameWidth: the race's rank list, the lobby's grid).
static void CopyName(char* dst, const char* src)
{
	int i;
	for (i = 0; i < NAME_LEN; i++)
	{
		char c = src[i];
		if (c == 0)
			break;
		dst[i] = (c < 0x20 || c > 0x7e) ? '_' : c;
	}
	for (; i < NAME_LEN; i++)
		dst[i] = 0;
}

// ---------------------------------------------- contested touches (1102)
//
// Two karts side by side reach the same placed weapon (a beaker, a Nitro):
// each machine sees only its own kart touch it, the other's copy trailing
// by the latency, so both claim it. A kart's machine that sees another kart
// close enough to have it too waits on the server's word before hurting its
// kart (Net_Pend, netWeapons.c) and marks its claim tentative. That one is
// held here: the touch's time taken as its arrival less half the
// claimant's round trip, kept until a touch by another player as early
// could have come in - the slowest other player's one way, a client
// step and a margin - and the earliest touch wins. A claim not marked is
// an end its machine has already shown: a kart it hurt at once (a beaker,
// a blast) cannot be un-hurt, so it stands and the waiting claim is
// dropped - two karts hurt by one weapon is what this is for; one that
// hurt nobody (a weapon blown up, a shield's pop) or put a TNT on a head
// (its machine gives it up: Net_HeadYield) loses to an earlier waiting
// touch. The winner is relayed as any first claim, the claimant included
// (its echo is its kart's go); the rest are dropped, and their machines
// learn of the winner's relay.

// (HOLD_STEP_MS, HOLD_MARGIN_MS, HOLD_MAX_MS: SV_config.h)

static struct WeaponHold* WeaponHold_Find(RoomInfo* ri, int id)
{
	for (int i = 0; i < 8; i++)
		if (ri->weaponHolds[i].used && ri->weaponHolds[i].id == id)
			return &ri->weaponHolds[i];
	return 0;
}

// The verdict: the held claim relayed, the weapon ended - while it is
// still live (nothing else has decided it meanwhile).
static void WeaponHold_Release(RoomInfo* ri, struct WeaponHold* h)
{
	struct SG_MessageWeaponEvent s;
	h->used = 0;
	if (ri->weaponFate[h->id] != WEAPON_LIVE) return;
	memset(&s, 0, sizeof(s));
	s.type = SG_WEAPONEVENT;
	s.clientID = h->peer;
	s.owner = h->id >> 8;
	s.serial = h->id & 0xff;
	s.kind = h->kind;
	s.arg = h->arg;
	ri->weaponFate[h->id] = WEAPON_ENDED;
	broadcastToPeersReliable(ri, &s, sizeof(s));
}

// Held claims whose time is up, each tick.
static void WeaponHold_Tick(RoomInfo* ri)
{
	enet_uint32 now = enet_time_get();
	for (int i = 0; i < 8; i++)
	{
		struct WeaponHold* h = &ri->weaponHolds[i];
		if (h->used && (int)(now - h->until) >= 0) WeaponHold_Release(ri, h);
	}
}

// How long after its touch a claim waits: the slowest other player's one
// way (half its round trip), a client step and a margin, at most HOLD_MAX_MS.
static enet_uint32 WeaponHold_Window(RoomInfo* ri, int peerID)
{
	enet_uint32 w = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		ENetPeer* p = ri->peerInfos[i].peer;
		if (i == peerID || p == 0 || p->state != ENET_PEER_STATE_CONNECTED) continue;
		if (ROUND_IS_OUT(ri->peerInfos[i].cause)) continue;   // a ghost out of the round: it touches nothing
		if (p->roundTripTime / 2 > w) w = p->roundTripTime / 2;
	}
	w += HOLD_STEP_MS + HOLD_MARGIN_MS;
	return (w > HOLD_MAX_MS) ? HOLD_MAX_MS : w;
}

// A weapon's end claimed (a NET_EV_FINAL_MASK kind, the weapon live):
// 1 when the claim goes on to be relayed now (the weapon ended), 0 when it
// is held or dropped.
static int WeaponHold_Claim(RoomInfo* ri, ENetPeer* peer, int peerID, struct CG_MessageWeaponEvent* r)
{
	int id = (r->owner << 8) | r->serial;
	enet_uint32 now = enet_time_get();
	enet_uint32 at = now - peer->roundTripTime / 2;
	struct WeaponHold* h = WeaponHold_Find(ri, id);

	if (r->tentative)
	{
		if (h != 0)
		{
			// two waiting: the earlier touch is kept, the later dropped
			if ((int)(at - h->at) < 0) { h->peer = peerID; h->kind = r->kind; h->arg = r->arg; h->at = at; }
			return 0;
		}
		for (int i = 0; i < 8; i++)
		{
			h = &ri->weaponHolds[i];
			if (h->used) continue;
			h->used = 1; h->peer = peerID; h->kind = r->kind; h->arg = r->arg; h->id = id;
			h->at = at;
			h->until = at + WeaponHold_Window(ri, peerID);
			if ((int)(now - h->until) >= 0) { h->used = 0; break; }   // nobody to wait for: at once
			return 0;
		}
		// no room to hold it: judged at once, as before
	}
	else if (h != 0)
	{
		// an end already shown on its machine, against a waiting claim: a
		// kart it hurt stands (it cannot be un-hurt: the waiting one is
		// spared); anything else, the earlier
		if (r->kind != NET_EV_POTION && r->kind != NET_EV_MINE_BLAST && (int)(h->at - at) <= 0)
		{
			WeaponHold_Release(ri, h);
			return 0;
		}
		h->used = 0;
	}
	ri->weaponFate[id] = WEAPON_ENDED;
	return 1;
}

// A masked kart's bump as its own machine saw it (NET_EV_KART_CLAIM, 1107):
// to the bumped kart's machine alone, which judges it (netWeapons.c) and,
// when it takes, sends its kart's hurt as ever (NET_EV_KART_HURT, to the
// room and the referee). The two players' round trips go with it, which
// only the server knows: that machine rewinds its kart's path by them to
// where the claimant's screen had it. In item rooms, between two players
// still in a round, from a player who fired a mask lately (MASK_SHOT_MS:
// CG_WEAPON), at most once a KART_CLAIM_GAP_MS for each pair.
#define KART_CLAIM_GAP_MS 400     // the ROM's own gap is 500 ms (CLAIM_GAP_MS)
#define MASK_SHOT_MS      20000   // a mask's longest, 12 s of game time (0x2d00, 960 a second) and its fade, with room for a game running slow: only a gate against abuse, the bumped kart's machine checks the mask itself
static void KartClaim_Relay(RoomInfo* ri, ENetPeer* peer, int peerID, const struct CG_MessageWeaponEvent* r)
{
	struct SG_MessageWeaponEvent s;
	PeerInfo* pa = &ri->peerInfos[peerID];
	int victim = r->owner;
	enet_uint32 now = enet_time_get();
	int rn = 0;

	for (; rn < 16; rn++)
		if (roomInfos + rn == ri)
			break;
	if (!ROOM_IS_ITEMS(rn)) return;
	if (victim >= MAX_CLIENTS || victim == peerID) return;
	ENetPeer* vp = ri->peerInfos[victim].peer;
	if (vp == 0) return;
	if (ri->roundLive && (!Round_IsIn(ri, peerID) || !Round_IsIn(ri, victim))) return;
	if (pa->maskShotAt == 0 || (int)(now - pa->maskShotAt) > MASK_SHOT_MS) return;
	if (pa->claimAt[victim] != 0 && (int)(now - pa->claimAt[victim]) < KART_CLAIM_GAP_MS) return;
	pa->claimAt[victim] = now | 1;

	unsigned int rtt = (peer->roundTripTime + vp->roundTripTime) / 4;
	memset(&s, 0, sizeof(s));
	s.type = SG_WEAPONEVENT;
	s.clientID = peerID;
	s.owner = victim;
	s.kind = NET_EV_KART_CLAIM;
	s.serial = r->serial;
	s.arg = (rtt > 255) ? 255 : rtt;
	sendToPeerReliable(vp, &s, sizeof(s));
}

// ProcessReceiveEvent builds each relayed message in one stack buffer
// (sgBuffer) and casts it to the message: every one it builds there has to
// fit. 16 bytes until 1104, when the kart message grew to 18 (its lap word).
#define SG_BUFFER_BYTES 32
STATIC_ASSERT2(sizeof(struct SG_EverythingKart) <= SG_BUFFER_BYTES, "sgBuffer: SG_EverythingKart");
STATIC_ASSERT2(sizeof(struct SG_MessageWeapon) <= SG_BUFFER_BYTES, "sgBuffer: SG_MessageWeapon");
STATIC_ASSERT2(sizeof(struct SG_MessageWeaponEvent) <= SG_BUFFER_BYTES, "sgBuffer: SG_MessageWeaponEvent");
STATIC_ASSERT2(sizeof(struct SG_MessageEndRace) <= SG_BUFFER_BYTES, "sgBuffer: SG_MessageEndRace");
STATIC_ASSERT2(sizeof(struct SG_MessageName) <= SG_BUFFER_BYTES, "sgBuffer: SG_MessageName");
STATIC_ASSERT2(sizeof(struct SG_MessageCharacter) <= SG_BUFFER_BYTES, "sgBuffer: SG_MessageCharacter");

void ProcessReceiveEvent(ENetPeer* peer, ENetPacket* packet) {

	int peerID = -1;

	RoomInfo* ri = &roomInfos[0];

	// Length before anything else looks at the payload.
	if (packet->dataLength < sizeof(struct CG_Header))
		return;
	if (packet->dataLength < MinPacketSize(((struct CG_Header*)packet->data)->type))
		return;

	//identify which client ID this came from
	GetDriverFromRace(peer, &ri, &peerID);

	struct CG_Header* recvBuf = (struct CG_Header*)packet->data;
	char sgBuffer[SG_BUFFER_BYTES];
	memset(sgBuffer, 0, sizeof(sgBuffer));

	if (((struct CG_Header*)recvBuf)->type != CG_JOINROOM)
	{
		if (peerID == -1)
			return;
	}

	// switch will compile into a jmp table, no funcPtrs needed
	switch (((struct CG_Header*)recvBuf)->type)
	{
		case CG_JOINROOM:
		{
			struct CG_MessageRoom* r = (struct CG_MessageRoom*)recvBuf;

			// Already in a room. A second CG_JOINROOM would register the same
			// peer in two rooms, and only one of them is cleaned up on
			// disconnect - the other keeps a dangling peer pointer.
			if (peerID != -1)
				return;

			// Wrong server version (SERVER_VERSION, the wire number - not the
			// build, so a client from a later release that left the protocol
			// alone still gets in). The room list (which carries this
			// server's number) goes back so the client shows its update
			// screen, and the join is dropped. The client checks as well, but
			// a round trip late; this is the check that cannot be raced.
			if (r->version != SERVER_VERSION)
			{
				PrintPrefix(-1);
				printf("Join refused (client expects server %u and this is %u)\n", (unsigned)r->version, (unsigned)SERVER_VERSION);
				SendRoomData(peer);
				return;
			}

			// kick if room is invalid. roomInfos has 16 entries, 0-15. (This
			// was "> 16", which let room 16 through: one past the array, and
			// the join wrote peer, count and host into whatever follows it.)
			if (r->room >= 16)
			{
				// update data, reject player
				SendRoomData(peer);
				return;
			}

			ri = &roomInfos[r->room];

			// Closed for joining (RoomOpenForJoin): loading, racing, or
			// somebody's pick clock is nearly out. A chosen track does not
			// close it any more.
			if (!RoomOpenForJoin(ri))
			{
				// update data, reject player
				SendRoomData(peer);
				return;
			}

			// Kicked from this room by its host a short while ago
			// (KickBan_Add): it shows closed on their list (SendRoomData), but
			// this is the check that cannot be raced.
			if (KickBan_Mask(peer->address.host) & (1u << r->room))
			{
				PrintPrefix(r->room + 1);
				printf("Join refused (kicked from this room by its host for %ds)\n", kickBanSeconds);
				SendRoomData(peer);
				return;
			}

			// look for empty slot
			int id = -1;
			for (int i = 0; i < MAX_CLIENTS; i++)
				if (ri->peerInfos[i].peer == 0)
				{
					id = i;
					break;
				}

			// Full room
			if (id == -1)
			{
				// update data, reject player
				SendRoomData(peer);
				return;
			}

			// Avoid duplicate connection (from multiple CG_Room messages)
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				// Peer is valid
				if (ri->peerInfos[i].peer != 0)
				{
					// Peer matches IP of another in the room
					if (ri->peerInfos[i].peer->address.host == peer->address.host)
					{
						// === This is NOT 64001 port ===
						// This is a special port that determines which "local IP"
						// to connect to, after the message goes to the router IP,
						// even 2x clients + 2x duckstation will have different ports

						// Peer matches port of another in the room
						if (ri->peerInfos[i].peer->address.port == peer->address.port)
						{
							// cancel duplicate connection
							return;
						}
					}
				}
			}


			// === Connection Accepted ===

			// if added to end, then increase count
			if (id == ri->clientCount)
				ri->clientCount++;

			// If a client disconnects, bools are set to 1,
			// so now if you leave and rejoin, this resets to 0
			memset(&ri->peerInfos[id], 0, sizeof(PeerInfo));

			ri->peerInfos[id].peer = peer;

			ForeachPeerLPL(SendRoomData); //someone chose a room, notify everyone else.

			// 5 seconds
			enet_peer_timeout(peer, 1000000, 1000000, 5000);

			// Claim the host role if the room has none - first player into an
			// empty room, or one whose host left while it was empty. Runs
			// BEFORE WelcomeNewClient so that call already has a valid host to
			// announce; doing it afterwards would send SG_NEWCLIENT (which
			// clears the client's hostID) with nothing following it.
			EnsureHost(ri, id);

			// A room's first player opens its first round: the vote, unless the
			// room does not vote. A vote already running hears of the newcomer
			// (their slot's vote is nothing yet).
			if (!ri->votingOff && !ri->boolRoomLocked && ri->votePhase == VOTE_NONE)
				Vote_Begin(ri, VOTE_TRACK);
			else if (ri->votePhase != VOTE_NONE)
				Vote_Broadcast(ri);

			WelcomeNewClient(ri, id);

			// A full budget for the newcomer. Either they have just become
			// host (EnsureHost) and their clock starts, or a host is already
			// on the clock and the joiner is told where it stands - or the
			// track is already in, and the joiner is sent it (their client
			// puts them on the character pick) and their own clock starts.
			// Everybody else's character state follows with the names
			// (CG_NAME).
			IdleReset(&ri->peerInfos[id]);
			if (ri->hostID == id)
				ArmHostIdle(ri);
			else if (ri->boolRoomLocked)
			{
				struct SG_MessageTrack st;
				st.type = SG_TRACK;
				st.voted = ri->trackVoted;
				st.padding = 0;
				st.trackID = ri->levelPlayed;
				st.lapID = ri->lapPlayed;
				sendToPeerReliable(peer, &st, sizeof(struct SG_MessageTrack));

				ArmIdle(ri, id);
			}
			else if (HasValidHost(ri) && ri->peerInfos[ri->hostID].idleStart != 0)
				SendIdleCountdown(peer, IdleLeftSeconds(&ri->peerInfos[ri->hostID]));

			break;
		}

		case CG_NAME:
		{
			struct SG_MessageName* s = (struct SG_MessageName*)&sgBuffer[0];
			struct CG_MessageName* r = (struct CG_MessageName*)recvBuf;

			s->numClientsTotal = ri->clientCount;

			// save new name (sanitised, see CopyName)
			CopyName(&ri->peerInfos[peerID].name[0], &r->name[0]);

			// "seen in the past 24h": now address + name, so two players
			// behind one address are two (SV_heartbeat.h)
			{
				char nameZ[NAME_LEN + 1];
				memcpy(nameZ, &ri->peerInfos[peerID].name[0], NAME_LEN);
				nameZ[NAME_LEN] = '\0';
				Heartbeat_SeenName(peer->data, peer->address.host, nameZ);
			}

			int roomId = (int)(ri - roomInfos) + 1;

			PrintPrefix(roomId);
			printf("Player %d joined room %d and is now identified as %.*s [%08x]\n",
				peerID,
				roomId,
				NAME_LEN, &ri->peerInfos[peerID].name[0],
				peer->address.host);

			s->type = SG_NAME;

			// send all OTHER (8) names to THIS (1) client
			for (int j = 0; j < MAX_CLIENTS; j++)
			{
				if (
						// skip empty sockets, skip self
						(ri->peerInfos[j].peer != 0) &&
						(peerID != j)
					)
				{
					// send all OTHER (8) names to THIS (1) client
					s->clientID = j;
					memcpy(&s->name[0], &ri->peerInfos[j].name[0], NAME_LEN);
					sendToPeerReliable(ri->peerInfos[peerID].peer, s, sizeof(struct SG_MessageName));
				}
			}

			// send THIS (1) name to all OTHER (8) clients
			s->clientID = peerID;
			memcpy(&s->name[0], &ri->peerInfos[peerID].name[0], NAME_LEN);
			broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageName));

			// A late joiner (the track is in, the room is on the character
			// pick) is also told everybody's character and lock, so the
			// player list is right from the first frame.
			if (ri->boolRoomLocked)
			{
				for (int j = 0; j < MAX_CLIENTS; j++)
				{
					if (ri->peerInfos[j].peer == 0 || j == peerID)
						continue;

					struct SG_MessageCharacter sc;
					sc.type = SG_CHARACTER;
					sc.clientID = j;
					sc.characterID = ri->peerInfos[j].characterID;
					sc.boolLockedIn = ri->peerInfos[j].boolLoadSelf;
					sc.padding = 0;
					sendToPeerReliable(ri->peerInfos[peerID].peer, &sc, sizeof(struct SG_MessageCharacter));
				}
			}
			break;
		}

		case CG_TRACK:
		{
			struct CG_MessageTrack* r = (struct CG_MessageTrack*)recvBuf;

			// Only the host picks the track. The ROM shows the picker to the
			// host alone, but a modified client could send this from any seat
			// and start a race under everybody else.
			if (peerID != ri->hostID || ri->boolRoomLocked)
				break;

			// A pick made as the room began to vote (PLAYER VOTING turned on
			// within a round trip of it): refused, and the host's client told
			// the vote, which takes them off the character pick their client
			// went on to with the pick (the SG_VOTE handler, CL_main.cpp).
			if (ri->votePhase != VOTE_NONE)
			{
				Vote_Send(ri, peer);
				break;
			}

			// Every lap row is a pick now: the ROM's lap menu offers all eight
			// (LAP_COUNT_BY_ID, 1 to 20 laps). This used to refuse rows 4-7 from
			// when they were greyed placeholders, and silently: the host's client
			// had already moved on to the character pick, so the host went on
			// alone while the guests waited on a track that never came, and the
			// host's lock-in then paused the only clock that could have ended it.
			// lapID is three bits, so there is nothing out of range to refuse.
			{
				int trackID = r->trackID;
				int lapID = r->lapID;

				// The game modes' rooms race the race tracks only - the ROM's
				// list and the vote offer nothing else (NUM_TRACKS, menu.c;
				// VOTE_TRACKS) - but trackID is five bits, so a modified
				// client could ask for an arena, where a round would meet
				// battle rules (the invincibility after a blast), no laps, and
				// ProcessDisconnectEvent's arena kill. Clamped rather than
				// refused, for the reason above: a refusal strands the host.
				if (ROOM_IS_MODE((int)(ri - roomInfos)) && trackID >= VOTE_TRACKS)
					trackID = VOTE_TRACKS - 1;

				// ELIMINATION and TAG have no lap pick (Room_NoLapPick): row 0,
				// whatever the host's lap menu sent.
				if (Room_NoLapPick(ri))
					lapID = 0;

				Room_TrackIn(ri, trackID, lapID, 0);
			}
			break;
		}

		case CG_CHARACTER:
		{
			struct SG_MessageCharacter* s = (struct SG_MessageCharacter*)&sgBuffer[0];
			struct CG_MessageCharacter* r = (struct CG_MessageCharacter*)recvBuf;

			s->type = SG_CHARACTER;
			s->clientID = peerID;
			s->characterID = r->characterID;
			// (no lock-in before the track is in: one crossing a vote's start
			// must not start the race on its own)
			s->boolLockedIn = r->boolLockedIn && ri->boolRoomLocked;

			ri->peerInfos[peerID].characterID = s->characterID;
			ri->peerInfos[peerID].boolLoadSelf = s->boolLockedIn;

			// Locked in: their own clock pauses, and in its place they are
			// shown the longest clock still running in the room - how long
			// the wait for the others can last (RoomWatchUpdate; the other
			// locked-in players are told too if this pause moved it).
			// Unlocked again (TRIANGLE): their clock resumes with what was
			// left, and the tick tells the watchers if that moved it. (Every
			// cursor move arrives here too, with boolLockedIn 0; that
			// resumes nothing already running.)
			if (s->boolLockedIn)
			{
				IdlePause(&ri->peerInfos[peerID]);
				RoomWatchUpdate(ri, peerID);
			}
			else if (ri->boolRoomLocked && ri->peerInfos[peerID].idleStart == 0)
				ArmIdle(ri, peerID);

			broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageCharacter));
			break;
		}

		case CG_TRACKCANCEL:
		{
			// The host stepping back from the character pick to the lap pick
			// (TRIANGLE in the ROM). Host only, and only while the room is on
			// the character pick: once everybody has locked in and
			// SG_STARTLOADING is out, it is too late.
			// (A track the room voted in: everybody's, not the host's to take
			// back, whatever PLAYER VOTING is now - turned the other way, it
			// is the next round's; the ROM's TRIANGLE leaves the room instead.)
			if (peerID != ri->hostID || !ri->boolRoomLocked || ri->boolLoadAll || ri->trackVoted)
				break;

			ri->boolRoomLocked = 0;
			ri->trackVoted = 0;
			ri->levelPlayed = 0;

			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				ri->peerInfos[i].boolLoadSelf = 0;

				// The guests have nothing to pick until the track is back:
				// their clocks pause, keeping what is left. The host's runs on.
				if (i != ri->hostID)
					IdlePause(&ri->peerInfos[i]);
			}

			PrintPrefix((int)(ri - roomInfos) + 1);
			printf("Host cancelled the track and the room is back to the lap pick\n");

			struct SG_Header sg;
			sg.type = SG_TRACKCANCEL;
			sg.padding = 0;
			broadcastToPeersReliable(ri, &sg, sizeof(struct SG_Header));

			// The host's clock never stopped; the room is told where it stands.
			// Or, PLAYER VOTING turned on since the host picked it (for the
			// next round, the track being in): no track in now, so the room
			// votes - as it always does with voting on and no track in, which
			// the join path (Vote_Begin for a new room) goes by.
			if (!ri->votingOff)
				Vote_Start(ri, VOTE_TRACK);
			else
				ArmHostIdle(ri);

			// Open again, so the room list shows it that way.
			ForeachPeerLPL(SendRoomData);
			break;
		}

		case CG_HOSTACTION:
		{
			// The host's OPTIONS in the lobby (hostOptions.c): CHANGE HOST or
			// KICK PLAYER at another player still in the room. Host only, and
			// not once the race is loading (SG_STARTLOADING closed the ROM's
			// OPTIONS too).
			struct CG_MessageHostAction* r = (struct CG_MessageHostAction*)recvBuf;
			int target = r->clientID;
			ENetPeer* tp = (target < MAX_CLIENTS) ? ri->peerInfos[target].peer : 0;

			// PLAYER VOTING on (clientID 1) or off (0) for the room. Before the
			// track is in it takes effect at once: off, a vote running stops
			// and the host's pick is due (their clock runs); on, the vote
			// opens in place of the host's pick. After, it is the next
			// round's.
			if (r->action == HOST_ACTION_VOTING)
			{
				if (peerID != ri->hostID || ri->boolLoadAll)
					break;
				ri->votingOff = !r->clientID;

				PrintPrefix((int)(ri - roomInfos) + 1);
				printf("Host %.*s (%d) turned player voting %s\n",
					NAME_LEN, &ri->peerInfos[peerID].name[0], peerID, ri->votingOff ? "off" : "on");

				if (ri->votingOff && ri->votePhase != VOTE_NONE)
				{
					ri->votePhase = VOTE_NONE;
					Vote_Broadcast(ri);
					ArmHostIdle(ri);
				}
				else if (!ri->votingOff && !ri->boolRoomLocked && ri->votePhase == VOTE_NONE)
					Vote_Start(ri, VOTE_TRACK);
				else
					Vote_Broadcast(ri);   // the setting, for the next round
				break;
			}

			// The seat must still hold the player the host picked, by name (the
			// one the server keeps and sends out, CopyName and SG_NAME, so the
			// two are byte for byte the same): a seat let go and taken again
			// before this arrived holds somebody else, and a newcomer whose
			// name is not in yet holds zeros.
			if (peerID != ri->hostID || ri->boolLoadAll ||
				target == peerID || tp == 0 || tp->state != ENET_PEER_STATE_CONNECTED ||
				(r->action != HOST_ACTION_GIVE && r->action != HOST_ACTION_KICK) ||
				r->name[0] == 0 || memcmp(r->name, &ri->peerInfos[target].name[0], NAME_LEN) != 0)
			{
				// Refused - the player has just gone, say. The sender's client
				// may already have moved the host on in its own lobby for a
				// CHANGE HOST (HostAction_Service): it is told who it is.
				if (HasValidHost(ri))
				{
					struct SG_MessageHost sgHost;
					sgHost.type = SG_HOST;
					sgHost.hostID = ri->hostID & 0xF;
					sendToPeerReliable(peer, &sgHost, sizeof(struct SG_MessageHost));
				}
				break;
			}

			if (r->action == HOST_ACTION_GIVE)
			{
				// The room is the player's: everybody is told (SG_HOST), and
				// the old host's ROM goes back to the wait for the track
				// (hostOptions.c), the new one's on to the track pick
				// (StatePS1_Lobby_GuestTrackWait). Before the track is in,
				// the clock that runs is the host's: the old one's stops,
				// keeping what is left, and the new one's starts.
				if (!ri->boolRoomLocked)
					IdlePause(&ri->peerInfos[peerID]);
				ri->hostID = (unsigned char)target;
				SendHost(ri);

				PrintPrefix((int)(ri - roomInfos) + 1);
				printf("Host %.*s (%d) handed the room to %.*s (%d)\n",
					NAME_LEN, &ri->peerInfos[peerID].name[0], peerID,
					NAME_LEN, &ri->peerInfos[target].name[0], target);

				ArmHostIdle(ri);
			}
			else
			{
				// Out, as the idle clock removes a player (ServerState_Tick):
				// the disconnect says why (the client puts its player back on
				// this server's room list) and ProcessDisconnectEvent tells the
				// room once it lands. Until then no clock of theirs runs out
				// and no lock-in of theirs starts the race.
				PeerInfo* pi = &ri->peerInfos[target];
				pi->idleStart = 0;
				pi->boolLoadSelf = 0;
				pi->vote = 0;   // and their vote out of the room's, at once
				KickBan_Add(tp->address.host, (int)(ri - roomInfos));

				PrintPrefix((int)(ri - roomInfos) + 1);
				printf("Host %.*s (%d) kicked %.*s (%d), kept out of the room for %ds\n",
					NAME_LEN, &ri->peerInfos[peerID].name[0], peerID,
					NAME_LEN, &pi->name[0], target, kickBanSeconds);

				enet_peer_disconnect(tp, DISCONNECT_REASON_KICKED);
				if (ri->votePhase != VOTE_NONE)
					Vote_Broadcast(ri);   // (the kicked one, on the way out, skipped)
			}
			break;
		}

		case CG_VOTE:
		{
			// A player's vote, or a change of it, in the vote running
			// (Vote_*); the room shown it at once. The close is the tick's.
			struct CG_MessageVote* r = (struct CG_MessageVote*)recvBuf;
			int options = ri->votePhase == VOTE_TRACK ? VOTE_TRACKS : VOTE_LAP_ROWS;

			if (ri->votePhase == VOTE_NONE || r->phase != ri->votePhase || r->option >= options)
				break;
			ri->peerInfos[peerID].vote = (unsigned char)(r->option + 1);
			Vote_Broadcast(ri);
			break;
		}

		case CG_STARTRACE:
		{
			#if 0
			printf("Ready to Race: %d\n", peerID);
			#endif

			ri->peerInfos[peerID].boolRaceSelf = 1;
			break;
		}

		case CG_RACEDATA:
		{
			struct SG_EverythingKart* s = (struct SG_EverythingKart*)&sgBuffer[0];
			struct CG_EverythingKart* r = (struct CG_EverythingKart*)recvBuf;

			s->type = SG_RACEDATA;
			s->clientID = peerID;

			s->kartRot1 = r->kartRot1 & 0xf;
			s->kartRot2 = r->kartRot2;

			s->buttonHold = r->buttonHold;

			s->boolReserves = r->boolReserves;
			s->wumpa = r->wumpa;

			memcpy(&s->posX, &r->posX, 6);

			// the flame's colour as the sender's screen shows it (FLAME_*, 1102)
			s->flame = r->flame;

			// its last hurt (NET_HURT_*, 1103): the others hurt their copy of
			// the kart with this message's position
			s->hurt = r->hurt;

			// its lap, behind-the-line flag and progress as its own screen has
			// them (NET_LAP_*, 1104), and the message's number: the others rank
			// their copy of the kart by the newest (netLap.c; the client's
			// KartSeq_Newer). As they came: the sender's word, as its position is.
			memcpy(s->lap, r->lap, sizeof(s->lap));
			s->seq = r->seq;

			broadcastToPeersUnreliable(ri, s, sizeof(struct SG_EverythingKart));
			break;
		}

		case CG_WEAPON:
		{
			struct SG_MessageWeapon* s = (struct SG_MessageWeapon*)&sgBuffer[0];
			struct CG_MessageWeapon* r = (struct CG_MessageWeapon*)recvBuf;

			// the last race's, late (after a forced return to the lobby):
			// nobody can fire before SG_STARTRACE
			if (!ri->boolRaceAll) break;

			// a new shot, or a held shield let go (the same count again):
			// what it places is live
			ri->weaponFate[(peerID << 8) | r->serial] = WEAPON_LIVE;

			// a mask (item 7): its bumps may be claimed (KartClaim_Relay)
			if (r->weapon == 7) ri->peerInfos[peerID].maskShotAt = enet_time_get() | 1;

			s->type = SG_WEAPON;
			s->clientID = peerID;

			s->weapon = r->weapon;
			s->juiced = r->juiced;
			s->flags = r->flags;

			// the shooter's pose and shot count, through as they came: every
			// machine spawns the weapon there and knows it by them
			s->held = r->held;
			s->serial = r->serial;
			s->target = (r->target <= MAX_CLIENTS) ? r->target : 0;
			memcpy(&s->pos[0], &r->pos[0], sizeof(s->pos));
			s->angle = r->angle;
			memcpy(&s->fwd[0], &r->fwd[0], sizeof(s->fwd));

			//this is a potential cheat mitigation:
			//I have it commented out for now BC if we ever change the ROOM_... allocations, that also
			//requires a server update (lame). Enable this if cheaters using items in non-item rooms becomes
			//a problem.
			
			// item rooms, and a room without items' bombs (on Tiger Temple and
			// Tiny Arena every player holds one: the other machines show it as
			// a ghost, netWeapons.c)
			int rn = 0;
			for (; rn < 16; rn++)
				if (roomInfos + rn == ri)
					break;
			if (ROOM_IS_ITEMS(rn) || (r->weapon == 1))
				broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageWeapon));

			//broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageWeapon)); //comment this if using the above cheat mitigation

			break;
		}

		// What happened to a weapon or a kart on the sender's machine
		// (NET_EV_*, global.h): to everyone in the room, with the sender's ID
		// - for a kart, the kart it happened to - when it is the first word
		// on that weapon (weaponFate). A kart's hurt and Tiger Temple's door
		// (NET_EV_NOID_MASK) have no weapon and go in every room; a
		// projectile's end goes in every room too (a room without items
		// shares its bombs); the rest in item rooms only, as the shots. The
		// sender gets its own back, which its client drops. The game modes'
		// referee (Round_*) hears two kinds: ELIMINATION's lap reports, which
		// stop here, and every kart's hurt, judged once it has gone out.
		case CG_WEAPONEVENT:
		{
			struct SG_MessageWeaponEvent* s = (struct SG_MessageWeaponEvent*)&sgBuffer[0];
			struct CG_MessageWeaponEvent* r = (struct CG_MessageWeaponEvent*)recvBuf;
			int kind = r->kind;

			if (!ri->boolRaceAll) break;   // as CG_WEAPON
			if (kind >= NET_EV_COUNT) break;

			// the referee's verdicts (NET_EV_OUT, NET_EV_IT) are the server's
			// to give: one from a client would put a player out, or make one
			// "it", on every screen
			if ((NET_EV_SERVER_MASK >> kind) & 1) break;

			// ELIMINATION's lap reports: for the referee alone (Elim_OnLap,
			// which ignores them in any other room), relayed to nobody
			if (kind == NET_EV_LAP)
			{
				Elim_OnLap(ri, peerID, r->arg);
				break;
			}

			// a masked kart's bump: to the bumped kart's machine alone
			if (kind == NET_EV_KART_CLAIM)
			{
				KartClaim_Relay(ri, peer, peerID, r);
				break;
			}

			if (((NET_EV_NOID_MASK >> kind) & 1) == 0)
			{
				// 4 bits on the wire, fewer seats: past them would index weaponFate
				// into the next room
				if (r->owner >= MAX_CLIENTS) break;
				unsigned char* fate = &ri->weaponFate[(r->owner << 8) | r->serial];

				int rn = 0;
				for (; rn < 16; rn++)
					if (roomInfos + rn == ri)
						break;
				if (!ROOM_IS_ITEMS(rn) && (((NET_EV_ANYROOM_MASK >> kind) & 1) == 0)) break;

				if ((NET_EV_FINAL_MASK >> kind) & 1)
				{
					// its end: the earliest touch says how (WeaponHold_Claim:
					// a contested one held for the others' claims)
					if (*fate != WEAPON_LIVE) break;
					if (!WeaponHold_Claim(ri, peer, peerID, r)) break;
				}
				else if (kind == NET_EV_TNT_ONHEAD)
				{
					// onto a head: the first kart to claim it has it - against
					// a waiting touch held on it (WeaponHold_*), the earlier
					// (the head's machine gives it up if not: Net_HeadYield)
					if (*fate != WEAPON_LIVE) break;
					struct WeaponHold* h = WeaponHold_Find(ri, (r->owner << 8) | r->serial);
					if (h != 0)
					{
						if ((int)(h->at - (enet_time_get() - peer->roundTripTime / 2)) <= 0)
						{
							WeaponHold_Release(ri, h);
							break;
						}
						h->used = 0;
					}
					*fate = (unsigned char)(WEAPON_HELD + peerID);
				}
				else if ((NET_EV_CARRIER_MASK >> kind) & 1)
				{
					// off a head: only the kart carrying it says
					if (*fate != WEAPON_HELD + peerID) break;
					*fate = WEAPON_ENDED;
				}
				else break;
			}

			s->type = SG_WEAPONEVENT;
			s->clientID = peerID;
			s->owner = r->owner;
			s->kind = r->kind;
			s->arg = r->arg;
			s->serial = r->serial;

			broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageWeaponEvent));

			// a game mode's rule on the hurt (a knock-out, a tag), after it:
			// the room replays the hurt, then hears the verdict
			if (kind == NET_EV_KART_HURT)
				Round_OnHurt(ri, peerID, r);
			break;
		}

		case CG_ENDRACE:
		{
			struct SG_MessageEndRace* s = (struct SG_MessageEndRace*)&sgBuffer[0];
			struct CG_MessageEndRace* r = (struct CG_MessageEndRace*)recvBuf;

			// Not before the race has started: an early CG_ENDRACE would put a
			// bogus result on everybody's screen and start the lobby-return
			// clock on them.
			if (!ri->boolRaceAll)
				break;

			s->type = SG_ENDRACE;
			s->clientID = peerID;

			memcpy(&s->courseTime, &r->courseTime, sizeof(r->courseTime));
			memcpy(&s->lapTime, &r->lapTime, sizeof(r->lapTime));

			int localTime = 0;
			memcpy(&localTime, &r->courseTime, sizeof(r->courseTime));

			char timeStr[32];
			snprintf(
				&timeStr[0], 32,
				"%d:%d%d:%d%d",
				localTime / 0xe100,
				(localTime / 0x2580) % 6,
				(localTime / 0x3c0) % 10,
				((localTime * 10) / 0x3c0) % 10,
				((localTime * 100) / 0x3c0) % 10
			);

			//printf("End Race: %d %s\n", peerID, timeStr);

			// In a game mode's live round the server's record says what this
			// is. From a player still in, a finish: placed from the top (in
			// ELIMINATION it can beat their last NET_EV_LAP here - whichever
			// comes first places them). From a player already placed, only
			// their arrival at the results their verdict sent them to - out,
			// or still in when the round ended - which is no finish. Only a
			// real finish with the round still going starts the lobby-return
			// clock below: a round that has just ended goes back through the
			// everybody-ended path (Round_Check), and an out player reaching
			// the results must not cut the others' race to 60 s.
			// The finish is the record's, not this message's: ELIMINATION's last
			// NET_EV_LAP may have placed them FINISH before this arrived (either
			// order happens), and that finish starts the clock all the same - or
			// two players stalled on an early lap would hold the room in the race
			// for good. Round_Finish does nothing for a player already placed.
			int realFinish = 1;
			if (ri->roundLive)
			{
				Round_Finish(ri, peerID);
				realFinish = !ri->roundOver && ri->peerInfos[peerID].cause == ROUND_CAUSE_FINISH;
			}

			ri->peerInfos[peerID].boolEndSelf = 1;

			// First one home starts the lobby-return timer for everybody else.
			if (realFinish && ri->firstEndTime == 0)
			{
				ri->firstEndTime = enet_time_get();

				PrintPrefix((int)(ri - &roomInfos[0]) + 1);
				printf("Player %.*s (%d) finished first but everyone else has %ds\n",
					NAME_LEN, &ri->peerInfos[peerID].name[0], peerID, LOBBY_RETURN_SECONDS);

				// Everybody sees the clock, not just the server log.
				struct SG_MessageLobbyCountdown sg;
				sg.type    = SG_LOBBYCOUNTDOWN;
				sg.padding = 0;
				sg.seconds = LOBBY_RETURN_SECONDS;
				broadcastToPeersReliable(ri, &sg, sizeof(struct SG_MessageLobbyCountdown));
			}

			broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageEndRace));
			break;
		}

		default:
		{
			break;
		}
	}
}

void ProcessNewMessages() {
	ENetEvent event;
	while (enet_host_service(server, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_RECEIVE:
				ProcessReceiveEvent(event.peer, event.packet);
				break;

			case ENET_EVENT_TYPE_CONNECT:
			{
				unsigned int ip = event.peer->address.host;
				union
				{
					unsigned int ip;
					unsigned char bytes[4];
				} ipBytes;
				ipBytes.ip = ip;

				if (AnyMatchIPBanL(ip)) //if this IP is banned
				{
					PrintPrefix(-1);
					printf("Banned player @ IP [%d.%d.%d.%d] attempted to connect, rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3]);
					enet_peer_disconnect_now(event.peer, 0);
				}
				// One address cannot hold the whole server: the host has 256
				// peer slots and a single client that opened connections and
				// kept them alive could sit on all of them, room or no room.
				// Sixteen leaves plenty for a household or a LAN behind one NAT
				// (two full rooms) while making that pointless.
				else if (CountPeersLPL_FromHost(ip) >= MAX_PEERS_PER_IP)
				{
					PrintPrefix(-1);
					printf("Too many connections from IP [%d.%d.%d.%d] (limit %d), rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3], MAX_PEERS_PER_IP);
					enet_peer_disconnect_now(event.peer, 0);
				}
				else
				{
					// for the site's "seen in the past 24h": a keyed hash of
					// the address, no address kept (SV_heartbeat.h)
					event.peer->data = Heartbeat_SeenConnect(ip);
					ProcessConnectEvent(event.peer);
				}
			}
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				ProcessDisconnectEvent(event.peer);
				break;
		}

		enet_packet_destroy(event.packet);
	}
}

// Same banner as the client: a screenshot of either window says which build
// it is at a glance. "Build 1036, Server 1035 | 08:24:01 26/SEP/2026" - the
// build (VERSION), the server version (SERVER_VERSION, what clients are
// checked against) and the compile time; __DATE__ is "Mmm dd yyyy" with the
// day space-padded, reordered to dd/MMM/yyyy. (Not "PrintBanner": global.h
// declares the client's PrintBanner(char) for both programs, and GCC rightly
// refuses a second one with a different signature.)
static void PrintServerBanner(void)
{
	printf("    ____        ___            ________________ \n");

	printf("   / __ \\____  / (_)___  ___  / ____/_  __/ __ \\\n");

	printf("  / / / / __ \\/ / / __ \\/ _ \\/ /     / / / /_/ /\n");

	printf(" / /_/ / / / / / / / / /  __/ /___  / / / _, _/ \n");

	printf(" \\____/_/ /_/_/_/_/ /_/\\___/\\____/ /_/ /_/ |_|  \n\n");


	const char* d = __DATE__;
	printf(" OnlineCTR Server (press CTRL + C to quit)\n Build %d, Server %d | %s %c%c/%c%c%c/%s\n\n",
		VERSION, SERVER_VERSION,
		__TIME__,
		d[4] == ' ' ? '0' : d[4], d[5],
		toupper((unsigned char)d[0]), toupper((unsigned char)d[1]), toupper((unsigned char)d[2]),
		d + 7);

	// The licence notice (AGPL-3.0's Appropriate Legal Notices: the
	// copyright, no warranty, the licence and where it is), with the
	// attribution ADDITIONAL-TERMS asks every copy to keep. The console is
	// sized for its longest line, the site's (ServerState_FirstBoot).
	printf(" Copyright (C) 2024-2026 OnlineCTR Team (http://online-ctr.com/)\n"
	       " Free software under the GNU AGPL-3.0, with the\n"
	       " additional terms in ADDITIONAL-TERMS; see LICENSE.\n"
	       " It comes with ABSOLUTELY NO WARRANTY.\n\n");
}

// DEBUG ONLY: rooms filled with bots (SV_backfill.h), in a build made with
// BACKFILL_PLAYERS set to their number; none in a release build.
#ifndef BACKFILL_PLAYERS
#define BACKFILL_PLAYERS 0
#endif
#if BACKFILL_PLAYERS > 0
#include "SV_backfill.h"
#endif

// The port typed at the prompt, asked for until it is one: a number from 1
// to 65535, or Enter alone for DEFAULT_PORT (SV_config.h). A whole line is
// read, and thrown away with whatever was wrong with it (scanf left a typing
// slip in the input, to fail on for ever). No input left - nothing piped
// in, no console - ends the server: no port is coming.
static int AskPort(void)
{
	char line[64];

	for (;;)
	{
		char* start;
		char* end;
		long port;

		printf("Enter Port (press Enter for %d): ", DEFAULT_PORT);
		fflush(stdout);

		if (!fgets(line, sizeof(line), stdin))
		{
			fprintf(stderr, "\nError: No port given (the input is closed); start the server with -p <port>\n");
			exit(EXIT_FAILURE);
		}

		// the rest of a line too long for the buffer goes with it
		if (!strchr(line, '\n'))
		{
			int c;
			while ((c = getchar()) != '\n' && c != EOF)
				;
		}

		// Enter alone (or only spaces): the default
		for (start = line; isspace((unsigned char)*start); start++)
			;
		if (*start == 0)
			return DEFAULT_PORT;

		port = strtol(start, &end, 10);
		while (isspace((unsigned char)*end))
			end++;

		if (end != start && *end == 0 && port >= 1 && port <= 65535)
			return (int)port;

		printf("Error: A port is a number from 1 to 65535\n");
	}
}

// Why the port could not be opened, after ENet's word that it could not:
// the bind is tried again on a socket of this function's own, and the
// system's reason read straight after it. Windows refuses a port it keeps
// for itself: Hyper-V, WSL and Docker reserve blocks of ports from 49152 up
// ("netsh interface ipv4 show excludedportrange protocol=udp"), different on
// every PC and after a restart. That is what closed a player's console on
// 64001 (16 Oct 2026). Linux and macOS keep the ports below 1024 for root.
// _WIN32, not __WINDOWS__: ENet picks Winsock by it, so a MinGW build (no
// __WINDOWS__) has its reason in WSAGetLastError too.
static void PrintPortTrouble(int port)
{
	ENetAddress address;
	ENetSocket probe;
	int bound = 0, error = 0;

	address.host = ENET_HOST_ANY;
	address.port = (enet_uint16)port;

	probe = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (probe == ENET_SOCKET_NULL)
	{
		fprintf(stderr, "No UDP socket could be made on this computer.\n");
		return;
	}

	if (enet_socket_bind(probe, &address) == 0)
		bound = 1;
	else
	{
#ifdef _WIN32
		error = WSAGetLastError();
#else
		error = errno;
#endif
	}
	enet_socket_destroy(probe);

	if (bound)
	{
		fprintf(stderr, "Port %d is free now. Try it again.\n", port);
		return;
	}

#ifdef _WIN32
	if (error == WSAEACCES)
	{
		fprintf(stderr, "Port %d is reserved: Windows keeps blocks of ports from\n"
		                "49152 up for Hyper-V, WSL and Docker, or another program has\n"
		                "it. Try one from 1024 to 49151.\n", port);
		return;
	}
	if (error == WSAEADDRINUSE)
	{
		fprintf(stderr, "Port %d is in use by another program (a second server?).\n"
		                "Try another one.\n", port);
		return;
	}
	fprintf(stderr, "Port %d could not be opened (Windows socket error %d).\n", port, error);
#else
	if (error == EACCES && port < 1024)
	{
		fprintf(stderr, "Port %d needs root, as every port below 1024 does.\n"
		                "Try one from 1024 up.\n", port);
		return;
	}
	if (error == EADDRINUSE)
	{
		fprintf(stderr, "Port %d is in use by another program (a second server?).\n"
		                "Try another one.\n", port);
		return;
	}
	fprintf(stderr, "Port %d could not be opened (%s).\n", port, strerror(error));
#endif
}

void ServerState_FirstBoot(int argc, char** argv)
{
	PrintServerBanner();

#ifdef __WINDOWS__
	HWND console = GetConsoleWindow();
	RECT r;
	GetWindowRect(console, &r); //stores the console's current dimensions
	// 580 wide: the licence notice's copyright line, 65 characters with the
	// site's address, on one line; 80 taller for its 5 lines
	MoveWindow(console, r.left, r.top, 580, 240 + 35 + 80, TRUE);
#endif

	int port = 0;   // from -p, or AskPort before it is used
	int boolIsPortArgument = 0;
	char* banlistPath = "";
	int boolIsBanlistArgument = 0;

	// port argument reading
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0)
		{
			boolIsPortArgument = 1;

			if (i + 1 < argc)
			{
				port = atoi(argv[i + 1]);
				i++; // next is the port number, so ignore it
			}
			else
			{
				fprintf(stderr, "Error: --port or -p requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
#if BACKFILL_PLAYERS > 0
		else if (strcmp(argv[i], "--backfill") == 0 && i + 1 < argc)
		{
			botCount = atoi(argv[i + 1]);   // the bots' number, this launch
			i++;
		}
#endif
		else if (strcmp(argv[i], "--votesecs") == 0)
		{
			// seconds a vote runs at most (VOTE_SECONDS)
			if (i + 1 < argc)
			{
				voteSeconds = atoi(argv[i + 1]);
				if (voteSeconds < 1) voteSeconds = 1;
				if (voteSeconds > 250) voteSeconds = 250;   // SG_VOTE's byte
				i++;
			}
			else
			{
				fprintf(stderr, "Error: --votesecs requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "--loadsecs") == 0)
		{
			// seconds a room waits on its players to load (LOAD_TIMEOUT_SECONDS)
			if (i + 1 < argc)
			{
				loadSeconds = atoi(argv[i + 1]);
				if (loadSeconds < 1) loadSeconds = 1;
				i++;
			}
			else
			{
				fprintf(stderr, "Error: --loadsecs requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "--kickban") == 0)
		{
			// seconds a kicked player is kept out of the room (KICK_BAN_SECONDS)
			if (i + 1 < argc)
			{
				kickBanSeconds = atoi(argv[i + 1]);
				if (kickBanSeconds < 0) kickBanSeconds = 0;
				i++;
			}
			else
			{
				fprintf(stderr, "Error: --kickban requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "--tagsecs") == 0)
		{
			// seconds TAG's "it" has before going out (TAG_SECONDS)
			if (i + 1 < argc)
			{
				tagSeconds = atoi(argv[i + 1]);
				if (tagSeconds < 1) tagSeconds = 1;
				if (tagSeconds > 250) tagSeconds = 250;   // NET_EV_IT's byte
				i++;
			}
			else
			{
				fprintf(stderr, "Error: --tagsecs requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "--tagfirst") == 0)
		{
			// TAG's first "it" in every round, by client ID, when that player
			// is in it (Tag_NewIt); otherwise drawn as usual
			if (i + 1 < argc)
			{
				tagFirst = atoi(argv[i + 1]);
				if (tagFirst < 0 || tagFirst >= MAX_CLIENTS) tagFirst = -1;
				i++;
			}
			else
			{
				fprintf(stderr, "Error: --tagfirst requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "--banlist") == 0 || strcmp(argv[i], "-b") == 0)
		{
			boolIsBanlistArgument = 1;
			if (i + 1 < argc)
			{
				banlistPath = argv[i + 1];
				i++; // next is banlist path, so ignore it
			}
			else
			{
				fprintf(stderr, "Error: --banlist or -b requires a value\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	//read banlist file
	if (boolIsBanlistArgument)
	{
		FILE* fBanlistPtr = fopen(banlistPath, "r");
		if (fBanlistPtr != NULL)
		{
			char lineBuf[100];
			unsigned int lineNum = 1 /*1 based*/, banEntries = 0;
			while (fgets(lineBuf, 100, fBanlistPtr))
			{
				union
				{
					unsigned int ip;
					unsigned char bytes[4];
				} ipBytes;
				int ip[4] = { 0 };
				if (lineBuf[0] == '\n' && lineBuf[1] == '\0') //empty line
					;
				else if (EOF == sscanf(lineBuf, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]))
					printf("Failiure to parse IPV4 address on line %d in 'banlist.txt'\n", lineNum);
				else
				{
					for (int i = 0; i < 4; i++)
						ipBytes.bytes[i] = (unsigned char)ip[i];
					AddIPBanL(ipBytes.ip);
					banEntries++;
				}
				lineNum++;
			}
			printf("Banlist initialized with %d entries\n", banEntries);
		}
		else
			printf("Unable to open 'banlist.txt'\n");
	}

	// the vote's draws (Vote_Finish), and TAG's "it" (Tag_NewIt)
	srand((unsigned)time(NULL));

	//initialize enet
	if (enet_initialize() != 0)
	{
		fprintf(stderr, "Failed to initialize ENet!\n");
		exit(EXIT_FAILURE);
	}
	atexit(enet_deinitialize);

	// The port, given with -p or asked for. One that cannot be opened is
	// asked for again, with the reason (PrintPortTrouble), rather than the
	// server ending - and with it the console a double-click opened, before
	// its last line could be read. Given with -p, it still ends the server,
	// for the script or container that started it to see.
	for (;;)
	{
		if (!boolIsPortArgument)
		{
			port = AskPort();
			printf("Okay, using port %d UDP\n\n", port);
		}

		ENetAddress address;
		address.host = ENET_HOST_ANY;
		address.port = port;
		server = enet_host_create(&address,
			16 * MAX_CLIENTS + 128	/* every seat in the 16 rooms, plus 128 on the room list */,
			2	/* allow up to 2 channels to be used, 0 and 1 */,
			0	/* assume any amount of incoming bandwidth */,
			0	/* assume any amount of outgoing bandwidth */);
		if (server)
			break;

		fprintf(stderr, "Error: Failed to create an ENet server host!\n");
		PrintPortTrouble(port);
		if (boolIsPortArgument)
			exit(EXIT_FAILURE);
		fprintf(stderr, "\n");
	}

	// Quick Play's status query, answered ahead of ENet
	server->intercept = OctrQuery_Intercept;

	PrintPrefix(-1);
	printf("Ready on port %d\n", port);

#if BACKFILL_PLAYERS > 0
	Backfill_Init(port);
#endif

	// Outbound status heartbeat. No-op unless OCTR_HEARTBEAT_URL and
	// OCTR_HEARTBEAT_SECRET are both set, and compiled out entirely when the
	// build has no libcurl.
	Heartbeat_Init();
}

// Snapshot the room occupancy for the heartbeat and hand it to the worker
// thread. Rate limited to roughly once a second: the caller runs in a
// usleep(1) loop, so publishing unconditionally would take the lock millions
// of times a second for data that is posted every 30.
static void Heartbeat_Sample(void)
{
	// Once a second, on ENet's millisecond clock: unsigned, so the
	// subtraction holds across its wrap (49 days). Not clock(): on Windows it
	// returns -1 for good after 2^31 ms (24.9 days) of running, which froze
	// the sample - and the site's count - until a restart; on Linux it is
	// CPU time, not wall time.
	static enet_uint32 lastSample = 0;
	enet_uint32 now = enet_time_get();
	OctrStatus st;
	int i, j;

	if (lastSample != 0 && (enet_uint32)(now - lastSample) < 1000)
		return;
	lastSample = now ? now : 1;

	memset(&st, 0, sizeof(st));
	st.version = SERVER_VERSION;
	st.build = VERSION;

	// Same hole-aware count SendRoomData() uses. Deliberately NOT
	// roomInfos[i].clientCount - that ignores empty holes and would disagree
	// with the SERVER TOTAL shown in game.
	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < MAX_CLIENTS; j++)
			if (roomInfos[i].peerInfos[j].peer != 0)
				st.rooms[i]++;

		st.locked[i] = !RoomOpenForJoin(&roomInfos[i]);
		st.total += st.rooms[i];
	}

	Heartbeat_Publish(&st);
}

// Put a room back into its lobby after a race: every client gets SG_NEWCLIENT
// (which drops them into LOBBY_ASSIGN_ROLE and, on the PS1, reloads the globe)
// and the per-race flags are cleared. Shared by the two ways a race can end -
// everybody finishing, or the lobby-return timer expiring - so the two paths
// cannot drift apart.
static void ResetRoomAfterRace(RoomInfo* ri)
{
	// Settle the host BEFORE the loop: WelcomeNewClient announces whoever
	// holds the role, so it has to be correct by then. The previous host may
	// have quit during the race, so re-pick rather than trusting the stored id.
	if (!HasValidHost(ri))
	{
		int newHost = PickNewHost(ri);
		if (newHost != -1)
			ri->hostID = (unsigned char)newHost;
	}

	// The next round's track by the players' vote, unless the room does not
	// vote: open before the welcome below, which carries it (and with it
	// the host's clock below stays stopped).
	ri->votePhase = VOTE_NONE;
	if (!ri->votingOff)
		Vote_Begin(ri, VOTE_TRACK);

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (ri->peerInfos[i].peer == 0)
			continue;

		ri->peerInfos[i].boolLoadSelf = 0;
		ri->peerInfos[i].boolRaceSelf = 0;
		ri->peerInfos[i].boolEndSelf = 0;
		IdleReset(&ri->peerInfos[i]);

		// tell all clients to reset
		// (also re-announces the host, which SG_NEWCLIENT clears)
		WelcomeNewClient(ri, i);
	}

	ri->levelPlayed = 0;
	ri->boolRoomLocked = 0;
	ri->trackVoted = 0;
	ri->boolLoadAll = 0;
	ri->boolRaceAll = 0;
	ri->boolEndAll = 0;
	memset(ri->weaponFate, 0, sizeof(ri->weaponFate));
	memset(ri->weaponHolds, 0, sizeof(ri->weaponHolds));
	ri->endTime = 0;
	ri->firstEndTime = 0;
	ri->watchDeadline = 0;

	// No game mode's round any more (Round_*): nothing judged, no clock, and
	// every seat's place gone (Round_Begin clears them again at the next
	// load; the leavers' seats too).
	ri->roundOn = 0;
	ri->roundLive = 0;
	ri->roundOver = 0;
	ri->roundGreenAt = 0;
	ri->roundDone = 0;
	ri->roundLaps = 0;
	ri->elimLap = 0;
	ri->itID = ROUND_NONE;
	ri->itGreenAt = 0;
	ri->itDeadline = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		ri->peerInfos[i].place = 0;
		ri->peerInfos[i].cause = 0;
		ri->peerInfos[i].lapsDone = 0;
	}

	// Back in the lobby with fresh budgets; the host's clock starts.
	ArmHostIdle(ri);

	ForeachPeerLPL(SendRoomData); //race over, re-notify all clients
}

void ServerState_Tick()
{
	ProcessNewMessages();

	Heartbeat_Sample();

	// a kick's time up: that room open to its player again on their list
	if (KickBan_Expire())
		ForeachPeerLPL(SendRoomData);

	for (int r = 0; r < 16; r++)
	{
		RoomInfo* ri = &roomInfos[r];

		if (ri->clientCount == 0)
			continue;

		// the room down to one player, or up to two: its clocks held or run
		// (Solo_Tick), ahead of the vote's close
		Solo_Tick(ri);

		// a vote closed: everybody has voted, or its time is up
		Vote_Tick(ri);

		// contested touches whose wait is up (WeaponHold_*)
		WeaponHold_Tick(ri);

		// a game mode's round decided at its green (Round_Tick), then TAG's
		// clock: started at the green, "it" out when it runs out (a round's
		// end here is picked up by the everybody-ended check below, this
		// same tick)
		Round_Tick(ri);
		Tag_Tick(ri);

		// The room list says whether this room can be joined, and that can
		// change on its own as a clock runs down (RoomOpenForJoin): push a
		// fresh list to everybody the moment it does.
		{
			char closed = !RoomOpenForJoin(ri);
			if (closed != ri->boolJoinClosed)
			{
				ri->boolJoinClosed = closed;
				ForeachPeerLPL(SendRoomData);
			}
		}

		// Lobby idle clocks (IdleRun): a running clock out of budget, out of
		// the room. The disconnect is the ordinary kind, so
		// ProcessDisconnectEvent does the rest once it lands - the empty-name
		// broadcast, and for a host the promotion of the next player, whose
		// own clock then starts.
		if (!ri->boolLoadAll)
		{
			for (int j = 0; j < MAX_CLIENTS; j++)
			{
				PeerInfo* pi = &ri->peerInfos[j];
				if (pi->peer == 0 || pi->idleStart == 0)
					continue;
				if (IdleLeftMs(pi) > 0)
					continue;

				pi->idleStart = 0;
				PrintPrefix(r + 1);
				printf("Player %.*s (%d) made no pick in %ds, removed for idling\n",
					NAME_LEN, &pi->name[0], j, LOBBY_IDLE_SECONDS);
				enet_peer_disconnect(pi->peer, DISCONNECT_REASON_IDLE);
			}
		}

		// The locked-in players' number (RoomWatchUpdate): told again the
		// moment the longest running clock changes - a removal just above,
		// an unlock, a late joiner. Only on the character pick; nothing to
		// watch before the track is in or once loading has started.
		if (ri->boolRoomLocked && !ri->boolLoadAll)
			RoomWatchUpdate(ri, -1);
		else
			ri->watchDeadline = 0;

		// (only with the track in: never on a vote's or a pick's way)
		if (!ri->boolLoadAll && ri->boolRoomLocked)
		{
			ri->boolLoadAll = 1;
			for (int j = 0; j < ri->clientCount; j++)
				if (ri->peerInfos[j].peer != 0)
					if (ri->peerInfos[j].boolLoadSelf == 0)
						ri->boolLoadAll = 0;

			if (ri->boolLoadAll)
			{
				// a new race: no weapon of the last one is known (the
				// clients clear theirs at this message too), nor any mask
				memset(ri->weaponFate, 0, sizeof(ri->weaponFate));
				memset(ri->weaponHolds, 0, sizeof(ri->weaponHolds));
				for (int k = 0; k < MAX_CLIENTS; k++)
				{
					ri->peerInfos[k].maskShotAt = 0;
					memset(ri->peerInfos[k].claimAt, 0, sizeof(ri->peerInfos[k].claimAt));
				}

				// send to all clients, the game modes' round seated now
				// (Round_Begin) and its code in the header's spare nibble -
				// set in every room, 0 for no round. It went out uninitialised
				// before the modes, which is why the client reads it in a
				// mode room only.
				struct SG_Header sg;
				sg.type = SG_STARTLOADING;
				sg.padding = Round_Begin(ri) & 0xF;
				broadcastToPeersReliable(ri, &sg, sizeof(struct SG_Header));

				// the load's clock (LOAD_TIMEOUT_SECONDS, below)
				ri->loadStartAt = IdleNow();
				int loading = 0;
				for (int j = 0; j < MAX_CLIENTS; j++)
					if (ri->peerInfos[j].peer != 0) loading++;
				PrintPrefix(r + 1);
				printf("Race loading: %d player%s\n", loading, loading == 1 ? "" : "s");
			}
		}

		// The load's time up (LOAD_TIMEOUT_SECONDS, SV_config.h): whoever has
		// not reached the start line is removed, as an idle player is, and the
		// race goes on for the rest - ProcessDisconnectEvent counts them in
		// (boolRaceSelf) once the disconnect lands. Once a race: a disconnect
		// lands by ENet's own timeout at the latest.
		if (ri->boolLoadAll && !ri->boolRaceAll && ri->loadStartAt != 0 &&
			(int)(enet_time_get() - ri->loadStartAt) >= loadSeconds * 1000)
		{
			ri->loadStartAt = 0;
			for (int j = 0; j < MAX_CLIENTS; j++)
			{
				PeerInfo* pi = &ri->peerInfos[j];
				if (pi->peer == 0 || pi->boolRaceSelf)
					continue;
				if (pi->peer->state != ENET_PEER_STATE_CONNECTED)
					continue;

				PrintPrefix(r + 1);
				printf("Player %.*s (%d) did not load in %ds, removed\n",
					NAME_LEN, &pi->name[0], j, loadSeconds);
				enet_peer_disconnect(pi->peer, DISCONNECT_REASON_IDLE);
			}
		}

		if (!ri->boolRaceAll)
		{
			ri->boolRaceAll = 1;
			for (int j = 0; j < ri->clientCount; j++)
				if (ri->peerInfos[j].peer != 0)
					if (ri->peerInfos[j].boolRaceSelf == 0)
						ri->boolRaceAll = 0;

			if (ri->boolRaceAll)
			{
				// send to all clients
				struct SG_Header sg;
				sg.type = SG_STARTRACE;
				sg.padding = 0;
				broadcastToPeersReliable(ri, &sg, sizeof(struct SG_Header));

				// the game modes' round refereed from here, if it still has
				// 2 players (TAG's first "it" told after this message)
				Round_Green(ri);
			}
		}

		if (!ri->boolEndAll)
		{
			ri->boolEndAll = 1;
			for (int j = 0; j < ri->clientCount; j++)
				if (ri->peerInfos[j].peer != 0)
					if (ri->peerInfos[j].boolEndSelf == 0)
						ri->boolEndAll = 0;

			if (ri->boolEndAll)
			{
				PrintPrefix(r+1);
				printf("Race has terminated, resetting room in %ds\n", RACE_OVER_COUNTDOWN_SECONDS);

				// Wall clock, not clock(): on Linux clock() is CPU time, and a
				// process that spends its life in usleep() accrues it at some
				// fraction of real time (the old "removed one zero" fudge in
				// CLOCKS_PER_SEC_FIX papered over exactly that). The clients
				// count this down in real seconds, so the reset has to fire
				// on real seconds too or the number on screen means nothing.
				ri->endTime = enet_time_get();

				// Replaces the LOBBY_RETURN clock on every client.
				struct SG_MessageLobbyCountdown sg;
				sg.type    = SG_LOBBYCOUNTDOWN;
				sg.padding = 0;
				sg.seconds = RACE_OVER_COUNTDOWN_SECONDS;
				broadcastToPeersReliable(ri, &sg, sizeof(struct SG_MessageLobbyCountdown));
			}

			// Lobby-return timer: somebody finished a while ago and somebody
			// else still has not. Checked only while the race is unfinished,
			// so once the last racer is in, the results countdown above runs
			// its full length even if this would have fired during it.
			else if (ri->firstEndTime != 0 &&
				(enet_time_get() - ri->firstEndTime) >= LOBBY_RETURN_SECONDS * 1000u)
			{
				PrintPrefix(r + 1);
				printf("Race still unfinished %ds after the first finish, returning everyone to the lobby\n",
					LOBBY_RETURN_SECONDS);

				ResetRoomAfterRace(ri);
			}
		}

		else
		{
			if ((enet_time_get() - ri->endTime) >= RACE_OVER_COUNTDOWN_SECONDS * 1000u)
			{
				PrintPrefix(r + 1);
				printf("Room has been reset\n");

				ResetRoomAfterRace(ri);
			}
		}
	}
}

// SIGTERM (docker stop, compose down, systemd) or SIGINT (Ctrl+C): the main
// loop ends and ServerState_Stop closes up. Without a handler the server
// never stopped on SIGTERM in a container - it is the container's first
// process, which ignores any signal it does not handle - so every docker
// stop, compose down and autodeploy waited out Docker's 10 s and then killed
// it, and the players found out only when their connection timed out.
// Windows: nothing sets it (closing the console ends the process, as before).
// _WIN32 as well as __WINDOWS__: only Server.vcxproj defines __WINDOWS__, and
// a MinGW build through CMakeLists.txt has no sigaction.
static volatile sig_atomic_t g_stopSignal = 0;

#if !defined(__WINDOWS__) && !defined(_WIN32)
static void OnStopSignal(int sig)
{
	g_stopSignal = sig;
}
#endif

// Every player told at once - the plain disconnect, as for a full server - so
// their game goes back to the room list now rather than waiting out its
// timeout. Then out with _exit, not exit or a return from main: the
// heartbeat's thread may be in the middle of a post, and exit's handlers
// (libcurl's TLS library cleans up at exit) would pull its state out from
// under it. stdout is the only thing left to flush.
static void ServerState_Stop(int sig)
{
	int told = 0;
	size_t i;

	for (i = 0; i < server->peerCount; i++)
	{
		ENetPeer* peer = &server->peers[i];
		if (peer->state == ENET_PEER_STATE_DISCONNECTED)
			continue;

		enet_peer_disconnect_now(peer, 0);
		told++;
	}
	enet_host_destroy(server);

	PrintPrefix(-1);
	printf("Stopping (%s): %d %s disconnected\n",
		(sig == SIGINT) ? "SIGINT" : "SIGTERM", told, (told == 1) ? "player" : "players");

	fflush(stdout);
	_exit(0);
}

int main(int argc, char** argv)
{
#ifndef __WINDOWS__
	// Line-buffer stdout even when it is a pipe. Under systemd, Docker or any
	// other log collector glibc would otherwise hold output in a 4 KB buffer
	// and release it in batches, so the startup lines (the banner, the
	// heartbeat's "enabled as ..." report) would not show up for a long time
	// and a crash would lose whatever was still buffered. On a terminal this
	// is already the default.
	//
	// Not on Windows: its C runtime has no line buffering (the request means
	// full buffering there, which would hide the console output instead of
	// helping), and it aborts the process on a size below 2 - which is how a
	// first version of this line, with size 0, crashed Server.exe on startup
	// before the banner was printed.
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
#endif

#ifdef __WINDOWS__
	// The loop below sleeps a microsecond between ticks (usleep, SV_helper.c),
	// which Windows rounds up to its timer's period. ENet asks for 1 ms
	// (enet_initialize, externals/enet/win32.c), so a tick is about 1 ms -
	// but Windows 11 ignores that for a process it thinks is in the
	// background (a minimised console, a service), back to 15.6 ms a tick,
	// unless the process opts out: done here. Looked up at run time, so the
	// exe still starts where the call does not exist (Windows 7).
	#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
	{
		typedef BOOL (WINAPI *SetProcInfo_t)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
		SetProcInfo_t setProcInfo = (SetProcInfo_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
		if (setProcInfo)
		{
			PROCESS_POWER_THROTTLING_STATE pt = { 0 };
			pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
			pt.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
			pt.StateMask = 0;
			setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));   // FALSE before Windows 10 1709: harmless
		}
	}
	#endif
#endif

	ServerState_FirstBoot(argc, argv);

#if !defined(__WINDOWS__) && !defined(_WIN32)
	// After the start-up rather than before it: a Ctrl+C at the port prompt
	// still ends the server at once, as it always did, instead of leaving
	// the prompt to fail over and over. SA_RESETHAND: a second Ctrl+C, should the
	// stop itself ever hang, kills it the old way.
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = OnStopSignal;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESETHAND;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
	}
#endif

	while (!g_stopSignal)
	{
		#ifdef __WINDOWS__
		void usleep(__int64 usec);
		#endif

		usleep(1);
		ServerState_Tick();
#if BACKFILL_PLAYERS > 0
		Backfill_Tick();
#endif
	}

	ServerState_Stop((int)g_stopSignal);
	return 0;
}
