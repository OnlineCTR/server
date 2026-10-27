// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

// Windows - Visual Studio and MinGW alike: _WIN32, which both define, not
// __WINDOWS__, which only Server.vcxproj does. Everything Windows-specific in
// the server keys on it, so a MinGW build gets the same Windows behaviour.
#ifdef _WIN32
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
#ifndef _WIN32
#include <unistd.h> // _exit
#endif
#include <enet/enet.h>
// (malloc and free come from <stdlib.h>, above: <malloc.h> is glibc's and
// MSVC's only - macOS has none, and a build there stopped at it)

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
#define SV_PROTOCOL 1117   // (1115 until 1117: Matt, the numbers match)
#include "global.h"

// The header has to be the one these sources were written against. A stale
// copy - an earlier release's global.h left next to the sources - would fail
// a page further down with undeclared message types and incomplete structs;
// this says what actually went wrong. NET_WEAPON_EVENTS is the newest
// thing this file needs from the header (10: a squish claimed too, and
// Saffi's own flame colour, FLAME_SAFFI, 1110; 9: a shield's bump claimed
// too, NET_CLAIM_HIT, 1109; 8: a masked kart's bump claimed,
// NET_EV_KART_CLAIM, 1107; 5: the game modes' events,
// NET_EV_LAP / NET_EV_OUT / NET_EV_IT, and SG_STARTLOADING's round code;
// 4: 4-bit IDs, 12-bit identity, the rooms' closed mask; 3: the weapons'
// arbitration, Tiger Temple's door, the item-less rooms' ghost bombs).
#if !defined(NET_WEAPON_EVENTS) || (NET_WEAPON_EVENTS < 10)
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
// The lobby's OPTIONS (CG_HOSTACTION, DISCONNECT_REASON_KICKED) and the
// players' vote (SG_VOTE, CG_VOTE): 1101; 2, the room's VOTE TO KICK in the
// host's KICK PLAYER's place (HOST_ACTION_KICKVOTE / KICKUNVOTE,
// KICK_VOTES_BY_PLAYERS, SG_VOTE's VOTE_KICK_REPLY): 1110.
#if !defined(NET_HOST_ACTIONS) || (NET_HOST_ACTIONS < 2) || !defined(NET_VOTING) || (NET_VOTING < 1)
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
// A race in progress watched (CG_MessageRoom's JOIN_SPECTATE,
// SG_MessageRooms.spectateMask, NEWCLIENT_SPECTATOR), the racers told how
// many watch (SG_MessageLobbyCountdown's kind, LOBBYCOUNT_WATCHERS), and a
// finished player's kart driven on by the bot (ENDRACE_BOT,
// SG_MessageEndRace.flags): 1110. An older header has none of these names,
// and its SG_ROOMS is two bytes short. 2, each racer told only of those
// watching them (CG_WATCH, CG_MessageWatch, WATCH_NOBODY) and the room
// list's racingMask (ROOM_RACING): 1111 - a 1110 header has neither, and
// its SG_ROOMS is 16 bytes, not 18.
#if !defined(NET_SPECTATE) || (NET_SPECTATE < 2)
#error "global.h is older than SV_main.c: replace it with the global.h from this release's zip (see README)"
#endif

// The server's own settings: connections, clocks, limits (public; nothing
// secret is compiled in - the heartbeat's address and key come from the
// environment at run time, SV_heartbeat.h).
#include "SV_config.h"

#include "SV_heartbeat.h"
#include "SV_auth.h"

#ifdef _MSC_VER
#pragma comment (lib, "Ws2_32.lib")
#endif

// A room's seats: MAX_NUM_PLAYERS (global.h), the ROM's and the client's.
#define MAX_CLIENTS MAX_NUM_PLAYERS

// Connections per address (MAX_PEERS_PER_IP), the results countdown
// (RACE_OVER_COUNTDOWN_SECONDS), the lobby-return timer
// (LOBBY_RETURN_SECONDS) and the lobby idle budget (LOBBY_IDLE_SECONDS):
// SV_config.h.


//SV_helper.c forward decls
void SleepMicro(long long usec);
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

	// Given once, by the seat's first CG_NAME with one in (1110: the vote
	// to kick goes by it); the join's memset empties it for the next player.
	char name[NAME_LEN];
	char characterID;
	char boolLoadSelf;
	char boolRaceSelf;
	char boolEndSelf;

	// Their vote in the room's vote running (Vote_*): the option + 1, 0
	// none - so the join's memset is no vote.
	unsigned char vote;

	// Their votes to kick (KickVote_*, 1110): bit t against the player in
	// seat t, one each at most - the room's masks together are who votes
	// against whom. Secret: only this player is ever answered with theirs
	// (SG_VOTE's VOTE_KICK_REPLY). Wiped at the countdown; a leaver's go
	// with them, and every vote against them (KickVote_Forget). The join's
	// memset clears it too. Server-only.
	unsigned short kickVotes;

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
	// (enet_time_get() | 1; 0 never). A shield's (1109): one claim for the
	// shield this player fired last (its count, shieldSerial: the same count
	// again is that shield let go - thrown, it bumps nobody), spent by the
	// claim - a shield's bump is its pop. A squish's (1110): when this
	// player's kart message last showed Saffi (FLAME_SAFFI, CG_RACEDATA),
	// and when this player last fired a TURBO (CG_WEAPON). Cleared as each
	// race loads (shieldSerial to SHIELD_SERIAL_NONE, which no count is); a
	// join's memset clears them too, and a race loads before a joiner fires.
	// Server-only.
	enet_uint32 maskShotAt;
	enet_uint32 claimAt[MAX_CLIENTS];
	unsigned char shieldClaim;
	unsigned short shieldSerial;
	enet_uint32 saffiSeenAt;
	enet_uint32 turboShotAt;

	// A race in progress watched (1110, Spectator_*). spectator: 0 a player;
	// 1 a spectator seated and held - on the room list, or loading the race
	// it joined - sent none of the room's broadcasts but its SG_NAMEs and
	// SG_HOST once its load is out (Broadcast_Held); 2 watching, caught up
	// at its CG_STARTRACE (Spectator_CatchUp) and sent everything from then
	// on. A spectator is in no race rule and no lobby count (Round_IsIn,
	// Vote_Voter, Room_Players, WeaponHold_Window, ...) and is never named to
	// the racers; the room's reset makes it a player (ResetRoomAfterRace).
	// welcomePending: the room reset while it was loading, its welcome due at
	// its CG_STARTRACE (Spectator_Welcome). loadSent: its SG_STARTLOADING
	// went (the answer to its CG_NAME). spectateAt: when it sat down
	// (SPECTATE_LOAD_SECONDS; never 0 then), 0 once that clock has removed
	// it. The join's memset clears all four. Server-only.
	char spectator;
	char welcomePending;
	char loadSent;
	enet_uint32 spectateAt;

	// Watching (1111, Watch_*): the seat whose kart this one's camera
	// follows, + 1 (0 nobody; CG_WATCH) - kept from a spectator's seat on
	// (counted from its catch-up); watchersTold: the count this seat was
	// last sent as a racer watched. 0 at each race's load and the reset; the
	// join's memset clears both. Server-only.
	unsigned char watching;
	unsigned char watchersTold;

	// This race as a spectator's catch-up retells it (1110,
	// Spectator_CatchUp): the order this player's CG_ENDRACE came in (1 the
	// first, 0 none yet), its ENDRACE_BOT, its course and best-lap times, and
	// the last kart message relayed for them, as the room got it. Kept after
	// they leave (a finisher's kart stays on every racer's screen); cleared as
	// each race loads. Server-only.
	unsigned char endOrder;
	unsigned char endFlags;
	int endCourse;
	int endLap;
	struct SG_EverythingKart lastKart;
	char hasLastKart;
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

	// What SendRoomData last said about joining, so ServerState_Tick can mark
	// the room list for its next push (RoomList_Dirty, RoomList_Flush) once
	// it changes (a clock crossing the close line, and back when the player
	// it belonged to is gone).
	char boolJoinClosed;

	// ...and whether it said the race in it can be watched (1110,
	// RoomOpenForSpectate): the green passing, the close before the forced
	// return, the last spare seat taken, all pushed the same way.
	char boolSpectateOpen;

	// enet_time_get() of the last NET_EV_PING round the room was sent
	// (Ping_Tick, 1114), 0 none yet
	enet_uint32 pingSentAt;

	// ...and whether it said the room is in a race (1111, RoomInRace): its
	// load and its reset - pushed even when the room was shown closed
	// already (a pick clock's last seconds before the load), so the row
	// offers RACE IN PROGRESS from the load on, not from the client's next
	// refresh.
	char boolRaceShown;

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

	// When the race's green light shows (1111, Matt: "JOIN ROOM greyed until
	// the green"): SG_STARTRACE + the lights (TAG_LIGHTS_MS), | 1 so it is
	// never 0; 0 before the race starts (cleared at SG_STARTLOADING and the
	// reset). A race is watched from here on (RoomOpenForSpectate) - a plain
	// race's as a round's, whose own green is roundGreenAt - where 1110 let a
	// plain race be watched from its lights' start. Same wrap-safe arithmetic
	// as endTime. Server-only.
	enet_uint32 raceGreenAt;

	// The race as a spectator joins it (1110, Spectator_*), server-only:
	// raceSeats, the seats with a player as it loaded (SG_STARTLOADING) - no
	// other seat is given to a spectator (SpectateSeat: a racer's seat still
	// has their kart on every screen, left or not), and only these are
	// named to one; roundCode, the round code that message carried
	// (Round_Begin), 0 once Round_Tick calls the round off at the green - a
	// spectator's own SG_STARTLOADING carries it; endCount, the CG_ENDRACEs
	// counted so far, for their order (PeerInfo.endOrder). Set as each race
	// loads.
	unsigned short raceSeats;
	unsigned char roundCode;
	unsigned char endCount;

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

RoomInfo roomInfos[16] = { 0 };   // { 0 }, not { NULL }: clang's -Wall wants braces round a NULL that fills a nested member

// Defined with the idle clock helpers further down; the room list needs it.
static int RoomOpenForJoin(RoomInfo* ri);
// (and whether its race can be watched: 1110, beside it)
static int RoomOpenForSpectate(RoomInfo* ri);
// (and whether it is in a race at all: 1111, beside that)
static int RoomInRace(RoomInfo* ri);

// ------------------------------------------------------------- kick bans
//
// The room's VOTE TO KICK (KickVote_*; the host's KICK PLAYER up to 1109)
// keeps the player it removes out of the room they were voted out of for
// KICK_BAN_SECONDS, by their address - the one thing a reconnect keeps (a
// household behind one NAT shares it, and is kept out of that room along
// with them). The room shows closed to them, on their room list and in
// Quick Play's query (KickBan_Mask), and a join is refused; every other room
// is theirs. Kept apart from RoomInfo, so the room emptying (its memset)
// does not lift it. Never on the wire; KICK_BAN_SECONDS is in global.h, as
// the client goes by it too (ServerLive_Kicked, CL_main.cpp). How many at
// once (KICK_BANS): SV_config.h.

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
static int kickVoteLog = 0;                      // --kicklog: every vote to kick in the log, voter and all - for testing only (KickVote_*)
static int spectateEnabled = SPECTATE_ENABLED;   // --nospectate: 0, no race in progress can be watched (RoomOpenForSpectate)
static int watchLog = 0;                         // --watchlog: every camera's report in the log, who watches whom - for testing only (Watch_Set, 1111)

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
		{ "NO ITEMS", "CLASSIC", "POTION GAME", "BOMBARDMENT", "ELIMINATION", "TAG, YOU'RE IT!", "NO ITEMS ULTRA SACRED FIRE", "CLASSIC ULTRA SACRED FIRE" };   // (the room list's names, 1117)
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

// The room's broadcasts skip a spectator still held (1110, spectator 1: on
// the room list, or loading the race it joined). It is told the race in one
// go at its CG_STARTRACE (Spectator_CatchUp), in order, and anything of the
// room's before that would come out of order with it - or, a kart message,
// a verdict, a finish, reach a client with no race yet to put it in. Two
// kinds get through once its SG_STARTLOADING has gone (loadSent) and while
// no welcome is pending: SG_NAME - a racer leaving during its load empties
// their name before its karts are born, or has their kart held after - and
// SG_HOST. The message's type is its first byte's low nibble, as every
// message's is.
static int Broadcast_Held(const PeerInfo* pi, const void* data)
{
	int type;

	if (pi->spectator != 1)
		return 0;
	type = ((const unsigned char*)data)[0] & 0xF;
	if ((type == SG_NAME || type == SG_HOST) && pi->loadSent && !pi->welcomePending)
		return 0;
	return 1;
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

		// (a spectator held: Broadcast_Held)
		if (Broadcast_Held(&ri->peerInfos[i], data))
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

		// (a spectator held: Broadcast_Held)
		if (Broadcast_Held(&ri->peerInfos[i], data))
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
	// this connection's nonce, what its joins must sign (1115, SV_auth.c)
	memcpy(mr.nonce, Auth_Nonce(peer), OCTR_AUTH_NONCE_LEN);


// The players in each room, and in closedMask the rooms that cannot be
// joined (RoomOpenForJoin), which the ROM shows as locked and, inside the
// room, as ROOM CLOSED on the PLAYERS box (lobbyGrid.c). And in
// spectateMask (1110) the rooms whose race can be watched now
// (RoomOpenForSpectate), which light RACE IN PROGRESS's JOIN ROOM on a row
// the ROM cannot join (hostOptions.c); in racingMask (1111) the rooms in a
// race at all (RoomInRace: its load, start line, lights, race and results),
// which open that box - JOIN ROOM greyed until the race can be watched,
// WAIT TO JOIN beside it (Matt, 1111). Its spectators are counted with its
// players: they hold a seat.
#define SETUP(x, index) \
	x = roomCount[index]; \
	if (!RoomOpenForJoin(&roomInfos[index])) \
		mr.closedMask |= (unsigned short)(1u << (index)); \
	if (RoomOpenForSpectate(&roomInfos[index])) \
		mr.spectateMask |= (unsigned short)(1u << (index)); \
	if (RoomInRace(&roomInfos[index])) \
		mr.racingMask |= (unsigned short)(1u << (index));

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

		// nor, there, a race to watch (1110): a spectator is one of the
		// room's players from its reset on - and the join's check refuses
		// it anyway (CG_JOINROOM). Nor a race in it at all (1111): the row
		// keeps 1110's Waiting to Join toggle, no RACE IN PROGRESS box
		// offering a join that would be refused.
		mr.spectateMask &= (unsigned short)~ban;
		mr.racingMask &= (unsigned short)~ban;

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
	// past QUERY_PER_IP_PER_SEC from one address: taken (not ENet's) and
	// unanswered (Guard_Query, SV_auth.c)
	if (!Guard_Query(host->receivedAddress.host))
		return 1;

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
		// and the races that can be watched (1110), as SG_ROOMS has them -
		// never in a room the asker was kicked from (above)
		if (RoomOpenForSpectate(&roomInfos[i]))
			r.spectateMask |= (unsigned short)(1u << i);
	}

	b.data = &r;
	b.dataLength = sizeof(r);
	enet_socket_send(host->socket, &host->receivedAddress, &b, 1);
	return 1;
}

// ---------------------------------------------------------- the room list
//
// The room list (SendRoomData) goes to every connection on the server when a
// room changes: a join, a leave, a track picked or cancelled, a race's load,
// start or end, a kick's end. Each change marks the list dirty, and the tick
// sends it once, at the end of its pass (RoomList_Flush) - and no sooner
// than ROOM_LIST_MIN_MS after the last (SV_config.h): a host toggling the
// track pick would otherwise have a list sent to everybody for every toggle.
// A joiner is still sent its own list at once (CG_JOINROOM), ahead of its
// welcome, as before.
static int g_roomListDirty = 0;
static enet_uint32 g_roomListSentAt = 0;

static void RoomList_Dirty(void)
{
	g_roomListDirty = 1;
}

static void RoomList_Flush(void)
{
	enet_uint32 now = enet_time_get();

	if (!g_roomListDirty)
		return;
	if (g_roomListSentAt != 0 && (enet_uint32)(now - g_roomListSentAt) < ROOM_LIST_MIN_MS)
		return;
	g_roomListDirty = 0;
	g_roomListSentAt = now ? now : 1;
	ForeachPeerLPL(SendRoomData);
}

// ------------------------------------------------------------- rate limits
//
// What one connection may send (SV_config.h), by kind: the kart message
// (RATE_KART_*: a client sends 30 a second, 60 with the 60 FPS disc), the
// weapon messages (RATE_EVENT_*: they come in bursts) and everything else
// (RATE_LOBBY_*: a join, a name, a pick, a vote...). Every message is
// counted before anything else is made of it: an empty one, or one of a
// type no client sends, as the lobby's; any other by its type, short or not.
// A bucket of tokens for each kind: PER_SEC is its refill a second, BURST
// what it holds. Each message takes a token; one that finds none is dropped.
//
// A bucket rather than a count for each second: ENet holds a reliable
// message behind an earlier one that was lost, and hands the whole run over
// in one go when the lost one's resend lands - after a Wi-Fi stall of a
// second or two, everything a player did in it arrives together. A plain
// count kept the oldest of that run and dropped the newest, which could be
// the character pick's lock-in, sent once and never again: the room would
// wait on it until that player was removed for idling. The bursts hold any
// backlog a stall short of ENet's 5 s timeout can build (about 90 lobby
// messages at the fastest a player can press), and still empty in a moment
// under a flood. A connection with FLOOD_DROPS_PER_SEC dropped in one
// counted second - one that starts at its first drop after the last such
// second ended, so a burst across two of them can reach about twice that -
// is a flood, not a player: disconnected, and the log says so. A steady
// flood is caught within its first second or two either way.
//
// Kept by ENet's index for the connection, so one on the room list is held
// to it as one in a room is; filled as each connection is made (CONNECT).
enum { RATE_KART, RATE_EVENT, RATE_LOBBY, RATE_KINDS };

typedef struct
{
	unsigned int milli[RATE_KINDS];    // tokens left, by kind, in thousandths
	enet_uint32 refillAt;              // enet_time_get() of the last refill
	enet_uint32 dropAt;                // when the current counted second of drops began
	unsigned short dropped;            // dropped in that counted second
	unsigned char flooded;             // disconnected for it: nothing more taken
} PeerRate;

static PeerRate peerRates[MAX_PEERS];
static const unsigned int rateRefill[RATE_KINDS] = { RATE_KART_PER_SEC, RATE_EVENT_PER_SEC, RATE_LOBBY_PER_SEC };
static const unsigned int rateBurst[RATE_KINDS] = { RATE_KART_BURST, RATE_EVENT_BURST, RATE_LOBBY_BURST };

STATIC_ASSERT2(MAX_PEERS >= 16 * MAX_CLIENTS, "MAX_PEERS: fewer connections than seats");
STATIC_ASSERT2(MAX_PEERS <= ENET_PROTOCOL_MAXIMUM_PEER_ID, "MAX_PEERS: past ENet's ceiling");
STATIC_ASSERT2(RATE_KART_BURST >= RATE_KART_PER_SEC && RATE_EVENT_BURST >= RATE_EVENT_PER_SEC &&
	RATE_LOBBY_BURST >= RATE_LOBBY_PER_SEC, "RATE_*_BURST: a bucket holds at least a second's refill");
STATIC_ASSERT2(RATE_KART_BURST <= 1000000 && RATE_EVENT_BURST <= 1000000 && RATE_LOBBY_BURST <= 1000000,
	"RATE_*_BURST: in thousandths, a bucket must fit an unsigned int");
STATIC_ASSERT2(RATE_KART_PER_SEC >= 1 && RATE_EVENT_PER_SEC >= 1 && RATE_LOBBY_PER_SEC >= 1,
	"RATE_*_PER_SEC: at least 1 (0 is not 'off': it would divide by zero at the refill)");
STATIC_ASSERT2(FLOOD_DROPS_PER_SEC >= 1 && FLOOD_DROPS_PER_SEC <= 65535,
	"FLOOD_DROPS_PER_SEC: 1 to 65535 (PeerRate.dropped is an unsigned short)");

static int Rate_Kind(int type)
{
	switch (type)
	{
		case CG_RACEDATA:    return RATE_KART;
		case CG_WEAPON:
		case CG_WEAPONEVENT: return RATE_EVENT;
		default:             return RATE_LOBBY;
	}
}

// A new connection in this slot (CONNECT): its buckets full, nothing dropped.
static void Rate_Reset(ENetPeer* peer)
{
	PeerRate* r = &peerRates[peer - server->peers];
	memset(r, 0, sizeof(PeerRate));
	for (int k = 0; k < RATE_KINDS; k++)
		r->milli[k] = rateBurst[k] * 1000u;
	r->refillAt = enet_time_get();
	r->dropAt = r->refillAt;
}

// 1: the message is taken. 0: dropped, its kind's bucket empty. type is the
// message's type, or -1 for an empty one (counted as the lobby's).
static int Rate_Take(ENetPeer* peer, int type)
{
	PeerRate* r = &peerRates[peer - server->peers];
	enet_uint32 now = enet_time_get();
	enet_uint32 elapsed = now - r->refillAt;
	int kind = Rate_Kind(type);

	if (r->flooded)
		return 0;

	// refill every bucket for the time since the last message, a token a
	// thousandth at a time: rate a second is rate thousandths a millisecond
	if (elapsed > 0)
	{
		r->refillAt = now;
		for (int k = 0; k < RATE_KINDS; k++)
		{
			unsigned int cap = rateBurst[k] * 1000u;
			unsigned int room = cap - r->milli[k];
			if (elapsed >= room / rateRefill[k] + 1)
				r->milli[k] = cap;
			else
				r->milli[k] += elapsed * rateRefill[k];
			if (r->milli[k] > cap)
				r->milli[k] = cap;
		}
	}

	if (r->milli[kind] >= 1000)
	{
		r->milli[kind] -= 1000;
		return 1;
	}

	if ((enet_uint32)(now - r->dropAt) >= 1000)
	{
		r->dropAt = now;
		r->dropped = 0;
	}
	if (++r->dropped >= FLOOD_DROPS_PER_SEC)
	{
		r->flooded = 1;
		PrintPrefix(-1);
		printf("Connection [%08x] sent %u messages over its limits in a second: disconnected as a flood\n",
			peer->address.host, (unsigned)r->dropped);
		enet_peer_disconnect(peer, 0);
	}
	return 0;
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
// The lowest seat still connected comes first (1110), as Vote_Voter has it:
// one vote to kick can bring on another (KickVote_Check), and a host voted
// out whose DISCONNECT lands first would otherwise hand the room to a lower
// seat already on its way out too - a host for a round trip, then a second
// migration. If none is (every one left on its way out: kicks, idle
// removals), any seat with a peer, as before.
// A spectator's seat (1110) only when no player's is left connected: the
// host's role is the lobby's, and a spectator is in the lobby only from the
// room's reset on - one still loading then is welcomed only at its own
// CG_STARTRACE (welcomePending), and a host with no clock till then would
// hold up the room's track pick. Among spectators, the lowest seat, as for
// the players.
static int PickNewHost(RoomInfo* ri)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED &&
			ri->peerInfos[i].spectator == 0)
			return i;

	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED)
			return i;

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

// ------------------------------------------------- each racer's WATCHERS
//
// A racer's WATCHERS (playerCount.c draws octr->watchers while it is 1 or
// more) counts the players whose camera is on that racer's kart (1111,
// Matt) - in 1110 it was the room's spectators, one number for every racer.
// Each client reports the kart its own camera follows as it settles
// (CG_WATCH: Watch_Service, CL_main.cpp - a spectator's from its race's wait
// on, a player's over the line or out of a round; a racer's camera is its
// own kart, nobody), and the server keeps one target per seat (watching)
// and tells each racer its own count, to it alone, as it changes
// (SG_LOBBYCOUNTDOWN's LOBBYCOUNT_WATCHERS, the count in its seconds'
// place; watchersTold the last sent). Reliable on channel 0, as the room's
// messages, so each count lands in order and after the race's own start. A
// count changes on a report (the target it left and the one it is on now),
// a spectator's catch-up (CG_STARTRACE: its target, stored while it was
// held, counted from here) and any leave (the target it watched; nobody
// watches a leaver any more). Nothing at the load or the reset: both sides
// clear (SG_STARTLOADING, ResetRoomAfterRace; the client at SG_STARTLOADING
// and SG_NEWCLIENT). One log line per count sent ("is watched by"), and,
// with --watchlog, one per report stored ("now watching").

// The seats watching racer t (1111, Matt): cameras on t's kart, by their
// CG_WATCH - a spectator caught up (2) or a player (0: over the line or out;
// a racer's client reports nobody). A spectator still held (1) is not
// counted till its catch-up (CG_STARTRACE), as in 1110.
static int Watch_Count(RoomInfo* ri, int t)
{
	int n = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		const PeerInfo* pi = &ri->peerInfos[i];
		if (i != t && pi->peer != 0 && pi->spectator != 1 && pi->watching == t + 1)
			n++;
	}
	return n;
}

// Racer t told its own count (SG_LOBBYCOUNTDOWN's LOBBYCOUNT_WATCHERS: its
// HUD's WATCHERS, playerCount.c, shown at 1 and more) if it changed since it
// was last told - and, since 1117, every seat watching it (its spectators'
// WATCHERS, LOBBYCOUNT_TARGET_WATCHERS). A seat with no player, or a
// spectator's, is never a racer watched.
static void Watch_Tell(RoomInfo* ri, int t)
{
	PeerInfo* pt;
	struct SG_MessageLobbyCountdown sg;
	int n;

	if (t < 0 || t >= MAX_CLIENTS) return;
	pt = &ri->peerInfos[t];
	if (pt->peer == 0 || pt->spectator != 0) return;
	n = Watch_Count(ri, t);
	if (n == pt->watchersTold) return;
	pt->watchersTold = (unsigned char)n;

	sg.type = SG_LOBBYCOUNTDOWN;
	sg.kind = LOBBYCOUNT_WATCHERS;
	sg.seconds = (unsigned char)n;
	sendToPeerReliable(pt->peer, &sg, sizeof(struct SG_MessageLobbyCountdown));

	// and every seat whose camera is on it (1117, Matt: a spectator's WATCHERS,
	// top left - LOBBYCOUNT_TARGET_WATCHERS, a kind older clients pass over)
	sg.kind = LOBBYCOUNT_TARGET_WATCHERS;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		const PeerInfo* pw = &ri->peerInfos[i];
		if (i != t && pw->peer != 0 && pw->watching == t + 1)
			sendToPeerReliable(pw->peer, &sg, sizeof(struct SG_MessageLobbyCountdown));
	}

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %.*s (%d) is watched by %d\n", NAME_LEN, &pt->name[0], t, n);
}

// Seat id's camera on racer t (CG_WATCH; WATCH_NOBODY or any refusal:
// nobody): only in a race (boolLoadAll), only a seat racing in it (raceSeats)
// with its player still in, a player's, not its own. The racer it left and
// the one it is on now told. A refusal is stored as nobody too, so a seat
// that sends one is off its old target.
static void Watch_Set(RoomInfo* ri, int id, int t)
{
	PeerInfo* pi = &ri->peerInfos[id];
	int was = (int)pi->watching - 1;

	if (!ri->boolLoadAll || t < 0 || t >= MAX_CLIENTS || t == id ||
		((ri->raceSeats >> t) & 1) == 0 || ri->peerInfos[t].peer == 0 ||
		ri->peerInfos[t].spectator != 0)
		t = -1;
	if (t == was) return;
	pi->watching = (unsigned char)(t + 1);

	// (--watchlog only, for the tests and probes, as --kicklog's votes: a
	// public server's console gets the counts sent, below, not every
	// camera's move - a watcher flicking through the racers, who watches
	// whom)
	if (watchLog)
	{
		PrintPrefix((int)(ri - roomInfos) + 1);
		if (t < 0) printf("Player %.*s (%d) now watching nobody\n", NAME_LEN, &pi->name[0], id);
		else printf("Player %.*s (%d) now watching %.*s (%d)\n", NAME_LEN, &pi->name[0], id,
			NAME_LEN, &ri->peerInfos[t].name[0], t);
	}

	Watch_Tell(ri, was);
	Watch_Tell(ri, t);

	// and this seat its new racer's count even where that count did not change
	// (a spectator not caught up yet is not counted: Watch_Count) - 1117
	if (t >= 0 && pi->peer != 0)
	{
		struct SG_MessageLobbyCountdown sg;
		sg.type = SG_LOBBYCOUNTDOWN;
		sg.kind = LOBBYCOUNT_TARGET_WATCHERS;
		sg.seconds = (unsigned char)Watch_Count(ri, t);
		sendToPeerReliable(pi->peer, &sg, sizeof(struct SG_MessageLobbyCountdown));
	}
}

// Seat id gone (ProcessDisconnectEvent, its peer already NULL): the racer it
// watched told one fewer; nobody watches it any more (it has no peer to tell).
static void Watch_Leave(RoomInfo* ri, int id)
{
	int was = (int)ri->peerInfos[id].watching - 1;
	ri->peerInfos[id].watching = 0;
	ri->peerInfos[id].watchersTold = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].watching == id + 1)
			ri->peerInfos[i].watching = 0;
	Watch_Tell(ri, was);
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
// way out (a kick's, an idle removal's: enet_peer_disconnect), nor a
// spectator's (1110): one watching is in no lobby of this room's, and one
// whose welcome is pending (the room reset while it loaded) joins the lobby
// only at its welcome (Spectator_Welcome) - until then its seat is nobody to
// wait on, so a room of one player and a spectator loading is a room of one.
// (The room list and the site count spectators: they hold a seat.)
static int Room_Players(RoomInfo* ri)
{
	int n = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED &&
			ri->peerInfos[i].spectator == 0)
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

// The seat a spectator gets in this room (1110): the lowest empty one that
// was empty as the race loaded too (not in raceSeats) - a racer's seat, left
// or not, still has their kart on every racer's screen, parked or driven on,
// and a spectator in it would be that kart to nobody (decision 3). -1: none.
static int SpectateSeat(RoomInfo* ri)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer == 0 && ((ri->raceSeats >> i) & 1) == 0)
			return i;
	return -1;
}

// Can the race in this room be watched right now (1110: a join with
// JOIN_SPECTATE takes a seat in it as a spectator, CG_JOINROOM)? Only while
// it runs: past its green - every race's (raceGreenAt, 1111, Matt: JOIN ROOM
// greyed until the green; 1110 opened a plain race at its lights' start),
// and a round's lights still showing (roundGreenAt, the round not yet
// decided at its green by Round_Tick) could yet call the round off, which a
// spectator's SG_STARTLOADING would already have said otherwise - and not
// over (boolEndAll: the results countdown, too short for a load); not in the
// last SPECTATE_CLOSE_SECONDS before the lobby-return timer forces the room
// back (a load takes a good part of them, and a spectator arriving as the
// room resets would come into a lobby still loading); and with a seat to
// give (SpectateSeat). Never in a page of rooms not on offer
// (ROOM_OPEN_INDEX), nor with --nospectate.
// (SPECTATE_ENABLED, SPECTATE_CLOSE_SECONDS: SV_config.h)
static int RoomOpenForSpectate(RoomInfo* ri)
{
	if (!spectateEnabled || !ROOM_OPEN_INDEX((int)(ri - roomInfos)))
		return 0;

	if (!ri->boolRaceAll || ri->boolEndAll || ri->roundGreenAt != 0)
		return 0;

	if (ri->raceGreenAt != 0 && (int)(enet_time_get() - ri->raceGreenAt) < 0)
		return 0;

	if (ri->firstEndTime != 0 &&
		(int)(enet_time_get() - ri->firstEndTime) >= (LOBBY_RETURN_SECONDS - SPECTATE_CLOSE_SECONDS) * 1000)
		return 0;

	return SpectateSeat(ri) >= 0;
}

// Is this room in a race (1111: SG_MessageRooms.racingMask)? From its
// SG_STARTLOADING to its reset - the countdown, the load, the start line,
// the lights, the race, its results - on a server that lets races be watched
// (with --nospectate the room list keeps 1110's Waiting to Join alone), in
// a page on offer. RoomOpenForSpectate is never true without it.
static int RoomInRace(RoomInfo* ri)
{
	return spectateEnabled && ROOM_OPEN_INDEX((int)(ri - roomInfos)) &&
		(ri->boolLoadAll || ri->boolRaceAll);
}

// What the room list says of this room (SendRoomData), as it was last
// marked for a push for a change of its own (ServerState_Tick;
// ResetRoomAfterRace): whether it can be joined, whether its race can be
// watched, whether it is in a race (1111). 1 if any of the three moved
// since, the record brought up to date - and the list is the caller's to
// mark (RoomList_Dirty).
static int RoomList_Changed(RoomInfo* ri)
{
	char closed = (char)!RoomOpenForJoin(ri);
	char watch = (char)RoomOpenForSpectate(ri);
	char racing = (char)RoomInRace(ri);

	if (closed == ri->boolJoinClosed && watch == ri->boolSpectateOpen && racing == ri->boolRaceShown)
		return 0;
	ri->boolJoinClosed = closed;
	ri->boolSpectateOpen = watch;
	ri->boolRaceShown = racing;
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
	// (nor for a spectator's seat, 1110: in the lobby that is one whose
	// welcome is pending - the room reset while it loaded - and its welcome
	// arms the clock, Spectator_Welcome; the guests wait on no clock till
	// then)
	if (ri->peerInfos[ri->hostID].spectator != 0)
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
	RoomList_Dirty();

	// Track and laps are in: the host's clock simply runs on into the
	// character pick, and every guest's starts - or resumes, after a
	// cancelled track. Each is told their own number.
	// (not one on the way out: a kick's, an idle removal's - nor a
	// spectator's seat, 1110: one whose welcome is pending has no pick until
	// its welcome, which starts its clock, Spectator_Welcome)
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (ri->peerInfos[i].peer != 0 && ri->peerInfos[i].peer->state == ENET_PEER_STATE_CONNECTED &&
			ri->peerInfos[i].spectator == 0)
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

// A player still to be heard: connected (not on the way out - a kick), and
// a player, not a spectator (1110). In the lobby a spectator's seat is one
// whose welcome is pending (the room reset while it loaded): it has no vote
// until it is welcomed (Spectator_Welcome), so the vote closes on the
// players' votes alone (Vote_Tick), and no clock of its own starts
// (Solo_Tick) - and, through KickVote_Player, it neither votes to kick nor
// can be voted out, nor counts in what a kick needs.
static int Vote_Voter(PeerInfo* pi)
{
	return pi->peer != 0 && pi->peer->state == ENET_PEER_STATE_CONNECTED && pi->spectator == 0;
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

// ----------------------------------------------------- vote to kick (1110)
//
// The lobby's VOTE TO KICK, in place of the host's KICK PLAYER: every player
// in the room has it on OPTIONS (hostOptions.c), against any other player,
// and picking that player again takes the vote back (HOST_ACTION_KICKVOTE,
// KICKUNVOTE: CG_HOSTACTION, from any seat). Once the votes against a player
// reach KICK_VOTES_BY_PLAYERS for the players in the room (global.h, Matt's
// table: the one voted against counted among them, never voting), they are
// out, and kept out of the room for KICK_BAN_SECONDS (KickBan_*), as the
// host's kick kept them. The votes are secret. A vote is answered to its
// voter alone, with their own votes as they stand (SG_VOTE's
// VOTE_KICK_REPLY; the client keeps them in octr->kickVotes for the ROM's
// pick); the room hears of nothing until a player is removed, and then only
// their empty SG_NAME, as for anybody leaving; and the log gives the count,
// never who voted (--kicklog, for testing, says it all). Only in the lobby:
// the countdown (SG_STARTLOADING) wipes every vote, as it closes the ROM's
// OPTIONS, and a vote crossing it is refused. A leaver's votes go with
// them, and the votes against them (KickVote_Forget); the room left smaller
// needs fewer, and the next tick looks again (KickVote_Check). Players are
// counted as the ROM's PLAYERS title counts them (KickVote_Player):
// connected, not one already on the way out, and named.

// The votes a kick needs by the players in the room, 0 to MAX_CLIENTS (0:
// none can be cast - the ROM greys VOTE TO KICK)
static const unsigned char kickVotesNeeded[] = { KICK_VOTES_BY_PLAYERS };
STATIC_ASSERT2(sizeof(kickVotesNeeded) == MAX_CLIENTS + 1, "KICK_VOTES_BY_PLAYERS: one count for each number of players, 0 to MAX_NUM_PLAYERS");

// A player the vote to kick counts: still to be heard (Vote_Voter: one on
// the way out counts no more, nor a spectator's seat, 1110) and named. A
// newcomer whose CG_NAME is not in yet is a seat the ROMs show nobody in -
// their PLAYERS count leaves it out (HostOpt_Players, hostOptions.c), so
// their greying and this count are the same number - and it neither votes
// nor raises the count the others need.
// A name stays once given (CG_NAME), so nobody leaves the count by renaming.
static int KickVote_Player(PeerInfo* pi)
{
	return Vote_Voter(pi) && pi->name[0] != 0;
}

// The votes a kick needs in this room now (0: no vote to kick), and the
// players that is for. Not Room_Players: that is Room_Solo's count, for the
// idle clocks and the track vote, where a seat with no name yet is somebody
// in the room.
static int KickVote_Needed(RoomInfo* ri, int* players)
{
	int n = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (KickVote_Player(&ri->peerInfos[i]))
			n++;
	*players = n;
	return kickVotesNeeded[n];
}

// The votes against seat t, by the players the vote counts (KickVote_Player).
static int KickVote_Count(RoomInfo* ri, int t)
{
	int n = 0;
	for (int v = 0; v < MAX_CLIENTS; v++)
		if (v != t && KickVote_Player(&ri->peerInfos[v]) && ((ri->peerInfos[v].kickVotes >> t) & 1))
			n++;
	return n;
}

// Seat id's own votes gone, and every vote against it: its player is
// leaving (ProcessDisconnectEvent, KickVote_Kick), or it is a newcomer's
// (the join - a guard: the last one's went as they left).
static void KickVote_Forget(RoomInfo* ri, int id)
{
	for (int v = 0; v < MAX_CLIENTS; v++)
		ri->peerInfos[v].kickVotes &= (unsigned short)~(1u << id);
	ri->peerInfos[id].kickVotes = 0;
}

// The answer to a vote, to its voter alone: what became of it (KICKVOTE_*)
// and all their votes as they stand now, which their ROM shows.
static void KickVote_Reply(ENetPeer* peer, int target, int result, unsigned short mine)
{
	struct SG_MessageKickVote sg;
	memset(&sg, 0, sizeof(sg));
	sg.type = SG_VOTE;
	sg.phase = VOTE_KICK_REPLY;
	sg.result = (unsigned char)(result & 3);
	sg.target = (unsigned char)target;
	sg.mine = mine;
	sendToPeerReliable(peer, &sg, sizeof(struct SG_MessageKickVote));
}

// Seat t voted out, as the idle clock removes a player (ServerState_Tick):
// the disconnect says why - the client puts its player back on this
// server's room list, KICKED BY VOTE - and ProcessDisconnectEvent tells the
// room once it lands (the empty SG_NAME; a new host, if it was the host).
// Until then no clock of theirs runs out and no lock-in of theirs starts
// the race, and their votes, and the votes against them, are gone at once.
// The log names the player and the count, nobody who voted.
static void KickVote_Kick(RoomInfo* ri, int t, int votes, int need, int players)
{
	PeerInfo* pi = &ri->peerInfos[t];

	pi->idleStart = 0;
	pi->boolLoadSelf = 0;
	pi->vote = 0;   // and their vote out of the room's track or lap vote, at once
	KickVote_Forget(ri, t);
	KickBan_Add(pi->peer->address.host, (int)(ri - roomInfos));

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %.*s (%d) removed by the room's vote (%d of %d needed, %d players), kept out of the room for %ds\n",
		NAME_LEN, &pi->name[0], t, votes, need, players, kickBanSeconds);

	enet_peer_disconnect(pi->peer, DISCONNECT_REASON_KICKED);
	if (ri->votePhase != VOTE_NONE)
		Vote_Broadcast(ri);   // (the kicked one, on the way out, skipped)
}

// Anybody with the votes against them at the count: out. On each vote cast
// (CG_HOSTACTION), and each tick (ServerState_Tick) for a room that has got
// smaller since its votes were cast: fewer players need fewer votes. One
// removal can bring the count down to another player's votes, so the room
// is looked at again, as many times as it has seats at most. Never once the
// race is loading: the countdown wiped the votes. And a room down to fewer
// players than a vote needs (1110: no vote to kick with one or two, Matt)
// keeps none: every vote standing is wiped, each voter told (their red
// names cleared), so nothing from before counts again when a third player
// comes - nor sits on a pick its voter can no longer open to take it back.
static void KickVote_Check(RoomInfo* ri)
{
	if (ri->boolLoadAll)
		return;

	for (int pass = 0; pass < MAX_CLIENTS; pass++)
	{
		int players;
		int need = KickVote_Needed(ri, &players);
		int t, votes = 0;

		if (need == 0)
		{
			for (t = 0; t < MAX_CLIENTS; t++)
			{
				PeerInfo* pv = &ri->peerInfos[t];
				if (pv->kickVotes == 0)
					continue;
				pv->kickVotes = 0;
				if (Vote_Voter(pv))
					KickVote_Reply(pv->peer, 0xFF, KICKVOTE_REMOVED, 0);
				if (kickVoteLog)
				{
					PrintPrefix((int)(ri - roomInfos) + 1);
					printf("Kick votes (--kicklog): seat %d's wiped, the room down to %d players\n", t, players);
				}
			}
			return;
		}
		for (t = 0; t < MAX_CLIENTS; t++)
			if (Vote_Voter(&ri->peerInfos[t]) && (votes = KickVote_Count(ri, t)) >= need)
				break;
		if (t == MAX_CLIENTS)
			return;
		KickVote_Kick(ri, t, votes, need, players);
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

// The same word to one client alone (1110): a spectator's catch-up retelling
// the round to it (Spectator_CatchUp), which the room's broadcasts skipped
// while it was held.
static void Round_SendTo(ENetPeer* peer, int kind, int id, int owner, int serial, int arg)
{
	struct SG_MessageWeaponEvent s;
	s.type = SG_WEAPONEVENT;
	s.clientID = id & 0xF;
	s.owner = owner & 0xF;
	s.kind = kind & 0xF;
	s.serial = (unsigned char)serial;
	s.arg = (unsigned char)arg;
	sendToPeerReliable(peer, &s, sizeof(struct SG_MessageWeaponEvent));
}

// A player the round still has: in the room and not placed. (Nobody joins
// mid-round as a player, so everybody in the room was seated as it loaded; a
// leaver's seat is empty.) Never a spectator (1110), who joins mid-round in
// a seat that was empty at the load, and is in no race. Any ID: out of range
// is nobody, so a client's 4-bit field can be passed as it came.
static int Round_IsIn(RoomInfo* ri, int id)
{
	return id >= 0 && id < MAX_CLIENTS &&
		ri->peerInfos[id].peer != 0 && ri->peerInfos[id].place == 0 &&
		ri->peerInfos[id].spectator == 0;
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
// A player already placed keeps their place. A spectator leaving (1110) was
// never in the round: nothing.
static void Round_Leave(RoomInfo* ri, int id)
{
	if (id < 0 || id >= MAX_CLIENTS)
		return;
	if (!ri->roundLive || ri->roundOver || ri->peerInfos[id].place != 0 || ri->peerInfos[id].spectator != 0)
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
		ri->roundCode = 0;   // (1110) a spectator loads a plain race, as the room races one now
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

		// The leaver's votes to kick go with them, and every vote against
		// them (KickVote_Forget); a count the smaller room now reaches is
		// the tick's (KickVote_Check). Above the boolEndSelf early return
		// below, so that no way out skips it. A room left below three
		// players wipes its votes here and now, not on the tick: a newcomer
		// in the same batch of messages would otherwise find them standing.
		KickVote_Forget(ri, peerID);
		{
			int left;
			if (KickVote_Needed(ri, &left) == 0)
				KickVote_Check(ri);   // only the wipe, with nothing to count
		}

		// The racer the leaver's camera was on is told one fewer, and
		// nobody watches the leaver any more (Watch_Leave, 1111). Above the
		// silent leave's early return below too: a watcher is a spectator or
		// a player over the line, both of whom leave that way.
		Watch_Leave(ri, peerID);

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
		// A spectator's leave (1110) is as silent, whatever its race's state
		// (its boolEndSelf is set from its join, but not once a reset left
		// its welcome pending): the racers were never told its name, so an
		// empty one would tell them nothing. The one fewer on the WATCHERS of
		// the racer it watched is Watch_Leave's, above (1111).
		if (ri->peerInfos[peerID].boolEndSelf != 0 || ri->peerInfos[peerID].spectator != 0)
		{
			RoomList_Dirty();
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

	RoomList_Dirty();   // someone left a room: everybody's list (RoomList_Flush)
}

// special: 0, or NEWCLIENT_SPECTATOR (1110) for a seat taken to watch the
// race in progress (Spectator_Join): its client keeps the ROM on the room
// list, puts no name of its own in, and loads the race at the
// SG_STARTLOADING its CG_NAME is answered with. Every other welcome - a
// join, the room's reset, a spectator's welcome - is 0, which makes a
// spectator's client a player's again.
void WelcomeNewClient(RoomInfo* ri, int id, int special)
{
	// Send acceptance to client
	struct SG_MessageClientStatus mw;
	mw.type = SG_NEWCLIENT;
	mw.clientID = id;
	mw.numClientsTotal = ri->clientCount;

	// ordinary day (or a spectator's seat)
	mw.special = special & 0xF;
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
		case CG_WATCH:      return sizeof(struct CG_MessageWatch);
		case CG_CHEAT:      return sizeof(struct CG_MessageCheat);
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
// Every racer's round trip to this server, told to the whole room once a
// second while it races (boolRaceAll): SG_WEAPONEVENT, kind NET_EV_PING,
// clientID the player, identity its own, arg ENet's smoothed estimate in
// ms / 4 (255 at most, 1 at least once ENet has one: 0 is "none yet" on
// the ROM). Unsequenced, a round a second: a lost one is made up for by
// the next. The ROM keeps them by slot for the warp orb's tie reach
// (NetOrb_Tie, netWeapons.c): a leader on a high ping is further ahead
// than its copy shows, and the shooter's own ping alone said too little
// (1114, Matt: orbs fizzling with the leader just ahead).
#define PING_TICK_MS 1000
static void Ping_Tick(RoomInfo* ri)
{
	enet_uint32 now = enet_time_get();
	struct SG_MessageWeaponEvent s;
	int i;

	if (!ri->boolRaceAll) { ri->pingSentAt = 0; return; }
	if (ri->pingSentAt != 0 && (int)(now - ri->pingSentAt) < PING_TICK_MS) return;
	ri->pingSentAt = now ? now : 1;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ENetPeer* peer = ri->peerInfos[i].peer;
		unsigned int rtt;
		if (peer == 0 || peer->state != ENET_PEER_STATE_CONNECTED) continue;
		rtt = peer->roundTripTime / 4;
		if (rtt == 0) rtt = 1;
		if (rtt > 255) rtt = 255;
		memset(&s, 0, sizeof(s));
		s.type = SG_WEAPONEVENT;
		s.clientID = i;
		s.owner = i;
		s.kind = NET_EV_PING;
		s.serial = 0;
		s.arg = (unsigned char)rtt;
		broadcastToPeersUnreliable(ri, &s, sizeof(s));
	}
}

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
		if (ri->peerInfos[i].spectator != 0) continue;        // a spectator (1110): no kart, and its claims are dropped
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

// A masked kart's bump as its own machine saw it (NET_EV_KART_CLAIM, 1107)
// - or a shield's, mask or not (1109: the claim's hit, its serial, the
// shield's NET_CLAIM_HIT(2, 0)): to the bumped kart's machine alone, which
// judges it (netWeapons.c) and, when it takes, sends its kart's hurt as ever
// (NET_EV_KART_HURT, to the room and the referee). The two players' round
// trips go with it, which only the server knows: that machine rewinds its
// kart's path by them to where the claimant's screen had it. In item rooms,
// between two players still in a round, from a player who fired a mask
// lately (MASK_SHOT_MS: CG_WEAPON) or - a shield's bump - a shield not yet
// claimed with (shieldClaim: one claim a shield, its pop), or - a squish
// (1110: NET_CLAIM_HIT(3, 5), mask or not) - a player whose kart showed
// Saffi lately (SAFFI_SEEN_MS: its kart messages' FLAME_SAFFI) or who fired
// a TURBO lately (TURBO_SHOT_MS), at most once a KART_CLAIM_GAP_MS for each
// pair.
#define KART_CLAIM_GAP_MS 400     // the ROM's own gap is 500 ms (CLAIM_GAP_MS)
#define SHIELD_SERIAL_NONE 0x100  // no shield fired this race: no shot count (8 bits) is it
#define MASK_SHOT_MS      20000   // a mask's longest, 12 s of game time (0x2d00, 960 a second) and its fade, with room for a game running slow: only a gate against abuse, the bumped kart's machine checks the mask itself
#define SAFFI_SEEN_MS     3000    // Saffi in the claimant's kart messages (unreliable, every frame) this lately: a squish just after Saffi ended, or past a few lost messages, still goes - only a gate against abuse, the squished kart's machine checks its copy of the rammer itself
#define TURBO_SHOT_MS     20000   // a TURBO's squish lasts as long as its fire, which boosts can keep going: as the mask's, only a gate against abuse
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
	int maskOn = pa->maskShotAt != 0 && (int)(now - pa->maskShotAt) <= MASK_SHOT_MS;
	int shieldBump = r->serial == NET_CLAIM_HIT(2, 0);
	int squish = r->serial == NET_CLAIM_HIT(3, 5);
	int saffiOn = pa->saffiSeenAt != 0 && (int)(now - pa->saffiSeenAt) <= SAFFI_SEEN_MS;
	int turboOn = pa->turboShotAt != 0 && (int)(now - pa->turboShotAt) <= TURBO_SHOT_MS;
	if (!maskOn && !(shieldBump && pa->shieldClaim) && !(squish && (saffiOn || turboOn))) return;
	if (pa->claimAt[victim] != 0 && (int)(now - pa->claimAt[victim]) < KART_CLAIM_GAP_MS) return;
	pa->claimAt[victim] = now | 1;
	if (shieldBump) pa->shieldClaim = 0;

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

// --------------------------------------- a race in progress watched (1110)
//
// Matt: "If a match is in progress and it has room, add an option to ask the
// player if they would like to join the match in progress. When they do,
// simply make them a spectator." A room racing past its green, not over,
// with a seat that was empty as the race loaded, can be watched
// (RoomOpenForSpectate: the room list's spectateMask, Quick Play's reply). A
// join into it that asks (CG_MessageRoom's JOIN_SPECTATE) - the room cannot
// be joined as a player then - takes that seat as a spectator
// (Spectator_Join), and its client keeps the ROM on the room list. Its
// CG_NAME is answered with the racers' names and characters and the race to
// load, SG_STARTLOADING with the round's code (Spectator_Name): its ROM loads
// the race on its own, a kart for each racer and its own hidden, parked off
// the track and finished (spectate.c). While it loads it is held: the room's
// broadcasts skip it, but for its SG_NAMEs and SG_HOST (Broadcast_Held). Its
// CG_STARTRACE says its race is loaded: it is caught up in one go, in order
// - the karts where they are, the round's verdicts, the finishes, the
// lobby's clock - and started (Spectator_CatchUp); from then on it is
// watching and gets every message the room gets, and the racer its camera
// is on is told one more watches them (Watch_*, 1111; in 1110 every racer
// was told the room's spectators). Nothing it sends of a race or a lobby
// counts (SPECTATOR_DROPS), its name never reaches a racer mid-race, and it
// is in no race rule and no lobby count (Round_IsIn, Vote_Voter,
// Room_Players, WeaponHold_Window, PickNewHost's preference) - but it holds
// a seat, so the room list and the site count it. Its leave is as silent as
// a finisher's. At the room's reset it is a player again
// (ResetRoomAfterRace): welcomed with the rest if it was watching or still on
// the room list, or, still loading, at its own CG_STARTRACE
// (Spectator_Welcome) - its client never meets a lobby in the middle of a
// load (the lost-start class: OnlineInit_Drivers would overwrite the lobby
// state with the race's). With --nospectate none of this ever starts.

// A racer a spectator is told of (its SG_NAMEs, SG_CHARACTERs and the
// catch-up's kart messages): a seat that was racing as the race loaded
// (raceSeats), a player's - never another spectator's - and either still in
// the room or gone after their finish (boolEndSelf: that leave is silent, so
// every racer's screen still has their kart, named; the catch-up's SG_ENDRACE
// brakes it at once). A racer gone before finishing has an empty name on
// every racer's screen (their SG_NAME at the leave): to a spectator, nobody.
static int Spectator_Racer(RoomInfo* ri, int j)
{
	PeerInfo* pj = &ri->peerInfos[j];
	return ((ri->raceSeats >> j) & 1) && pj->spectator == 0 &&
		(pj->peer != 0 || pj->boolEndSelf != 0);
}

// A join that asks to watch (JOIN_SPECTATE) into a room whose race can be
// (RoomOpenForSpectate, so SpectateSeat has a seat for it): seated as a
// spectator, held. Its race flags are set as a racer's are at the end of a
// race - loaded, racing, ended - so the room's load, start and end never
// wait on it, and its leave is a finisher's, silent. No host claim, no vote
// and no clock: it is in no lobby until the room's reset. To it alone first
// - SG_NEWCLIENT marked NEWCLIENT_SPECTATOR (with the host and the vote, as
// every welcome sends them), then the track (as a late joiner is sent it) -
// and the room list to everybody after: the reverse of a player's join,
// which sends the list first. A spectator taking the last spare seat makes
// its own row one that cannot be watched any more, and a client handed that
// list ahead of its SG_NEWCLIENT, in an earlier pass of its loop, would
// give the room list back to its player mid-join (RoomsUnlock_Settle,
// CL_main.cpp).
static void Spectator_Join(RoomInfo* ri, ENetPeer* peer)
{
	int id = SpectateSeat(ri);
	PeerInfo* pi;
	struct SG_MessageTrack st;

	if (id < 0)
	{
		SendRoomData(peer);   // (never: RoomOpenForSpectate found it one)
		return;
	}
	pi = &ri->peerInfos[id];

	// if added to end, then increase count (as a player's join)
	if (id == ri->clientCount)
		ri->clientCount++;

	memset(pi, 0, sizeof(PeerInfo));
	pi->peer = peer;
	pi->spectator = 1;
	pi->spectateAt = IdleNow();
	pi->boolLoadSelf = 1;
	pi->boolRaceSelf = 1;
	pi->boolEndSelf = 1;

	// nobody's vote to kick against the seat: a guard, as at a player's join
	KickVote_Forget(ri, id);

	// 5 seconds, as a player's join
	enet_peer_timeout(peer, 1000000, 1000000, 5000);

	WelcomeNewClient(ri, id, NEWCLIENT_SPECTATOR);

	st.type = SG_TRACK;
	st.voted = ri->trackVoted;
	st.padding = 0;
	st.trackID = ri->levelPlayed;
	st.lapID = ri->lapPlayed;
	sendToPeerReliable(peer, &st, sizeof(struct SG_MessageTrack));

	RoomList_Dirty();   // someone chose a room: everybody's list, after the welcome above (RoomList_Flush)

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %d joined room %d as a spectator\n", id, (int)(ri - roomInfos) + 1);
}

// A spectator's CG_NAME (its client sends it at its SG_NEWCLIENT, with its
// player's name): the name kept for the room's reset, when the racers are
// told it - never now (a racer's ROM takes a name to be a kart) - and the
// race it is to load, unless that race is over already: a name for each
// racer (Spectator_Racer) and their characters, locked in, then the room's
// SG_STARTLOADING with the round's code (roundCode), which its client loads
// from. Over by now (boolEndAll: the last racer finished within the join's
// round trip): nothing more - its ROM stays on the room list until the reset
// welcomes it into the lobby as a player (loadSent stays 0). Once: a later
// CG_NAME from a spectator's seat is ignored (CG_NAME).
static void Spectator_Name(RoomInfo* ri, int id, const struct CG_MessageName* r)
{
	PeerInfo* pi = &ri->peerInfos[id];
	struct SG_MessageName sn;
	struct SG_MessageCharacter sc;
	struct SG_Header sg;
	int racers = 0;

	// (sanitised, see CopyName; an empty one names nobody, as a player's)
	if (pi->name[0] == 0 && r->name[0] != 0)
		CopyName(&pi->name[0], &r->name[0]);

	// "seen in the past 24h", as a player's CG_NAME is
	if (pi->name[0] != 0)
	{
		char nameZ[NAME_LEN + 1];
		memcpy(nameZ, &pi->name[0], NAME_LEN);
		nameZ[NAME_LEN] = '\0';
		Heartbeat_SeenName(pi->peer->data, pi->peer->address.host, nameZ);
	}

	if (!ri->boolEndAll)
	{
		memset(&sn, 0, sizeof(sn));
		sn.type = SG_NAME;
		sn.numClientsTotal = ri->clientCount;
		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			if (!Spectator_Racer(ri, j))
				continue;
			sn.clientID = j;
			memcpy(&sn.name[0], &ri->peerInfos[j].name[0], NAME_LEN);
			sendToPeerReliable(pi->peer, &sn, sizeof(struct SG_MessageName));
			racers++;
		}

		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			if (!Spectator_Racer(ri, j))
				continue;
			memset(&sc, 0, sizeof(sc));
			sc.type = SG_CHARACTER;
			sc.clientID = j;
			sc.characterID = ri->peerInfos[j].characterID;
			sc.boolLockedIn = 1;
			sendToPeerReliable(pi->peer, &sc, sizeof(struct SG_MessageCharacter));
		}

		sg.type = SG_STARTLOADING;
		sg.padding = ri->roundCode & 0xF;
		sendToPeerReliable(pi->peer, &sg, sizeof(struct SG_Header));
		pi->loadSent = 1;
	}

	PrintPrefix((int)(ri - roomInfos) + 1);
	if (ri->boolEndAll)
		printf("Player %d watching room %d is identified as %.*s [%08x]: the race is over, welcomed at the reset\n",
			id, (int)(ri - roomInfos) + 1, NAME_LEN, &pi->name[0], pi->peer->address.host);
	else
		printf("Player %d watching room %d is identified as %.*s [%08x]: sent %d racer%s and the race to load\n",
			id, (int)(ri - roomInfos) + 1, NAME_LEN, &pi->name[0], pi->peer->address.host,
			racers, racers == 1 ? "" : "s");
}

// A spectator's CG_STARTRACE, its race loaded: the race so far, to it alone,
// all reliable on channel 0, in this order - and from then on it is watching
// (spectator 2) and gets every message the room gets. Nothing reached it
// while it was held, and the server is single-threaded, so each thing comes
// exactly once and in order.
//   1. Each racer's last kart message (SG_RACEDATA, lastKart), so every kart
//      starts where it is. Its client takes it as the newest (KartSeq_Newer)
//      and seeds its hurt numbers from its hurt (CL_main.cpp), so no hurt
//      from before the join is played again.
//   2. A round's state (none once roundCode is 0: called off at the green).
//      Each placed player's verdict, with no attacker and no ROUND_LAST; in
//      BOMBARDMENT the lives of each player still in; the round's end if it
//      is over - the bare ROUND_LAST (nobody, no cause), after which the
//      client takes no more of the round - or, TAG still running, who is it
//      and the seconds left on its clock (rounded up, as the clients count).
//   3. Each finish, in the order the room heard them (endOrder), with its
//      ENDRACE_BOT - but never for a finisher who has left since: every
//      racer's screen has their kart braked by now (its kart messages
//      stopped: CL_main.cpp's watchdog), and the spectator brakes it at once.
//   4. The lobby's clock as the racers count it (LOBBYCOUNT_CLOCK, never
//      below 1: the server's own clock is what fires): the results' if the
//      race is over (from endTime - which also covers a round ended with no
//      finish), the lobby-return's if anybody has finished, none otherwise.
//   5. The start (SG_STARTRACE): its ROM goes on from its wait.
// Tiger Temple's door is not retold: an open door is a moment's, and the
// client drops door events before the race anyway; live ones reach a
// spectator from here.
static void Spectator_CatchUp(RoomInfo* ri, int id)
{
	PeerInfo* pi = &ri->peerInfos[id];
	ENetPeer* peer = pi->peer;
	enet_uint32 now = enet_time_get();
	int karts = 0, finishes = 0, clock = 0;
	int j, n;

	// 1. the karts
	for (j = 0; j < MAX_CLIENTS; j++)
		if (Spectator_Racer(ri, j) && ri->peerInfos[j].hasLastKart)
		{
			sendToPeerReliable(peer, &ri->peerInfos[j].lastKart, sizeof(struct SG_EverythingKart));
			karts++;
		}

	// 2. the round
	if (ri->roundCode != 0)
	{
		int row = Round_Row(ri);

		for (j = 0; j < MAX_CLIENTS; j++)
			if (ri->peerInfos[j].place != 0)
				Round_SendTo(peer, NET_EV_OUT, j, ROUND_NONE, ri->peerInfos[j].cause, ri->peerInfos[j].place);

		if (row == ROOM_ROW_BOMBARDMENT)
			for (j = 0; j < MAX_CLIENTS; j++)
				if (Round_IsIn(ri, j))
					Round_SendTo(peer, NET_EV_LIVES, j, ROUND_NONE, 0, ri->peerInfos[j].lives);

		if (ri->roundOver)
			Round_SendTo(peer, NET_EV_OUT, ROUND_NONE, ROUND_NONE, ROUND_LAST, ROUND_NONE);
		else if (row == ROOM_ROW_TAG)
		{
			int none = ri->itID >= MAX_CLIENTS;
			int running = !none && ri->itGreenAt == 0 && ri->itDeadline != 0;
			int secs = none ? 0 : tagSeconds;
			if (running)
			{
				int ms = (int)(ri->itDeadline - now);
				secs = ms <= 0 ? 1 : (ms + 999) / 1000;
			}
			Round_SendTo(peer, NET_EV_IT, none ? ROUND_NONE : ri->itID, ROUND_NONE, running ? 1 : 0, secs);
		}
	}

	// 3. the finishes, in order
	for (n = 1; n <= ri->endCount; n++)
		for (j = 0; j < MAX_CLIENTS; j++)
		{
			PeerInfo* pj = &ri->peerInfos[j];
			struct SG_MessageEndRace se;

			if (pj->endOrder != n)
				continue;
			memset(&se, 0, sizeof(se));
			se.type = SG_ENDRACE;
			se.clientID = j;
			se.flags = (pj->peer != 0) ? (unsigned char)(pj->endFlags & ENDRACE_BOT) : 0;
			se.courseTime = pj->endCourse;
			se.lapTime = pj->endLap;
			sendToPeerReliable(peer, &se, sizeof(struct SG_MessageEndRace));
			finishes++;
		}

	// 4. the lobby's clock
	if (ri->boolEndAll || ri->firstEndTime != 0)
	{
		struct SG_MessageLobbyCountdown sl;

		if (ri->boolEndAll)
			clock = RACE_OVER_COUNTDOWN_SECONDS - (int)((now - ri->endTime) / 1000);
		else
			clock = LOBBY_RETURN_SECONDS - (int)((now - ri->firstEndTime) / 1000);
		if (clock < 1)
			clock = 1;

		sl.type = SG_LOBBYCOUNTDOWN;
		sl.kind = LOBBYCOUNT_CLOCK;
		sl.seconds = (unsigned char)clock;
		sendToPeerReliable(peer, &sl, sizeof(struct SG_MessageLobbyCountdown));
	}

	// 5. the start
	{
		struct SG_Header sg;
		sg.type = SG_STARTRACE;
		sg.padding = 0;
		sendToPeerReliable(peer, &sg, sizeof(struct SG_Header));
	}

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %.*s (%d) loaded and is watching: %d kart%s, %d finish%s%s",
		NAME_LEN, &pi->name[0], id, karts, karts == 1 ? "" : "s", finishes, finishes == 1 ? "" : "es",
		ri->roundCode != 0 ? ", the round" : "");
	if (clock != 0)
		printf(", %ds on the clock", clock);
	printf("\n");
}

// A spectator's CG_STARTRACE when the room reset while it loaded
// (welcomePending, ResetRoomAfterRace): its load is done, so now it can be
// put in the lobby - welcomed as a player (SG_NEWCLIENT with no special, and
// the host and the vote with it), as the reset welcomed everybody else. The
// racers answered their own CG_NAMEs at the reset while it was still a
// spectator, so its name is told to the room here: its own CG_NAME after
// this welcome finds the seat named and tells nobody (CG_NAME), and without
// this the racers would never learn it - nor their ROMs birth a kart for it
// in the next race. Then as a join ends: with the track in, the track and
// its own clock - host or not, everybody is on the character pick then -
// otherwise the host's clock if it is the host by now, or the host's clock
// shown. Its race flags are cleared (they were at the reset), and its
// CG_STARTRACE writes none (CG_STARTRACE): a stale boolRaceSelf would let
// the next race start while it was still in the lobby.
static void Spectator_Welcome(RoomInfo* ri, int id)
{
	PeerInfo* pi = &ri->peerInfos[id];
	ENetPeer* peer = pi->peer;

	pi->spectator = 0;
	pi->welcomePending = 0;
	pi->loadSent = 0;
	pi->spectateAt = 0;
	pi->boolLoadSelf = 0;
	pi->boolRaceSelf = 0;
	pi->boolEndSelf = 0;
	pi->watching = 0;       // (1111: its camera on nobody now - the reset cleared it too)
	pi->watchersTold = 0;
	IdleReset(pi);

	EnsureHost(ri, id);
	WelcomeNewClient(ri, id, 0);

	if (pi->name[0] != 0)
	{
		struct SG_MessageName sn;
		memset(&sn, 0, sizeof(sn));
		sn.type = SG_NAME;
		sn.clientID = id;
		sn.numClientsTotal = ri->clientCount;
		memcpy(&sn.name[0], &pi->name[0], NAME_LEN);
		broadcastToPeersReliable(ri, &sn, sizeof(struct SG_MessageName));
	}

	PrintPrefix((int)(ri - roomInfos) + 1);
	printf("Player %.*s (%d) loaded after the room's reset: welcomed into the lobby as a player\n",
		NAME_LEN, &pi->name[0], id);

	if (ri->boolRoomLocked)
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
	else if (ri->hostID == id)
		ArmHostIdle(ri);
	else if (HasValidHost(ri) && ri->peerInfos[ri->hostID].idleStart != 0)
		SendIdleCountdown(peer, IdleLeftSeconds(&ri->peerInfos[ri->hostID]));
}

// What a spectator's client sends that is dropped: a race's (CG_RACEDATA,
// CG_WEAPON, CG_WEAPONEVENT, CG_ENDRACE - it has no kart in the race) and a
// lobby's (CG_CHARACTER, CG_TRACK, CG_TRACKCANCEL, CG_VOTE, CG_HOSTACTION -
// it is in no lobby until the reset makes it a player). A client of this
// release sends none of them (CL_main.cpp); this is the check that does not
// lean on that. Its CG_NAME, CG_STARTRACE and CG_WATCH are its own
// (Spectator_Name, CG_STARTRACE; Watch_Set, 1111: its camera is the very
// thing a racer's WATCHERS counts).
#define SPECTATOR_DROPS ((1u << CG_RACEDATA) | (1u << CG_WEAPON) | (1u << CG_WEAPONEVENT) | (1u << CG_ENDRACE) | \
	(1u << CG_CHARACTER) | (1u << CG_TRACK) | (1u << CG_TRACKCANCEL) | (1u << CG_VOTE) | (1u << CG_HOSTACTION))

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

// ...and every message, either way, fits the packet ENet is allowed
// (MAX_PACKET_BYTES, SV_config.h): one that outgrows it fails here, not on
// the wire, where ENet would drop it without a word.
STATIC_ASSERT2(sizeof(struct SG_MessageRooms) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageRooms");
STATIC_ASSERT2(sizeof(struct SG_MessageVote) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageVote");
STATIC_ASSERT2(sizeof(struct SG_MessageKickVote) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageKickVote");
STATIC_ASSERT2(sizeof(struct SG_MessageClientStatus) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageClientStatus");
STATIC_ASSERT2(sizeof(struct SG_MessageName) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageName");
STATIC_ASSERT2(sizeof(struct SG_MessageTrack) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageTrack");
STATIC_ASSERT2(sizeof(struct SG_MessageCharacter) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageCharacter");
STATIC_ASSERT2(sizeof(struct SG_EverythingKart) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_EverythingKart");
STATIC_ASSERT2(sizeof(struct SG_MessageWeapon) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageWeapon");
STATIC_ASSERT2(sizeof(struct SG_MessageWeaponEvent) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageWeaponEvent");
STATIC_ASSERT2(sizeof(struct SG_MessageEndRace) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageEndRace");
STATIC_ASSERT2(sizeof(struct SG_MessageLobbyCountdown) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageLobbyCountdown");
STATIC_ASSERT2(sizeof(struct SG_MessageIdleCountdown) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageIdleCountdown");
STATIC_ASSERT2(sizeof(struct SG_MessageHost) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_MessageHost");
STATIC_ASSERT2(sizeof(struct SG_Header) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: SG_Header");
STATIC_ASSERT2(sizeof(struct CG_MessageRoom) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageRoom");
STATIC_ASSERT2(sizeof(struct CG_MessageName) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageName");
STATIC_ASSERT2(sizeof(struct CG_MessageTrack) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageTrack");
STATIC_ASSERT2(sizeof(struct CG_MessageCharacter) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageCharacter");
STATIC_ASSERT2(sizeof(struct CG_EverythingKart) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_EverythingKart");
STATIC_ASSERT2(sizeof(struct CG_MessageWeapon) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageWeapon");
STATIC_ASSERT2(sizeof(struct CG_MessageWeaponEvent) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageWeaponEvent");
STATIC_ASSERT2(sizeof(struct CG_MessageHostAction) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageHostAction");
STATIC_ASSERT2(sizeof(struct CG_MessageVote) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageVote");
STATIC_ASSERT2(sizeof(struct CG_MessageEndRace) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageEndRace");
STATIC_ASSERT2(sizeof(struct CG_MessageWatch) <= MAX_PACKET_BYTES, "MAX_PACKET_BYTES: CG_MessageWatch");

// The client's anti-cheat tripped (CG_CHEAT, 1115: its checks of the game's
// code and ModsC in RAM and of the game's cheat codes, CHEAT_REASON_*). The
// player is removed with DISCONNECT_REASON_CHEAT (KICKED BY ANTI-CHEAT on
// their screen), the address barred for CHEAT_BAN_SECONDS here
// (Auth_BarCheat) and, through the site, on every server
// (Heartbeat_BanPublish). Said in the log with the seat's name when it has
// one. A spectator's or a room-list connection's counts the same.
static void AntiCheat_Trip(ENetPeer* peer, RoomInfo* ri, int peerID, int reason)
{
	union { enet_uint32 ip; unsigned char b[4]; } u;
	static const char* const what[] = { "?", "the game's code", "ModsC", "the game's cheat codes" };
	u.ip = peer->address.host;
	PrintPrefix(peerID == -1 ? -1 : (int)(ri - roomInfos) + 1);
	if (peerID == -1)
		printf("Anti-cheat: a connection from [%d.%d.%d.%d] tripped on %s (%d): removed\n",
			u.b[0], u.b[1], u.b[2], u.b[3], what[reason < 4 ? reason : 0], reason);
	else
		printf("Anti-cheat: %.*s (%d) from [%d.%d.%d.%d] tripped on %s (%d): removed\n",
			NAME_LEN, &ri->peerInfos[peerID].name[0], peerID, u.b[0], u.b[1], u.b[2], u.b[3], what[reason < 4 ? reason : 0], reason);
	Auth_BarCheat(peer->address.host);
	Heartbeat_BanPublish(peer->address.host);
	enet_peer_disconnect(peer, DISCONNECT_REASON_CHEAT);
}

void ProcessReceiveEvent(ENetPeer* peer, ENetPacket* packet) {

	int peerID = -1;

	RoomInfo* ri = &roomInfos[0];

	// Its sender's bucket first (Rate_Take): past it, dropped. Every message
	// counts, a malformed one too - an empty one or one of a type no client
	// sends as the lobby's, any other by its type nibble, short or not - so a
	// flood of malformed ones is cut off as any other is. Only the type
	// nibble is read here, and only from a message with a byte in it.
	if (!Rate_Take(peer, packet->dataLength ? ((struct CG_Header*)packet->data)->type : -1))
		return;
	Guard_Stamp(peer);   // heard from (the room list's silence, Guard_Silent)

	// Length before anything past the type nibble is read. A type the
	// protocol has not got, or a message too short for its type, is nothing
	// a client of this release sends (Suspect, SV_auth.c: a run of them and
	// the connection goes).
	if (packet->dataLength < sizeof(struct CG_Header))
		return;
	if (packet->dataLength < MinPacketSize(((struct CG_Header*)packet->data)->type))
	{
		if (Suspect(peer, "a type the protocol has not got, or too short for its type"))
			enet_peer_disconnect(peer, 0);
		return;
	}

	//identify which client ID this came from
	GetDriverFromRace(peer, &ri, &peerID);

	struct CG_Header* recvBuf = (struct CG_Header*)packet->data;
	char sgBuffer[SG_BUFFER_BYTES];
	memset(sgBuffer, 0, sizeof(sgBuffer));

	// (a CG_CHEAT too, 1115: the client's anti-cheat trips on the room list as
	// in a room)
	if (((struct CG_Header*)recvBuf)->type != CG_JOINROOM && ((struct CG_Header*)recvBuf)->type != CG_CHEAT)
	{
		if (peerID == -1)
			return;
	}

	// a spectator's (1110): nothing of a race or a lobby (SPECTATOR_DROPS)
	if (peerID != -1 && ri->peerInfos[peerID].spectator != 0 &&
		((SPECTATOR_DROPS >> ((struct CG_Header*)recvBuf)->type) & 1))
		return;

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

			// The launcher's signature over this connection's nonce (1115,
			// SV_auth.c): without a good one this is no official client, and
			// nothing of its goes further - the room list back, as for the
			// version, and a few refusals bar the address for a while.
			if (!Auth_Check(peer, r))
			{
				union { enet_uint32 ip; unsigned char b[4]; } u;
				u.ip = peer->address.host;
				PrintPrefix(-1);
				printf("Join refused (unofficial client: bad signature) from [%d.%d.%d.%d]\n", u.b[0], u.b[1], u.b[2], u.b[3]);
				if (Auth_Refused(peer))
					enet_peer_disconnect(peer, 0);
				else
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

			// Voted out of this room a short while ago (KickVote_Kick,
			// KickBan_Add): it shows closed on their list (SendRoomData), but
			// this is the check that cannot be raced. Ahead of the closed
			// room's test (1110), so that it keeps them out of a spectator's
			// seat too: a spectator is the room's player from its reset on.
			if (KickBan_Mask(peer->address.host) & (1u << r->room))
			{
				PrintPrefix(r->room + 1);
				printf("Join refused (kicked from this room by a vote, for %ds)\n", kickBanSeconds);
				SendRoomData(peer);
				return;
			}

			// Avoid duplicate connection (from multiple CG_Room messages).
			// Ahead of the closed room's test too (1110), for a spectator's
			// join as for a player's; it only reads the seats.
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

			// Closed for joining (RoomOpenForJoin): loading, racing, or
			// somebody's pick clock is nearly out. A chosen track does not
			// close it any more. Unless the join asks to watch (1110,
			// JOIN_SPECTATE) and the race in it can be watched
			// (RoomOpenForSpectate): a spectator's seat (Spectator_Join).
			// A JOIN_SPECTATE into a room open again (its race ended between
			// the list and the press) simply joins, below.
			if (!RoomOpenForJoin(ri))
			{
				if ((r->flags & JOIN_SPECTATE) && RoomOpenForSpectate(ri))
				{
					Spectator_Join(ri, peer);
					break;
				}

				// update data, reject player
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


			// === Connection Accepted ===

			// if added to end, then increase count
			if (id == ri->clientCount)
				ri->clientCount++;

			// If a client disconnects, bools are set to 1,
			// so now if you leave and rejoin, this resets to 0
			memset(&ri->peerInfos[id], 0, sizeof(PeerInfo));

			ri->peerInfos[id].peer = peer;

			// nobody's vote to kick against the seat either: a guard, as the
			// last player's went with them (KickVote_Forget)
			KickVote_Forget(ri, id);

			// the joiner's own list at once, ahead of its welcome, as it always
			// was; everybody else's at the tick's end (RoomList_Flush)
			SendRoomData(peer);
			RoomList_Dirty();

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

			WelcomeNewClient(ri, id, 0);

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

			// A seat's name is given once (1110): the vote to kick finds its
			// player by it (CG_HOSTACTION), so one that could be changed at
			// will - a modified client renaming itself every round trip, or
			// blanking it - would slip every vote against it. Once the seat
			// has a name, a CG_NAME keeps it: nothing stored, nothing told to
			// the room, nothing logged. Only the sender is answered, with the
			// room's names (and, on the character pick, its picks): the
			// client sends its CG_NAME at every SG_NEWCLIENT, which is the
			// join and again each time the room comes back from a race
			// (ResetRoomAfterRace), and its handler has emptied its names
			// both times - this answer is what refills them. Its name is the
			// same each time (one per client run), so nothing a real client
			// does is lost. The join's memset empties the seat for whoever
			// sits down next. An empty name names nobody: refused, the seat
			// left unnamed (no name yet, as before any CG_NAME).
			int named = ri->peerInfos[peerID].name[0] != 0;

			// A spectator's (1110), ahead of the empty name's refusal: it is
			// answered with the race to load (Spectator_Name), a name in it
			// or none. Once that has gone (loadSent), or from a seat whose
			// welcome is pending (it loads still), one is ignored. Its name
			// reaches the racers only once it is a player (the room's reset,
			// Spectator_Welcome).
			if (ri->peerInfos[peerID].spectator != 0)
			{
				PeerInfo* pn = &ri->peerInfos[peerID];
				if (pn->spectator == 1 && !pn->welcomePending && !pn->loadSent)
					Spectator_Name(ri, peerID, r);
				break;
			}

			if (!named && r->name[0] == 0)
				break;

			s->numClientsTotal = ri->clientCount;

			// save new name (sanitised, see CopyName)
			if (!named)
				CopyName(&ri->peerInfos[peerID].name[0], &r->name[0]);

			// "seen in the past 24h": now address + name, so two players
			// behind one address are two (SV_heartbeat.h). Again with each
			// later CG_NAME (the seat's own name, kept): a player still here
			// after a race is seen still.
			{
				char nameZ[NAME_LEN + 1];
				memcpy(nameZ, &ri->peerInfos[peerID].name[0], NAME_LEN);
				nameZ[NAME_LEN] = '\0';
				Heartbeat_SeenName(peer->data, peer->address.host, nameZ);
			}

			int roomId = (int)(ri - roomInfos) + 1;

			if (!named)
			{
				PrintPrefix(roomId);
				printf("Player %d joined room %d and is now identified as %.*s [%08x]\n",
					peerID,
					roomId,
					NAME_LEN, &ri->peerInfos[peerID].name[0],
					peer->address.host);
			}

			s->type = SG_NAME;

			// send all OTHER (8) names to THIS (1) client
			// (never a spectator's, 1110: a racer's ROM takes a name to be a
			// kart, and a spectator is named to the room only as a player -
			// the reset's answers, Spectator_Welcome)
			for (int j = 0; j < MAX_CLIENTS; j++)
			{
				if (
						// skip empty sockets, skip self
						(ri->peerInfos[j].peer != 0) &&
						(peerID != j) &&
						(ri->peerInfos[j].spectator == 0)
					)
				{
					// send all OTHER (8) names to THIS (1) client
					s->clientID = j;
					memcpy(&s->name[0], &ri->peerInfos[j].name[0], NAME_LEN);
					sendToPeerReliable(ri->peerInfos[peerID].peer, s, sizeof(struct SG_MessageName));
				}
			}

			// send THIS (1) name to all OTHER (8) clients - the first time
			// only: they hold it from then on, and after a race each of them
			// is sent it again in the answer to its own CG_NAME (above)
			if (!named)
			{
				s->clientID = peerID;
				memcpy(&s->name[0], &ri->peerInfos[peerID].name[0], NAME_LEN);
				broadcastToPeersReliable(ri, s, sizeof(struct SG_MessageName));
			}

			// A late joiner (the track is in, the room is on the character
			// pick) is also told everybody's character and lock, so the
			// player list is right from the first frame. (Not a spectator's
			// seat's, 1110, as for the names.)
			if (ri->boolRoomLocked)
			{
				for (int j = 0; j < MAX_CLIENTS; j++)
				{
					if (ri->peerInfos[j].peer == 0 || j == peerID || ri->peerInfos[j].spectator != 0)
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

				// Every room races the race tracks only - the ROM's list and
				// the vote offer nothing else (NUM_TRACKS, menu.c; VOTE_TRACKS)
				// - but trackID is five bits, so a modified client could ask
				// for an arena or a hub level. In a mode room a round would
				// meet battle rules (the invincibility after a blast), no laps,
				// and ProcessDisconnectEvent's arena kill; in any room it is a
				// level nobody picked, loaded on every machine in the room.
				// Clamped rather than refused, for the reason above: a refusal
				// strands the host. (Mode rooms alone up to 1111.)
				if (trackID >= VOTE_TRACKS)
				{
					trackID = VOTE_TRACKS - 1;
					if (Suspect(peer, "a track past the race tracks")) { enet_peer_disconnect(peer, 0); break; }
				}

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
			RoomList_Dirty();
			break;
		}

		case CG_HOSTACTION:
		{
			// The lobby's OPTIONS (hostOptions.c): anybody's VOTE TO KICK
			// against another player still in the room (KickVote_*), and the
			// host's CHANGE HOST at one and PLAYER VOTING for the room. Not
			// once the race is loading (SG_STARTLOADING closed the ROM's
			// OPTIONS too). The host's instant KICK PLAYER (2, 1101-1109) is
			// gone: refused below, as any action the host's check does not
			// know.
			struct CG_MessageHostAction* r = (struct CG_MessageHostAction*)recvBuf;
			int target = r->clientID;
			ENetPeer* tp = (target < MAX_CLIENTS) ? ri->peerInfos[target].peer : 0;

			if (r->action != HOST_ACTION_KICKVOTE && r->action != HOST_ACTION_KICKUNVOTE &&
				r->action != HOST_ACTION_VOTING && r->action != HOST_ACTION_GIVE)
			{
				if (Suspect(peer, "a host action the protocol has not got")) { enet_peer_disconnect(peer, 0); break; }
			}

			// A vote to kick, cast or taken back, from any seat. Refused
			// (KICKVOTE_REFUSED) once the room is loading (a vote crossing
			// SG_STARTLOADING), from a player on the way out or one with no
			// name yet (KickVote_Player: not a player the vote counts, 1110),
			// in a room with no vote to kick (one or two players, 1110 - or,
			// were the table ever so, a count the others could never reach:
			// the ROM greys VOTE TO KICK for both),
			// against oneself, and at a seat that no longer holds the player
			// picked - by name, as for CHANGE HOST below (a name stays once
			// given, CG_NAME, so a player cannot step out from under the
			// votes against them by renaming). Answered either way, to the
			// voter alone: their votes as they stand, for their ROM to show
			// (the voter told first, then the kick a cast may bring about).
			// The room hears nothing of it, and the log nothing unless
			// --kicklog asks.
			if (r->action == HOST_ACTION_KICKVOTE || r->action == HOST_ACTION_KICKUNVOTE)
			{
				PeerInfo* vi = &ri->peerInfos[peerID];
				int players;
				int need = KickVote_Needed(ri, &players);
				int result;

				// (1110: nor at a spectator's seat - one whose welcome is
				// pending, the room reset while it loaded - which is nobody the
				// vote counts, KickVote_Player, and so could never be voted
				// out: its vote would stand on nothing)
				if (ri->boolLoadAll || !KickVote_Player(vi) ||
					need == 0 || need > players - 1 ||
					target == peerID || tp == 0 || tp->state != ENET_PEER_STATE_CONNECTED ||
					ri->peerInfos[target].spectator != 0 ||
					r->name[0] == 0 || memcmp(r->name, &ri->peerInfos[target].name[0], NAME_LEN) != 0)
					result = KICKVOTE_REFUSED;
				else if (r->action == HOST_ACTION_KICKVOTE)
				{
					vi->kickVotes |= (unsigned short)(1u << target);
					result = KICKVOTE_CAST;
				}
				else
				{
					vi->kickVotes &= (unsigned short)~(1u << target);
					result = KICKVOTE_REMOVED;
				}

				// (for testing only: this says who votes against whom)
				if (kickVoteLog)
				{
					PrintPrefix((int)(ri - roomInfos) + 1);
					if (result == KICKVOTE_REFUSED)
						printf("Kick vote (--kicklog): %.*s (%d) %s seat %d refused\n",
							NAME_LEN, &vi->name[0], peerID,
							r->action == HOST_ACTION_KICKVOTE ? "voting against" : "taking back a vote against", target);
					else
						printf("Kick vote (--kicklog): %.*s (%d) %s %.*s (%d), %d of %d needed (%d players)\n",
							NAME_LEN, &vi->name[0], peerID,
							result == KICKVOTE_CAST ? "votes against" : "takes back their vote against",
							NAME_LEN, &ri->peerInfos[target].name[0], target,
							KickVote_Count(ri, target), need, players);
				}

				KickVote_Reply(peer, target, result, vi->kickVotes);
				if (result == KICKVOTE_CAST)
					KickVote_Check(ri);
				break;
			}

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

			// CHANGE HOST, the one action left at a player that is the host's
			// alone (KICK PLAYER's 2 is refused here, with anything else). The
			// seat must still hold the player the host picked, by name (the
			// one the server keeps and sends out, CopyName and SG_NAME, so the
			// two are byte for byte the same): a seat let go and taken again
			// before this arrived holds somebody else, and a newcomer whose
			// name is not in yet holds zeros. Nor a spectator's seat (1110):
			// one whose welcome is pending is in no lobby yet.
			if (peerID != ri->hostID || ri->boolLoadAll ||
				target == peerID || tp == 0 || tp->state != ENET_PEER_STATE_CONNECTED ||
				ri->peerInfos[target].spectator != 0 ||
				r->action != HOST_ACTION_GIVE ||
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

			// The room is the player's: everybody is told (SG_HOST), and the
			// old host's ROM goes back to the wait for the track
			// (hostOptions.c), the new one's on to the track pick
			// (StatePS1_Lobby_GuestTrackWait). Before the track is in, the
			// clock that runs is the host's: the old one's stops, keeping what
			// is left, and the new one's starts.
			if (!ri->boolRoomLocked)
				IdlePause(&ri->peerInfos[peerID]);
			ri->hostID = (unsigned char)target;
			SendHost(ri);

			PrintPrefix((int)(ri - roomInfos) + 1);
			printf("Host %.*s (%d) handed the room to %.*s (%d)\n",
				NAME_LEN, &ri->peerInfos[peerID].name[0], peerID,
				NAME_LEN, &ri->peerInfos[target].name[0], target);

			ArmHostIdle(ri);
			break;
		}

		case CG_VOTE:
		{
			// A player's vote, or a change of it, in the vote running
			// (Vote_*); the room shown it at once. The close is the tick's.
			struct CG_MessageVote* r = (struct CG_MessageVote*)recvBuf;
			int options = ri->votePhase == VOTE_TRACK ? VOTE_TRACKS : VOTE_LAP_ROWS;

			if (ri->votePhase != VOTE_NONE && r->phase == ri->votePhase && r->option >= options)
			{
				if (Suspect(peer, "a vote for an option the vote has not got")) enet_peer_disconnect(peer, 0);
				break;
			}
			if (ri->votePhase == VOTE_NONE || r->phase != ri->votePhase || r->option >= options)
				break;
			ri->peerInfos[peerID].vote = (unsigned char)(r->option + 1);
			Vote_Broadcast(ri);
			break;
		}

		case CG_CHEAT:
		{
			AntiCheat_Trip(peer, ri, peerID, ((struct CG_MessageCheat*)recvBuf)->reason);
			return;
		}

		case CG_WATCH:
		{
			// The kart this player's camera follows now (1111, Watch_Set): a
			// spectator's, or a player's over the line or out of a round - the
			// racers it leaves and lands on told their counts. A peer in no
			// room never gets here (above).
			struct CG_MessageWatch* r = (struct CG_MessageWatch*)recvBuf;
			Watch_Set(ri, peerID, r->target);
			break;
		}

		case CG_STARTRACE:
		{
			PeerInfo* pi = &ri->peerInfos[peerID];

			#if 0
			printf("Ready to Race: %d\n", peerID);
			#endif

			// A spectator's (1110): its race is loaded. The room reset while
			// it loaded: welcomed into the lobby now (Spectator_Welcome).
			// Otherwise caught up and watching (Spectator_CatchUp), and the
			// racer its camera is on (a CG_WATCH stored while it was held)
			// told it counts now (Watch_Tell, 1111) - after the catch-up's
			// private SG_STARTRACE, as 1110's count was. Either way it writes
			// no boolRaceSelf - and first, ahead of the lobby's test below: a
			// welcome's CG_STARTRACE always comes once the reset has cleared
			// boolLoadAll, and its boolRaceSelf, never cleared at the next
			// SG_STARTLOADING, would start that race while it still loaded.
			if (pi->spectator != 0)
			{
				if (pi->spectator == 1)
				{
					if (pi->welcomePending)
						Spectator_Welcome(ri, peerID);
					else if (ri->boolRaceAll && pi->loadSent)
					{
						Spectator_CatchUp(ri, peerID);
						pi->spectator = 2;
						Watch_Tell(ri, (int)pi->watching - 1);
					}
				}
				break;
			}

			// (1110) a late one, in the lobby, must not count for the next
			// race: a race's start needs every player's from its own load
			if (!ri->boolLoadAll)
				break;

			pi->boolRaceSelf = 1;
			break;
		}

		case CG_RACEDATA:
		{
			struct SG_EverythingKart* s = (struct SG_EverythingKart*)&sgBuffer[0];
			struct CG_EverythingKart* r = (struct CG_EverythingKart*)recvBuf;

			// a race's message: from the room's load (SG_STARTLOADING) to its
			// reset. In the lobby there is no kart to move, and a client that
			// sent them there could flood the room's other players through the
			// relay below (unreliable, a copy to each). The client sends them
			// in its race states alone (SendKartMessage, CL_main.cpp).
			if (!ri->boolLoadAll) break;

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

			// Saffi's own blue (1110): its squishes may be claimed
			// (KartClaim_Relay)
			if (r->flame == FLAME_SAFFI) ri->peerInfos[peerID].saffiSeenAt = enet_time_get() | 1;

			// its last hurt (NET_HURT_*, 1103): the others hurt their copy of
			// the kart with this message's position
			s->hurt = r->hurt;

			// its lap, behind-the-line flag and progress as its own screen has
			// them (NET_LAP_*, 1104), and the message's number: the others rank
			// their copy of the kart by the newest (netLap.c; the client's
			// KartSeq_Newer). As they came: the sender's word, as its position is.
			memcpy(s->lap, r->lap, sizeof(s->lap));
			s->seq = r->seq;

			// kept, as the room gets it, for a spectator's catch-up (1110,
			// Spectator_CatchUp): the kart where it is now - the newest to
			// arrive, a frame's at most from the newest sent
			memcpy(&ri->peerInfos[peerID].lastKart, s, sizeof(struct SG_EverythingKart));
			ri->peerInfos[peerID].hasLastKart = 1;

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

			// a mask (item 7): its bumps may be claimed (KartClaim_Relay); a
			// shield (item 6), new (not one let go: the same count), one bump;
			// a TURBO (item 0, 1110): its squishes
			if (r->weapon == 7) ri->peerInfos[peerID].maskShotAt = enet_time_get() | 1;
			if (r->weapon == 0) ri->peerInfos[peerID].turboShotAt = enet_time_get() | 1;
			if (r->weapon == 6)
			{
				PeerInfo* pw = &ri->peerInfos[peerID];
				pw->shieldClaim = (r->serial != pw->shieldSerial);   // the same count again: let go
				pw->shieldSerial = r->serial;
			}

			s->type = SG_WEAPON;
			s->clientID = peerID;

			s->weapon = r->weapon;
			s->juiced = r->juiced;
			s->flags = r->flags;

			// the shooter's pose and shot count, through as they came: every
			// machine spawns the weapon there and knows it by them
			s->held = r->held;
			s->serial = r->serial;
			if (r->target > MAX_CLIENTS && Suspect(peer, "a weapon's target past the seats")) { enet_peer_disconnect(peer, 0); break; }
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
			if (kind >= NET_EV_COUNT)
			{
				if (Suspect(peer, "a weapon event the protocol has not got")) enet_peer_disconnect(peer, 0);
				break;
			}

			// the referee's verdicts (NET_EV_OUT, NET_EV_IT) are the server's
			// to give: one from a client would put a player out, or make one
			// "it", on every screen - and the server's ping (NET_EV_PING)
			if ((NET_EV_SERVER_MASK >> kind) & 1)
			{
				if (Suspect(peer, "a weapon event only the server sends")) enet_peer_disconnect(peer, 0);
				break;
			}

			// ELIMINATION's lap reports: for the referee alone (Elim_OnLap,
			// which ignores them in any other room), relayed to nobody
			if (kind == NET_EV_LAP)
			{
				Elim_OnLap(ri, peerID, r->arg);
				break;
			}

			// a masked kart's or a shield's bump, or a squish: to the bumped kart's machine alone
			if (kind == NET_EV_KART_CLAIM)
			{
				KartClaim_Relay(ri, peer, peerID, r);
				break;
			}

			if (((NET_EV_NOID_MASK >> kind) & 1) == 0)
			{
				// 4 bits on the wire, fewer seats: past them would index weaponFate
				// into the next room
				if (r->owner >= MAX_CLIENTS)
				{
					if (Suspect(peer, "a weapon event's owner past the seats")) enet_peer_disconnect(peer, 0);
					break;
				}
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

			// ENDRACE_BOT (1110): this player's kart drives on as the bot past
			// the line, and its kart messages go on - passed on as it came, so
			// no machine holds a brake on that copy (CL_main.cpp). Set here or
			// never: sgBuffer is zeroed for each message. And the finish kept,
			// in the order the room hears them, for a spectator's catch-up
			// (Spectator_CatchUp) - the first one only, should a client send
			// two.
			s->flags = (unsigned char)(r->padding & ENDRACE_BOT);
			{
				PeerInfo* pe = &ri->peerInfos[peerID];
				if (pe->endOrder == 0)
				{
					pe->endOrder = ++ri->endCount;
					pe->endFlags = s->flags;
					memcpy(&pe->endCourse, &r->courseTime, sizeof(r->courseTime));
					memcpy(&pe->endLap, &r->lapTime, sizeof(r->lapTime));
				}
			}

			int localTime = 0;
			memcpy(&localTime, &r->courseTime, sizeof(r->courseTime));
			if (localTime <= 0 || localTime > 0xe100 * 600)
			{
				if (Suspect(peer, "a finish time no race has")) enet_peer_disconnect(peer, 0);
			}

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
				sg.kind    = LOBBYCOUNT_CLOCK;
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

				// a new connection in this slot: its buckets full again
				// (Rate_*), whatever becomes of it below, and its nonce for
				// the signed join (1115, SV_auth.c)
				Rate_Reset(event.peer);
				Auth_Connect(event.peer);

				// a connection flood (Guard_Connect, SV_auth.c): too many from
				// this address this minute (barred), or too many from everywhere
				// this second (refused)
				int flood = Guard_Connect(ip);
				if (flood)
				{
					if (flood == 1)
					{
						PrintPrefix(-1);
						printf("Too many connections from IP [%d.%d.%d.%d] this minute (limit %d), rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3], CONNECT_PER_IP_PER_MINUTE);
					}
					enet_peer_disconnect_now(event.peer, 0);
				}
				else if (AnyMatchIPBanL(ip)) //if this IP is banned
				{
					PrintPrefix(-1);
					printf("Banned player @ IP [%d.%d.%d.%d] attempted to connect, rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3]);
					enet_peer_disconnect_now(event.peer, 0);
				}
				// barred for its joins' signatures or its messages a short
				// while ago (Auth_Barred, SV_auth.c)
				else if (Auth_Barred(ip))
				{
					PrintPrefix(-1);
					printf("Barred address [%d.%d.%d.%d] attempted to connect, rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3]);
					enet_peer_disconnect_now(event.peer, 0);
				}
				// barred for cheating on another server, by the site's list
				// (Heartbeat_Banned, 1115)
				else if (Heartbeat_Banned(ip))
				{
					PrintPrefix(-1);
					printf("Anti-cheat banned address [%d.%d.%d.%d] (from the site's list) attempted to connect, rejected\n", ipBytes.bytes[0], ipBytes.bytes[1], ipBytes.bytes[2], ipBytes.bytes[3]);
					enet_peer_disconnect_now(event.peer, 0);
				}
				// One address cannot hold the whole server: the host has
				// MAX_PEERS slots, and a single client that opened connections
				// and kept them alive could sit on all of them, room or no
				// room. MAX_PEERS_PER_IP (12, SV_config.h) is a full room for a
				// household or a LAN behind one NAT. This is the check that
				// refuses a connection over it, at once, with the log line;
				// ENet's own cap (duplicatePeers, one above, set at start-up)
				// only stops any further half-open handshakes from the address.
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

			case ENET_EVENT_TYPE_NONE:
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

// The banlist's address: four numbers of 0 to 255, one to three digits each,
// separated by dots - no signs, no spaces inside. The bytes in out, in the
// order written (network order, as ENetAddress.host keeps them); *end just
// past the last digit. 1 for an address, 0 for anything else.
static int BanList_ParseIPv4(const char* s, unsigned char out[4], const char** end)
{
	for (int i = 0; i < 4; i++)
	{
		int v = 0, digits = 0;
		if (i > 0)
		{
			if (*s != '.')
				return 0;
			s++;
		}
		while (*s >= '0' && *s <= '9')
		{
			v = v * 10 + (*s - '0');
			if (++digits > 3)
				return 0;
			s++;
		}
		if (digits == 0 || v > 255)
			return 0;
		out[i] = (unsigned char)v;
	}
	*end = s;
	return 1;
}

// What the rest of a banlist line is, from s: 1 nothing (spaces, tabs, the
// '\r' of a CRLF file), 2 a '#' note, 0 anything else - a range's "/8", a
// fifth number, a port - which makes the line no address at all.
static int BanList_Rest(const char* s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r')
		s++;
	if (*s == '\0')
		return 1;
	return *s == '#' ? 2 : 0;
}

void ServerState_FirstBoot(int argc, char** argv)
{
	PrintServerBanner();

#ifdef _WIN32
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
		else if (strcmp(argv[i], "--botkick") == 0 && i + 1 < argc)
		{
			// the seat (client ID) every bot votes to kick, in every lobby
			// (SV_backfill.h); out of range, none
			botKick = atoi(argv[i + 1]);
			if (botKick < 0 || botKick >= MAX_CLIENTS) botKick = -1;
			i++;
		}
		else if (strcmp(argv[i], "--botspectate") == 0 && i + 1 < argc)
		{
			// the last N bots join only to watch a race in progress (1110,
			// SV_backfill.h)
			botSpectate = atoi(argv[i + 1]);
			if (botSpectate < 0) botSpectate = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botspecname") == 0 && i + 1 < argc)
		{
			// seconds a spectator bot waits with its CG_NAME after it sits
			// down (1110: a race over between a join and its name)
			botSpecNameSecs = atoi(argv[i + 1]);
			if (botSpecNameSecs < 0) botSpecNameSecs = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botspecload") == 0 && i + 1 < argc)
		{
			// seconds a spectator bot "loads" (1110: its CG_STARTRACE after
			// its SG_STARTLOADING; a room reset during the load)
			botSpecLoadSecs = atoi(argv[i + 1]);
			if (botSpecLoadSecs < 0) botSpecLoadSecs = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botleave") == 0 && i + 1 < argc)
		{
			// seconds after its finish (--botfinish) or its catch-up (a
			// spectator bot) that a bot leaves (1110); 0 never
			botLeaveSecs = atoi(argv[i + 1]);
			if (botLeaveSecs < 0) botLeaveSecs = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botfinish") == 0 && i + 1 < argc)
		{
			// in a plain race, the first N racing bots cross the line, one
			// after another (1110: a 9th finisher's bot)
			botFinish = atoi(argv[i + 1]);
			if (botFinish < 0) botFinish = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botfinishat") == 0 && i + 1 < argc)
		{
			// seconds after the green that the first of them does (1110)
			botFinishAtSecs = atoi(argv[i + 1]);
			if (botFinishAtSecs < 0) botFinishAtSecs = 0;
			i++;
		}
		else if (strcmp(argv[i], "--botfinishbot") == 0)
		{
			// their CG_ENDRACE says ENDRACE_BOT, as a finisher's whose kart
			// the bot drives on (1110)
			botFinishBot = 1;
		}
		else if (strcmp(argv[i], "--botwatch") == 0 && i + 1 < argc)
		{
			// the target a spectator bot (from its catch-up, or from its
			// load with --botspecload) and a --botfinish bot (after its
			// CG_ENDRACE) report their camera on, CG_WATCH (1111): a client
			// ID, or 15 for nobody (WATCH_NOBODY); out of 0-15, none sent
			botWatch = atoi(argv[i + 1]);
			if (botWatch < 0 || botWatch > WATCH_NOBODY) botWatch = -1;
			i++;
		}
		else if (strcmp(argv[i], "--botload") == 0 && i + 1 < argc)
		{
			// seconds a racing bot "loads" (1111: its CG_STARTRACE after
			// SG_STARTLOADING), holding the room at the start line; 0 as
			// ever, 3-5 s
			botLoadSecs = atoi(argv[i + 1]);
			if (botLoadSecs < 0) botLoadSecs = 0;
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
		else if (strcmp(argv[i], "--kicklog") == 0)
		{
			// every vote to kick in the log, who against whom (KickVote_*):
			// for testing only - the votes are secret, and a server's console
			// is read by whoever runs it, who may be playing in the room
			kickVoteLog = 1;
		}
		else if (strcmp(argv[i], "--watchlog") == 0)
		{
			// every camera's report in the log, who watches whom (1111,
			// Watch_Set: "now watching"): for testing only, as --kicklog -
			// the counts the racers are told ("is watched by") are logged
			// without it
			watchLog = 1;
		}
		else if (strcmp(argv[i], "--nospectate") == 0)
		{
			// no race in progress can be watched (1110, SPECTATE_ENABLED,
			// RoomOpenForSpectate): no room offered as one to watch, a join
			// that asks refused as a closed room's
			spectateEnabled = 0;
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

	// Read the banlist file: one IPv4 address a line. Blank lines and lines
	// starting with '#' are ignored, and so is a '#' note after an address.
	// Anything else is skipped with its line number in the log, never banned
	// as something it is not: "10.0" used to ban 10.0.0.0, "300.1.1.1" wrapped
	// to 44.1.1.1, and a range such as "10.0.0.0/8" would ban one address
	// while looking like a whole network. Read a byte at a time, so that one
	// line of the file is always one entry and one line number, however long
	// it is and whatever is in it: the first 255 bytes are kept, the rest only
	// looked at for text after an address, and a line holding a NUL byte is no
	// address.
	if (boolIsBanlistArgument)
	{
		FILE* fBanlistPtr = fopen(banlistPath, "r");
		if (fBanlistPtr != NULL)
		{
			char lineBuf[256];
			unsigned int lineNum = 1 /*1 based*/, banEntries = 0;
			for (;;)
			{
				union
				{
					unsigned int ip;
					unsigned char bytes[4];
				} ipBytes;
				const char* p = lineBuf;
				const char* end = NULL;
				size_t len = 0;
				int c, any = 0, hasNul = 0;
				int tailNote = 0, tailText = 0;   // past the kept bytes: a '#' note began, other text came first
				int rest;

				while ((c = getc(fBanlistPtr)) != EOF)
				{
					any = 1;
					if (c == '\n')
						break;
					if (c == '\0')
						hasNul = 1;
					if (len < sizeof(lineBuf) - 1)
						lineBuf[len++] = (char)c;
					else if (!tailNote && !tailText)
					{
						if (c == '#')
							tailNote = 1;
						else if (c != ' ' && c != '\t' && c != '\r')
							tailText = 1;
					}
				}
				if (!any)
					break;
				lineBuf[len] = '\0';

				if (lineNum == 1 && len >= 2 &&
					(memcmp(lineBuf, "\xFF\xFE", 2) == 0 || memcmp(lineBuf, "\xFE\xFF", 2) == 0))
				{
					// UTF-16 (Notepad's "Unicode", PowerShell 5's '>'): every
					// line would read as junk; said once, and nothing banned
					printf("Banlist: '%s' is saved as UTF-16; save it as plain text or UTF-8\n", banlistPath);
					break;
				}

				// a file saved as "UTF-8 with BOM" (Notepad, on older Windows)
				if (lineNum == 1 && len >= 3 && memcmp(lineBuf, "\xEF\xBB\xBF", 3) == 0)
					p += 3;
				while (*p == ' ' || *p == '\t')
					p++;

				if (hasNul)
					printf("Banlist: line %u of '%s' is not one IPv4 address, skipped\n", lineNum, banlistPath);
				else if (*p == '#' || (BanList_Rest(p) == 1 && !tailText))
					;   // a comment, or a blank line
				else if (!BanList_ParseIPv4(p, ipBytes.bytes, &end) ||
					(rest = BanList_Rest(end)) == 0 || (rest == 1 && tailText))
					printf("Banlist: line %u of '%s' is not one IPv4 address, skipped\n", lineNum, banlistPath);
				else
				{
					AddIPBanL(ipBytes.ip);
					banEntries++;
				}
				lineNum++;
			}
			fclose(fBanlistPtr);
			printf("Banlist: %u entries from '%s'\n", banEntries, banlistPath);
		}
		else
			printf("Banlist: unable to open '%s'\n", banlistPath);
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
			MAX_PEERS	/* every seat in the 16 rooms, and the rest on the room list (SV_config.h) */,
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

	// What ENet may hold for one connection (SV_config.h). Its defaults are
	// 32 MB each, sized for file transfers, and a fragmented packet is set
	// aside in full at its first fragment: with them, any connection could
	// have had the server allocate 32 MB for packets this game never sends.
	// The largest message is 18 bytes (the asserts beside SG_BUFFER_BYTES).
	server->maximumPacketSize = MAX_PACKET_BYTES;
	server->maximumWaitingData = MAX_WAITING_BYTES;

	// Connections from one address in any state, half-open handshakes
	// included, which the CONNECT event's own count (CountPeersLPL_FromHost)
	// never sees: ENet refuses past this at a handshake's first packet. One
	// above MAX_PEERS_PER_IP, so a connection over the limit still completes
	// and meets the CONNECT check, which refuses it at once with a log line -
	// at MAX_PEERS_PER_IP itself ENet would refuse it silently, and the player
	// would wait out their connect attempt instead of being told.
	server->duplicatePeers = MAX_PEERS_PER_IP + 1;

	PrintPrefix(-1);
	printf("Ready on port %d\n", port);

	// (1110) said, as it differs from every other server's
	if (!spectateEnabled)
	{
		PrintPrefix(-1);
		printf("Races in progress cannot be watched (--nospectate)\n");
	}

#if BACKFILL_PLAYERS > 0
	Backfill_Init(port);
#endif

	// The signed join (1115): the mode said, the nonces seeded (SV_auth.c)
	Auth_Init();

	// Outbound status heartbeat. No-op unless OCTR_HEARTBEAT_URL and
	// OCTR_HEARTBEAT_SECRET are both set, and compiled out entirely when the
	// build has no libcurl.
	Heartbeat_Init();
}

// Snapshot the room occupancy for the heartbeat and hand it to the worker
// thread. Rate limited to roughly once a second: the caller runs in a
// SleepMicro(1) loop, so publishing unconditionally would take the lock millions
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
	// The spectators first (1110), so that the host's settle below can pick
	// a player among them. One watching is a player from here, and so is
	// one still on the room list (its race ended between its join and its
	// CG_NAME: loadSent 0) - both welcomed with the rest below, which takes
	// their clients into the lobby, and every player's CG_NAME after it is
	// answered with their names now (CG_NAME). One still loading the race
	// is not welcomed yet (welcomePending): an SG_NEWCLIENT in the middle of
	// its load would have its ROM's race start overwrite the lobby state
	// (OnlineInit_Drivers: the lost-start class) - it is welcomed at its own
	// CG_STARTRACE (Spectator_Welcome). Seats emptied during the race are
	// cleared of it too.
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		PeerInfo* pi = &ri->peerInfos[i];
		if (pi->peer == 0 || pi->spectator == 2 || (pi->spectator == 1 && !pi->loadSent))
		{
			pi->spectator = 0;
			pi->welcomePending = 0;
			pi->loadSent = 0;
			pi->spectateAt = 0;
		}
		else if (pi->spectator == 1)
			pi->welcomePending = 1;
	}

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

		// (a spectator still loading, 1110: welcomed at its own CG_STARTRACE,
		// Spectator_Welcome - till then the next race cannot load without
		// it, its boolLoadSelf 0, and it is in no lobby count or clock)
		if (ri->peerInfos[i].welcomePending)
			continue;

		// tell all clients to reset
		// (also re-announces the host, which SG_NEWCLIENT clears)
		WelcomeNewClient(ri, i, 0);
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
	ri->raceGreenAt = 0;   // (1111: no race to watch until the next one's green)

	// No game mode's round any more (Round_*): nothing judged, no clock, and
	// every seat's place gone (Round_Begin clears them again at the next
	// load; the leavers' seats too). No vote to kick either: the countdown
	// wiped them and none is taken since (KickVote_*) - a guard.
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
	ri->roundCode = 0;   // (1110: no race to tell a spectator of, nor seats racing)
	ri->raceSeats = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		ri->peerInfos[i].place = 0;
		ri->peerInfos[i].cause = 0;
		ri->peerInfos[i].lapsDone = 0;
		ri->peerInfos[i].kickVotes = 0;

		// (1111) nobody watching anybody, and nobody told a count: every
		// client cleared its WATCHERS at the SG_NEWCLIENT above, and a count
		// of 1 again next race would otherwise equal watchersTold and go
		// untold. Nothing sent.
		ri->peerInfos[i].watching = 0;
		ri->peerInfos[i].watchersTold = 0;
	}

	// Back in the lobby with fresh budgets; the host's clock starts (not a
	// host whose welcome is pending, 1110: ArmHostIdle, Spectator_Welcome).
	ArmHostIdle(ri);

	// The list as it goes out is the room's as the tick knows it (1111,
	// RoomList_Changed: open again, no race); the push itself is the tick's,
	// once for everything that changed in its pass (RoomList_Flush).
	RoomList_Changed(ri);
	RoomList_Dirty();   // race over: everybody's list
}

// (RoomList_SilentSweep's walk: ForeachPeerLPL takes a plain function)
static void RoomList_SilentOne(ENetPeer* peer)
{
	RoomInfo* ri = &roomInfos[0];
	int peerID = -1;
	if (peer == 0 || peer->state != ENET_PEER_STATE_CONNECTED) return;
	GetDriverFromRace(peer, &ri, &peerID);
	if (peerID != -1 || !Guard_Silent(peer)) return;
	{
		union { enet_uint32 ip; unsigned char b[4]; } u;
		u.ip = peer->address.host;
		PrintPrefix(-1);
		printf("Room list connection from [%d.%d.%d.%d] silent for %ds, disconnected\n", u.b[0], u.b[1], u.b[2], u.b[3], ROOMLIST_SILENT_SECONDS);
	}
	enet_peer_disconnect(peer, 0);   // (deferred: the list is not changed under the walk)
}

static void RoomList_SilentSweep(void)
{
	static enet_uint32 lastAt = 0;
	enet_uint32 now = enet_time_get();
	if (lastAt != 0 && (enet_uint32)(now - lastAt) < 1000) return;
	lastAt = now ? now : 1;
	ForeachPeerLPL(RoomList_SilentOne);
}

void ServerState_Tick()
{
	ProcessNewMessages();

	Heartbeat_Sample();

	// a kick's time up: that room open to its player again on their list
	if (KickBan_Expire())
		RoomList_Dirty();

	// a connection on the room list that has sent nothing for
	// ROOMLIST_SILENT_SECONDS: disconnected (a bot holding a slot; the client
	// asks for the list every 5 s). Once a second.
	RoomList_SilentSweep();

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

		// a vote to kick at its count in a room smaller than when it was
		// cast (KickVote_Check; a cast is looked at as it comes in)
		KickVote_Check(ri);

		// contested touches whose wait is up (WeaponHold_*)
		WeaponHold_Tick(ri);

		// the racers' pings to the room, once a second (Ping_Tick, 1114)
		Ping_Tick(ri);

		// a game mode's round decided at its green (Round_Tick), then TAG's
		// clock: started at the green, "it" out when it runs out (a round's
		// end here is picked up by the everybody-ended check below, this
		// same tick)
		Round_Tick(ri);
		Tag_Tick(ri);

		// A spectator still held SPECTATE_LOAD_SECONDS after it sat down
		// (1110, SV_config.h): its name never came, or its load never ended.
		// Removed as an idle player is, once; its leave is silent, as every
		// spectator's (ProcessDisconnectEvent).
		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			PeerInfo* pi = &ri->peerInfos[j];
			if (pi->peer == 0 || pi->spectator != 1 || pi->spectateAt == 0)
				continue;
			if ((int)(enet_time_get() - pi->spectateAt) < SPECTATE_LOAD_SECONDS * 1000)
				continue;

			pi->spectateAt = 0;
			if (pi->peer->state != ENET_PEER_STATE_CONNECTED)
				continue;
			PrintPrefix(r + 1);
			printf("Spectator %.*s (%d) did not reach the race in %ds, removed\n",
				NAME_LEN, &pi->name[0], j, SPECTATE_LOAD_SECONDS);
			enet_peer_disconnect(pi->peer, DISCONNECT_REASON_IDLE);
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
				// clients clear theirs at this message too), nor any mask,
				// TURBO or Saffi - and no vote to kick stands: the votes are
				// the lobby's (KickVote_*), the clients clear theirs at this
				// message too, and the next lobby starts with none
				int kickWiped = 0;
				memset(ri->weaponFate, 0, sizeof(ri->weaponFate));
				memset(ri->weaponHolds, 0, sizeof(ri->weaponHolds));
				for (int k = 0; k < MAX_CLIENTS; k++)
				{
					ri->peerInfos[k].maskShotAt = 0;
					ri->peerInfos[k].saffiSeenAt = 0;
					ri->peerInfos[k].turboShotAt = 0;
					ri->peerInfos[k].shieldClaim = 0;
					ri->peerInfos[k].shieldSerial = SHIELD_SERIAL_NONE;
					memset(ri->peerInfos[k].claimAt, 0, sizeof(ri->peerInfos[k].claimAt));
					for (unsigned m = ri->peerInfos[k].kickVotes; m != 0; m &= m - 1)
						kickWiped++;
					ri->peerInfos[k].kickVotes = 0;
				}
				if (kickVoteLog)
				{
					PrintPrefix(r + 1);
					printf("Kick votes (--kicklog): %d wiped at the countdown\n", kickWiped);
				}

				// The race as a spectator will join it (1110, Spectator_*):
				// the seats racing - none other is ever given to one - and
				// nothing of it yet, no finish and no kart message, no green
				// (raceGreenAt, 1111) - and nobody's camera on anybody, nobody
				// told a count (Watch_*, 1111: every client clears its
				// WATCHERS at this message; nothing sent).
				ri->raceSeats = 0;
				ri->endCount = 0;
				ri->raceGreenAt = 0;
				for (int k = 0; k < MAX_CLIENTS; k++)
				{
					if (ri->peerInfos[k].peer != 0)
						ri->raceSeats |= (unsigned short)(1u << k);
					ri->peerInfos[k].endOrder = 0;
					ri->peerInfos[k].endFlags = 0;
					ri->peerInfos[k].hasLastKart = 0;
					ri->peerInfos[k].watching = 0;
					ri->peerInfos[k].watchersTold = 0;
				}

				// send to all clients, the game modes' round seated now
				// (Round_Begin) and its code in the header's spare nibble -
				// set in every room, 0 for no round. It went out uninitialised
				// before the modes, which is why the client reads it in a
				// mode room only. The code is kept for a spectator's own
				// SG_STARTLOADING (roundCode, 1110).
				struct SG_Header sg;
				ri->roundCode = (unsigned char)(Round_Begin(ri) & 0xF);
				sg.type = SG_STARTLOADING;
				sg.padding = ri->roundCode;
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

				// its green, the lights from now (1111): watched from there
				// (RoomOpenForSpectate; Matt: JOIN ROOM greyed until the
				// green), its list pushed then by the room-list check below
				ri->raceGreenAt = (enet_time_get() + TAG_LIGHTS_MS) | 1;

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
				// process that spends its life asleep accrues it at some
				// fraction of real time (an old "removed one zero" fudge of
				// CLOCKS_PER_SEC papered over exactly that). The clients
				// count this down in real seconds, so the reset has to fire
				// on real seconds too or the number on screen means nothing.
				ri->endTime = enet_time_get();

				// Replaces the LOBBY_RETURN clock on every client.
				struct SG_MessageLobbyCountdown sg;
				sg.type    = SG_LOBBYCOUNTDOWN;
				sg.kind    = LOBBYCOUNT_CLOCK;
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

		// The room list says whether this room can be joined, and that can
		// change on its own as a clock runs down (RoomOpenForJoin): the list
		// is marked for this pass's push (RoomList_Flush, below), which goes
		// out at the end of the tick, or ROOM_LIST_MIN_MS after the previous
		// push. Likewise whether its race can be watched (1110,
		// RoomOpenForSpectate): a green, the close before the forced return,
		// a seat given or freed; and whether it is in a race (1111,
		// RoomInRace): its load - a room shown closed already by a pick
		// clock's last seconds still gets a list then. Last in the room's
		// pass (1111), so the list says what this tick made of the room: the
		// SG_STARTLOADING above is in whatever list goes out next (the reset
		// records its own change: RoomList_Changed).
		if (RoomList_Changed(ri))
			RoomList_Dirty();
	}

	// the room list to every connection, once, for whatever changed in this
	// pass - a join, a leave, a pick, a load, a reset, a kick's end
	// (RoomList_Flush: no sooner than ROOM_LIST_MIN_MS after the last)
	RoomList_Flush();
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
#ifndef _WIN32
	// Line-buffer stdout even when it is a pipe. Under systemd, Docker or any
	// other log collector glibc would otherwise hold output in a 4 KB buffer
	// and release it in batches, so the startup lines (the banner, the
	// heartbeat's "enabled as ..." report) would not show up for a long time
	// and a crash would lose whatever was still buffered. On a terminal this
	// is already the default.
	//
	// Not on Windows, MinGW's build included: its C runtime has no line
	// buffering (the request means full buffering there, which would hide
	// the console output instead of helping), and it aborts the process on a
	// size below 2 - which is how a first version of this line, with size 0,
	// crashed Server.exe on startup before the banner was printed.
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
#endif
	// (Windows: a console shows each printf as it is made. Anywhere else - a
	// file, a pipe, a service's log - the C runtime holds stdout in 4 KB
	// blocks, which would have 'Ready on port' and the room logs arrive late,
	// in batches, and be lost to a kill: the main loop flushes it once a pass
	// instead, below. Unbuffered would cost a system call per character.)

#ifdef _WIN32
	// The loop below sleeps a microsecond between ticks (SleepMicro, SV_helper.c),
	// which Windows rounds up to its timer's period. ENet asks for 1 ms
	// (enet_initialize, enet/win32.c), so a tick is about 1 ms -
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
		SleepMicro(1);
		ServerState_Tick();
#if BACKFILL_PLAYERS > 0
		Backfill_Tick();
#endif
#ifdef _WIN32
		// What this pass printed, out now (see the top of main): a write per
		// pass that printed anything, nothing for one that did not. The
		// heartbeat thread's lines go out on the next pass.
		fflush(stdout);
#endif
	}

	ServerState_Stop((int)g_stopSignal);
	return 0;
}
