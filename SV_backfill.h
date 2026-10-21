// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

// DEBUG ONLY: a lobby filled with bots, for testing with a full room from
// one or two machines. Compiled in only with BACKFILL_PLAYERS set to the
// number of bots (the build's define: /DBACKFILL_PLAYERS=10, or CL's
// /DBACKFILL_PLAYERS#10; CMake -DBACKFILL_PLAYERS=10), and then
// --backfill N changes it at launch. Never on a public server: the release
// build has none of this.
//
// Each bot is a real client of this server, over loopback: its own ENet
// host (its own port, as the room's duplicate-address check wants)
// connecting to 127.0.0.1 on the server's port, speaking the ordinary
// protocol - so every room, vote, clock, kick and race path runs exactly as
// it does for players. What a bot does:
//   - on the room list, it joins the room with the most players in it that
//     has a seat and is open; with nobody anywhere, it waits. So the bots
//     follow the first real player into their room.
//   - in the room: its name (BOT1...), as a client sends it on SG_NEWCLIENT;
//     a random vote a few seconds into each vote (SG_VOTE); a random
//     character locked in a few seconds after the track is in (SG_TRACK),
//     locked again after a cancel (SG_TRACKCANCEL); made host with voting
//     off, a random track and laps a few seconds later.
//   - loading (SG_STARTLOADING): CG_STARTRACE a few seconds on, as a
//     client does once its race has loaded. In a plain race it sends
//     nothing: its kart sits on the grid, it never finishes, and the room
//     returns to the lobby LOBBY_RETURN_SECONDS after the first real
//     finisher.
//   - in a game mode's round (a mode room whose SG_STARTLOADING carried a
//     round code, global.h's ROUND_CODE_*), it plays the mode by sending
//     what a client's ROM would report about its own kart, so the server's
//     referee (Round_*, SV_main.c) has something to judge and a round can
//     be tested from one DuckStation (Bot_RoundTick):
//       POTION GAME / BOMBARDMENT: every 10-40 s its kart "is hurt"
//         (NET_EV_KART_HURT) by a beaker / a bomb's blast from a random
//         player of the room, now and then itself; 1 in 4 is a decoy the
//         referee must not count (a shield stopped it, or another reason).
//       ELIMINATION: a lap (NET_EV_LAP) every 25 s + 0.7 s per bot (BOT1
//         the fastest), +-0.4 s, from the green; the last lap with its
//         CG_ENDRACE, as a client crossing the line sends both.
//       TAG YOU'RE IT: made "it" (NET_EV_IT), it hits another bot still in
//         with a missile 5-20 s on - the victim bot reports the hurt, as a
//         victim's machine does - or, a third of the time, catches nobody
//         and sits out its clock.
//     Placed (NET_EV_OUT about itself: out, finished, or still in at the
//     end) it reports nothing more and sends CG_ENDRACE, as a client does
//     once its ROM has entered the finished state; the round then ends on
//     its own. What bots cannot test: a human's weapon hitting a bot. A
//     bot's kart is only a copy on the human's machine, and only a kart's
//     own machine tests hits on it (netWeapons.c); that takes two
//     DuckStations.
//   - with --botkick N: a vote to kick the player in seat N (CG_HOSTACTION's
//     HOST_ACTION_KICKVOTE, by the name SG_NAME gave the seat) a few seconds
//     after the seat is named, in every lobby - the countdown wipes the
//     votes - so KICK_VOTES_BY_PLAYERS's counts can be tried at any size of
//     room from one DuckStation (the server's --kicklog shows each vote).
//     The server's answer to it (SG_VOTE's VOTE_KICK_REPLY) is no vote of
//     the room's, and is not taken for one.
//   - with --botspectate N (1110): the last N bots join only to watch - on
//     the room list, the fullest room whose race can be watched (SG_ROOMS'
//     spectateMask), with JOIN_SPECTATE - and, seated as a spectator
//     (SG_NEWCLIENT's NEWCLIENT_SPECTATOR), send their name, "load" the race
//     the answer gives them (CG_STARTRACE a few seconds after its
//     SG_STARTLOADING) and watch from the catch-up's SG_STARTRACE, reporting
//     CG_WATCH{N} with --botwatch (below) and nothing else; the room's reset
//     welcomes them as players (special 0), and from there they are
//     ordinary bots. --botspecname S holds a spectator's CG_NAME S seconds (a
//     race over between a join and its name), --botspecload S its
//     CG_STARTRACE (a room reset during its load). So the server's side of a
//     spectator - the seat, the hold, the catch-up, the WATCHERS of the racer
//     it watches, the welcome - can be tried from one DuckStation.
//   - with --botfinish N (1110): in a plain race, the first N racing bots
//     cross the line one after another, BOT1 --botfinishat S seconds after
//     the green (25 by default), each next 1.5 s later - their CG_ENDRACE as
//     a client sends it, with ENDRACE_BOT if --botfinishbot - so a real
//     player can finish 9th or later behind them (the bot driver's ranks past
//     vanilla's 8).
//   - with --botleave S (1110): a bot that has finished (--botfinish) or a
//     spectator bot watching leaves S seconds on - the racer it watched
//     (--botwatch) told one fewer, a finished bot's kart braked by its
//     watchdog, and a spectator joining after that gets its finish with no
//     ENDRACE_BOT.
//   - with --botwatch N (1111): a spectator bot, from its catch-up - or,
//     with --botspecload, from its SG_STARTLOADING, while it is still held -
//     and a --botfinish bot, right after its CG_ENDRACE, report their camera
//     on seat N (CG_WATCH{N}, once a race), as a client does when its camera
//     settles on that kart (Watch_Service, CL_main.cpp); 15 is nobody
//     (WATCH_NOBODY). So a racer's WATCHERS, the count of those whose camera
//     is on it, can be tried from one DuckStation: N the player's seat, or
//     another bot's to see that the player is told nothing.
//   - with --botload S (1111): the racing bots send their CG_STARTRACE S
//     seconds after SG_STARTLOADING, not 3-5 s on (at most the server's
//     load wait less 5 s, --loadsecs), so the room sits at the start line -
//     in a race, not watchable yet (racingMask without spectateMask) - for a
//     probe of RACE IN PROGRESS's greyed JOIN ROOM.
//   - removed (a kick, an idle clock), or gone with --botleave: back on the
//     room list, connected again a few seconds later. (A kick keeps
//     127.0.0.1 - every bot - out of that room for the kick ban's time.)
// Driven from the server's own loop (Backfill_Tick, main).

#define BACKFILL_MAX 16

// The start lights: the green comes this long after SG_STARTRACE
// (trafficLightsTimer 0xf40, states.c, at 960 a second), and a client's
// race clock (timeElapsedInRace, CG_ENDRACE's courseTime) runs from there.
#define BOT_LIGHTS_MS 4070

typedef struct
{
	ENetHost* host;
	ENetPeer* peer;
	int connected;
	int inRoom;
	int joinSent;
	int clientID;
	int isHost;
	int voteEnabled;
	int votePhase;
	int voted;              // the vote voted in (VOTE_*), 0 none
	int trackIn;            // the room's track is in (SG_TRACK)
	enet_uint32 voteAt;     // when to cast this vote; 0 none due
	enet_uint32 charAt;     // when to lock a character in
	enet_uint32 pickAt;     // when to pick the track as host (voting off)
	enet_uint32 raceAt;     // when to say the race has loaded
	enet_uint32 reconnectAt;

	// --botkick: the name in the seat voted against, as SG_NAME gave it
	// (CG_HOSTACTION carries it for the server's check), and when to cast
	// the vote; 0 none due. Cleared with the lobby round (SG_NEWCLIENT).
	char kickName[NAME_LEN];
	enet_uint32 kickAt;

	// The race (SG_STARTLOADING on, cleared with the round at SG_NEWCLIENT)
	// and, in a mode room, its round: what this bot's ROM would report.
	int room;               // the room it sits in, as the race loads (Bot_Room); -1 none
	int roundRow;           // the room row (ROOM_ROW_POTION_GAME to ROOM_ROW_TAG) when the race is a round; 0 none (row 0 is never a mode room)
	int roundLaps;          // ELIMINATION: the round's laps (the round code); 0 not known
	int placed;             // the server's NET_EV_OUT about this bot came: it reports nothing more
	int endSent;            // its CG_ENDRACE went (once a race, as the client's boolAlreadySent_EndRace)
	int roundOver;          // a verdict with ROUND_LAST came: nobody reports any more
	int isIt;               // TAG: the last NET_EV_IT named this bot
	int lapsDone;           // ELIMINATION: the laps reported (NET_EV_LAP's arg, as drivers[0]->lapIndex)
	int lapMs;              // ELIMINATION: this bot's lap time
	enet_uint32 raceStart;  // when SG_STARTRACE came (the green is BOT_LIGHTS_MS on); 0 not yet
	enet_uint32 hurtAt;     // POTION GAME, BOMBARDMENT: the next hurt to report; 0 none due
	enet_uint32 lapAt;      // ELIMINATION: the next lap to report; 0 none due
	enet_uint32 tagAt;      // TAG, while "it": when another bot reports this one's missile; 0 none due
	int hurtSeq;            // its hurts' number (NET_HURT_SEQ, 1103), as a ROM counts its kart's

	// A race in progress watched (1110). spectator: a --botspectate bot, for
	// good - it joins only to watch; spectating: seated as a spectator now
	// (SG_NEWCLIENT's NEWCLIENT_SPECTATOR), until the room's reset welcomes
	// it as a player; watching: its catch-up came (the private
	// SG_STARTRACE). nameAt: its CG_NAME due (--botspecname); finishAt: its
	// CG_ENDRACE due in a plain race (--botfinish); leaveAt: when it leaves
	// (--botleave). 0 none due. watchSent: its CG_WATCH went this race
	// (--botwatch, 1111; once a race, cleared with the race).
	int spectator;
	int spectating;
	int watching;
	enet_uint32 nameAt;
	enet_uint32 finishAt;
	enet_uint32 leaveAt;
	int watchSent;
} Bot;

static Bot bots[BACKFILL_MAX];
static int botCount = BACKFILL_PLAYERS;
static int botPort;
static int botKick = -1;   // --botkick: the seat (client ID) every bot votes to kick; -1 none
static int botSpectate = 0;       // --botspectate: the last N bots join only to watch a race in progress (1110)
static int botSpecNameSecs = 0;   // --botspecname: seconds a spectator bot holds its CG_NAME; 0 at once
static int botSpecLoadSecs = 0;   // --botspecload: seconds a spectator bot "loads"; 0 as every bot, 3-5 s
static int botFinish = 0;         // --botfinish: in a plain race, the first N racing bots cross the line
static int botFinishAtSecs = 25;  // --botfinishat: the first of them that long after the green
static int botFinishBot = 0;      // --botfinishbot: their CG_ENDRACE with ENDRACE_BOT
static int botLeaveSecs = 0;      // --botleave: seconds after its finish or its catch-up a bot leaves; 0 never
static int botWatch = -1;         // --botwatch: the seat spectator and finished bots report their camera on (CG_WATCH, 1111); -1 none sent
static int botLoadSecs = 0;       // --botload: seconds a racing bot "loads" (1111); 0 as ever, 3-5 s

#define BOT_FINISH_GAP_MS 1500    // --botfinish: between one bot's finish and the next's

static enet_uint32 Bot_Now(void)
{
	enet_uint32 t = enet_time_get();
	return t ? t : 1;   // 0 is "none due"
}

static enet_uint32 Bot_In(int fromMs, int toMs)
{
	return Bot_Now() + (enet_uint32)(fromMs + rand() % (toMs - fromMs + 1));
}

static void Bot_Send(Bot* b, const void* data, size_t size)
{
	ENetPacket* p = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);
	if (enet_peer_send(b->peer, 0, p) < 0)
		enet_packet_destroy(p);
}

static void Bot_Connect(Bot* b)
{
	ENetAddress a;
	enet_address_set_host(&a, "127.0.0.1");
	a.port = (enet_uint16)botPort;
	b->peer = enet_host_connect(b->host, &a, 2, 0);
	b->connected = 0;
	b->inRoom = 0;
	b->joinSent = 0;
	b->reconnectAt = 0;
	b->spectating = b->watching = 0;
}

// A race's state: none yet (a new race loading, or back in the lobby).
static void Bot_RaceReset(Bot* b)
{
	b->room = -1;
	b->roundRow = 0;
	b->roundLaps = 0;
	b->placed = b->endSent = b->roundOver = b->isIt = 0;
	b->lapsDone = b->lapMs = 0;
	b->raceStart = b->hurtAt = b->lapAt = b->tagAt = 0;
	b->finishAt = b->leaveAt = 0;
	b->watchSent = 0;
}

static void Bot_RoundReset(Bot* b)
{
	b->votePhase = VOTE_NONE;
	b->voted = 0;
	b->trackIn = 0;
	b->voteAt = b->charAt = b->pickAt = b->raceAt = 0;
	b->kickAt = 0;
	b->nameAt = 0;
	memset(b->kickName, 0, sizeof(b->kickName));
	Bot_RaceReset(b);
}

// Its name, as a client sends it at each SG_NEWCLIENT: BOT1... by its index.
static void Bot_SendName(Bot* b, int index)
{
	struct CG_MessageName m;
	memset(&m, 0, sizeof(m));
	m.type = CG_NAME;
	snprintf(m.name, sizeof(m.name), "BOT%d", index + 1);
	Bot_Send(b, &m, sizeof(m));
}

// The room this bot sits in, from the server's own table (the bots live in
// the server's process): the seat whose peer is the far end of this bot's
// connection - ENet gives both ends the connect's random ID (connectID).
// Not the room the bot last asked for: every join sends the room list to
// everybody, the joiner too, before its SG_NEWCLIENT, so the bot asks
// again (Bot_OnRooms), maybe for another room once the first is full, and
// the server ignores that second ask (CG_JOINROOM, "Already in a room").
// -1 when it sits nowhere.
static int Bot_Room(const Bot* b)
{
	if (b->peer == 0)
		return -1;
	for (int r = 0; r < 16; r++)
		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			const ENetPeer* p = roomInfos[r].peerInfos[j].peer;
			if (p != 0 && p->connectID == b->peer->connectID)
				return r;
		}
	return -1;
}

// A random taken seat (client ID) in this bot's room, other than skipA and
// skipB (-1: skip none), from the server's table as Bot_Room; -1 if none.
static int Bot_RandomSeat(const Bot* b, int skipA, int skipB)
{
	int ids[MAX_CLIENTS];
	int n = 0;

	if (b->room < 0)
		return -1;
	for (int j = 0; j < MAX_CLIENTS; j++)
		if (roomInfos[b->room].peerInfos[j].peer != 0 && j != skipA && j != skipB)
			ids[n++] = j;
	return n ? ids[rand() % n] : -1;
}

// What happened to this bot's kart, as its ROM's NetEv_Send would have it
// sent (CG_WEAPONEVENT; NET_EV_*, global.h).
static void Bot_SendEvent(Bot* b, int kind, int owner, int serial, int arg)
{
	struct CG_MessageWeaponEvent m;
	memset(&m, 0, sizeof(m));
	// a hurt numbered as a ROM numbers its kart's (1-15 round again: the
	// count's bits 1-4, bit 0 the attacker), so the clients' dedup takes it
	if (kind == NET_EV_KART_HURT)
	{
		b->hurtSeq = (b->hurtSeq % 15) + 1;
		serial = (b->hurtSeq << 1) | (serial & 1);
	}
	m.type = CG_WEAPONEVENT;
	m.owner = (unsigned char)owner;
	m.kind = (unsigned char)kind;
	m.serial = (unsigned char)serial;
	m.arg = (unsigned char)arg;
	Bot_Send(b, &m, sizeof(m));
}

// The race over for this bot, as a client sends it once its ROM has
// entered the finished state (StatePC_Game_EndRace, CL_main.cpp): the race
// clock in the game's units (960 a second), which runs from the green, and
// a best lap - ELIMINATION's lap time once it has done one, else none.
// flags: CG_MessageEndRace's (1110): ENDRACE_BOT for a --botfinishbot
// finish, as a client whose kart the bot driver took over says it; 0
// otherwise.
static void Bot_EndRace(Bot* b, int flags)
{
	struct CG_MessageEndRace m;
	int ms;

	if (b->endSent || b->raceStart == 0)
		return;   // once; and the server takes none before the race
	b->endSent = 1;

	ms = (int)(Bot_Now() - (b->raceStart + BOT_LIGHTS_MS));
	if (ms < 0)
		ms = 0;

	memset(&m, 0, sizeof(m));
	m.type = CG_ENDRACE;
	m.padding = flags & 0xF;
	m.courseTime = (int)((long long)ms * 960 / 1000);
	m.lapTime = b->lapsDone ? (int)((long long)b->lapMs * 960 / 1000) : 0;
	Bot_Send(b, &m, sizeof(m));
}

// The kart its camera follows, as a client reports it once its camera has
// settled on one (CG_WATCH, 1111: Watch_Service, CL_main.cpp): --botwatch's
// seat, once a race (watchSent); nothing without --botwatch.
static void Bot_SendWatch(Bot* b, int index)
{
	struct CG_MessageWatch m;

	if (botWatch < 0 || b->watchSent)
		return;
	b->watchSent = 1;

	memset(&m, 0, sizeof(m));
	m.type = CG_WATCH;
	m.target = (unsigned char)botWatch;
	Bot_Send(b, &m, sizeof(m));

	PrintPrefix(b->room >= 0 ? b->room + 1 : -1);
	printf("BOT%d (%d) reports its camera on %d (--botwatch)%s\n", index + 1, b->clientID, botWatch,
		b->spectating && !b->watching ? ", still held" : "");
}

// A lap time's wobble, +-0.4 s: less than the 0.7 s between two bots' lap
// times, so the order of a lap stays mostly the bots' order.
static int Bot_Jitter(void)
{
	return rand() % 801 - 400;
}

// The room list: the fullest room with a seat that is open, if anybody is
// in one.
static void Bot_OnRooms(Bot* b, const struct SG_MessageRooms* r)
{
	const unsigned char counts[16] = {
		r->numClients01, r->numClients02, r->numClients03, r->numClients04,
		r->numClients05, r->numClients06, r->numClients07, r->numClients08,
		r->numClients09, r->numClients10, r->numClients11, r->numClients12,
		r->numClients13, r->numClients14, r->numClients15, r->numClients16 };
	int best = -1;

	if (b->inRoom)
		return;
	b->joinSent = 0;   // a refused join comes back as a room list

	// --botspectate (1110): only to watch - the fullest room whose race can
	// be watched now, with JOIN_SPECTATE; never one to play in. A refusal
	// (the race over meanwhile, the seat taken) is a room list, as ever.
	if (b->spectator)
	{
		struct CG_MessageRoom m;
		for (int k = 0; k < 16; k++)
		{
			if (counts[k] == 0 || ((r->spectateMask >> k) & 1) == 0)
				continue;
			if (best < 0 || counts[k] > counts[best])
				best = k;
		}
		if (best < 0)
			return;
		memset(&m, 0, sizeof(m));
		m.type = CG_JOINROOM;
		m.flags = JOIN_SPECTATE;
		m.room = (unsigned char)best;
		m.version = SERVER_VERSION;
		Bot_Send(b, &m, sizeof(m));
		b->joinSent = 1;
		return;
	}

	for (int k = 0; k < 16; k++)
	{
		if (counts[k] == 0 || counts[k] >= MAX_CLIENTS || ((r->closedMask >> k) & 1))
			continue;
		if (best < 0 || counts[k] > counts[best])
			best = k;
	}
	if (best < 0)
		return;

	{
		struct CG_MessageRoom m;
		memset(&m, 0, sizeof(m));
		m.type = CG_JOINROOM;
		m.room = (unsigned char)best;
		m.version = SERVER_VERSION;
		Bot_Send(b, &m, sizeof(m));
		b->joinSent = 1;
	}
}

static void Bot_OnMessage(Bot* b, int index, const unsigned char* data, size_t size)
{
	int type = data[0] & 0xF;

	switch (type)
	{
		case SG_ROOMS:
			if (size >= sizeof(struct SG_MessageRooms))
				Bot_OnRooms(b, (const struct SG_MessageRooms*)data);
			break;

		case SG_NEWCLIENT:
		{
			// in the room (or the room back from a race): the name, a new round.
			// Seated to watch (1110: NEWCLIENT_SPECTATOR, a --botspectate
			// bot's JOIN_SPECTATE): its name maybe a while later
			// (--botspecname), the race to load in the answer to it; the
			// room's reset welcomes it as a player (special 0), and from
			// there it is one as every bot is.
			const struct SG_MessageClientStatus* r = (const struct SG_MessageClientStatus*)data;
			b->inRoom = 1;
			b->clientID = r->clientID;
			b->isHost = 0;
			b->spectating = (r->special & NEWCLIENT_SPECTATOR) != 0;
			b->watching = 0;
			Bot_RoundReset(b);
			if (b->spectating)
			{
				int rn = Bot_Room(b);
				PrintPrefix(rn >= 0 ? rn + 1 : -1);
				printf("BOT%d (%d) seated as a spectator%s\n", index + 1, b->clientID,
					botSpecNameSecs > 0 ? ", its name held (--botspecname)" : "");
			}
			if (b->spectating && botSpecNameSecs > 0)
				b->nameAt = Bot_Now() + (enet_uint32)botSpecNameSecs * 1000;
			else
				Bot_SendName(b, index);
			break;
		}

		case SG_NAME:
		{
			// --botkick: the seat voted against named - a newcomer in it, or
			// its player's name again with the next lobby round - and the
			// vote cast a few seconds on; emptied (its player gone), none.
			// Never against itself (the server refuses that).
			const struct SG_MessageName* r = (const struct SG_MessageName*)data;
			if (botKick < 0 || size < sizeof(struct SG_MessageName) ||
				r->clientID != botKick || r->clientID == b->clientID)
				break;
			memcpy(b->kickName, r->name, NAME_LEN);
			b->kickAt = r->name[0] ? Bot_In(2000, 8000) : 0;
			break;
		}

		case SG_HOST:
		{
			// (a spectator picks nothing, 1110: it is in no lobby until the
			// reset - the server drops a pick from it anyway)
			const struct SG_MessageHost* r = (const struct SG_MessageHost*)data;
			b->isHost = (r->hostID == b->clientID);
			if (b->isHost && !b->spectating && !b->voteEnabled && !b->trackIn && b->pickAt == 0)
				b->pickAt = Bot_In(3000, 8000);
			break;
		}

		case SG_VOTE:
		{
			const struct SG_MessageVote* r = (const struct SG_MessageVote*)data;

			// The answer to this bot's own vote to kick (--botkick, 1110;
			// SG_MessageKickVote): nothing of the room's vote is in it - its
			// phase is no vote's, and the bit enabled sits in is its result's
			// - so none of it is taken.
			if (r->phase == VOTE_KICK_REPLY)
				break;

			b->voteEnabled = r->enabled;
			if (r->phase != b->votePhase)
			{
				// a vote cut short (PLAYER VOTING off) and started again:
				// a new vote, in the same phase as the one voted in
				if (r->phase == VOTE_NONE)
					b->voted = 0;
				b->votePhase = r->phase;
				b->voteAt = (r->phase != VOTE_NONE && b->voted != r->phase && !b->spectating) ? Bot_In(1000, 6000) : 0;
			}
			if (b->isHost && !b->spectating && !b->voteEnabled && r->phase == VOTE_NONE && !b->trackIn && b->pickAt == 0)
				b->pickAt = Bot_In(3000, 8000);
			break;
		}

		case SG_TRACK:
			// (a spectator's, at its seat: the race's track, no character to
			// pick - 1110)
			b->trackIn = 1;
			b->votePhase = VOTE_NONE;
			b->voted = 0;   // the vote over (as an SG_VOTE of phase NONE says)
			b->voteAt = b->pickAt = 0;
			b->charAt = b->spectating ? 0 : Bot_In(2000, 10000);
			break;

		case SG_TRACKCANCEL:
			b->trackIn = 0;
			b->charAt = 0;
			if (b->isHost && !b->voteEnabled)
				b->pickAt = Bot_In(3000, 8000);
			break;

		case SG_STARTLOADING:
		{
			// A new race. Its round code (the header nibble, global.h's
			// ROUND_CODE_*): heeded in a mode room only, as the client does
			// (an older server leaves the nibble uninitialised); 0 a plain
			// race; for ELIMINATION the laps.
			// A spectator's (1110: the answer to its CG_NAME) is the same,
			// but it plays no round - it has no kart - and "loads" for
			// --botspecload seconds if given; a racing bot for --botload
			// seconds (1111), holding the room at the start line.
			// A spectator "loading" for --botspecload reports its camera
			// now, while still held (--botwatch, 1111): the server stores it
			// and counts it only from the catch-up.
			int code = ((const struct SG_Header*)data)->padding;
			Bot_RaceReset(b);
			b->room = Bot_Room(b);
			if (b->room >= 0 && ROOM_IS_MODE(b->room) && code != 0 && !b->spectating)
			{
				b->roundRow = ROOM_ROW(b->room);
				b->roundLaps = (code < ROUND_CODE_VOTED) ? code : 0;
			}
			if (b->spectating && botSpecLoadSecs > 0)
			{
				b->raceAt = Bot_Now() + (enet_uint32)botSpecLoadSecs * 1000;
				Bot_SendWatch(b, index);
			}
			else if (!b->spectating && botLoadSecs > 0)
				b->raceAt = Bot_Now() + (enet_uint32)botLoadSecs * 1000;
			else
				b->raceAt = Bot_In(3000, 5000);
			break;
		}

		case SG_STARTRACE:
			// the lights: the green BOT_LIGHTS_MS on. A round's first reports
			// are planned from here (the referee judges from the green).
			b->raceStart = Bot_Now();

			// A spectator's (1110): its catch-up's last word - watching now,
			// its camera reported (--botwatch, 1111; already, held, with
			// --botspecload), and gone --botleave seconds on if given.
			if (b->spectating)
			{
				b->watching = 1;
				if (botLeaveSecs > 0)
					b->leaveAt = b->raceStart + (enet_uint32)botLeaveSecs * 1000;
				PrintPrefix(b->room >= 0 ? b->room + 1 : -1);
				printf("BOT%d (%d) caught up and watching\n", index + 1, b->clientID);
				Bot_SendWatch(b, index);
				break;
			}

			// --botfinish (1110): in a plain race, the first N racing bots
			// across the line in their order, from --botfinishat seconds
			// after the green
			if (b->roundRow == 0 && index < botFinish)
				b->finishAt = b->raceStart + BOT_LIGHTS_MS + (enet_uint32)botFinishAtSecs * 1000 +
					(enet_uint32)index * BOT_FINISH_GAP_MS;

			if (b->roundRow == ROOM_ROW_POTION_GAME)
				b->hurtAt = Bot_In(10000, 40000);
			else if (b->roundRow == ROOM_ROW_BOMBARDMENT)
				b->hurtAt = Bot_In(4000, 14000);   // BOMB_LIVES of them to go out
			else if (b->roundRow == ROOM_ROW_ELIMINATION)
			{
				// a lap time of its own, in the bots' order (BOT1 the
				// fastest), so who goes out on which lap is foreseeable
				b->lapMs = 25000 + index * 700;
				b->lapAt = b->raceStart + BOT_LIGHTS_MS + (enet_uint32)(b->lapMs + Bot_Jitter());
			}
			break;

		case SG_WEAPONEVENT:
		{
			// The referee's verdicts (the rest - weapons, other karts' hurts -
			// is for a ROM to show; a bot has none).
			const struct SG_MessageWeaponEvent* r = (const struct SG_MessageWeaponEvent*)data;
			if (size < sizeof(struct SG_MessageWeaponEvent))
				break;

			// (a spectator has no kart: none of it is about it, 1110)
			if (b->spectating)
				break;

			if (r->kind == NET_EV_OUT)
			{
				// placed - out, finished or still in at the end, any of
				// them: its ROM would enter the finished state (gameModes.c)
				// and its client send CG_ENDRACE; nothing more is reported
				if (r->clientID == b->clientID && !b->placed)
				{
					b->placed = 1;
					b->isIt = 0;
					b->hurtAt = b->lapAt = b->tagAt = 0;
					Bot_EndRace(b, 0);
				}
				if (r->serial & ROUND_LAST)
					b->roundOver = 1;
			}
			else if (r->kind == NET_EV_IT)
			{
				// TAG: "it" now (from the lights, or a tag, or another "it"
				// out of time or gone). Planned once, as it becomes "it" -
				// the green's NET_EV_IT names the same player again. A third
				// of the time it catches nobody and sits out its clock:
				// tagging always inside 20 s, against a clock that restarts
				// on every tag, no bot would ever be out of time, and a room
				// of bots would never end its round.
				int wasIt = b->isIt;
				b->isIt = (r->clientID == b->clientID && !b->placed);
				if (!b->isIt)
					b->tagAt = 0;
				else if (!wasIt)
				{
					// from the green on, if that is still to come
					enet_uint32 now = Bot_Now();
					enet_uint32 from = (b->raceStart ? b->raceStart : now) + BOT_LIGHTS_MS;
					if ((int)(from - now) < 0)
						from = now;
					b->tagAt = (rand() % 3 == 0) ? 0 : from + (enet_uint32)(5000 + rand() % 15001);
				}
			}
			break;
		}

		default:
			break;
	}
}

// The race's reports in a round, as this bot's ROM would send them about
// its own kart (each machine reports only its own: netWeapons.c). Nothing
// once it is placed or the round is over, or before its race has started.
static void Bot_RoundTick(Bot* b, int index, enet_uint32 now)
{
	if (b->roundRow == 0 || b->placed || b->roundOver || b->raceStart == 0)
		return;

	// POTION GAME / BOMBARDMENT: hurt by a beaker (damage type 1, reason 2
	// driven into it or 4 touched while thrown) / a bomb's blast (type 2,
	// reason 1), by a random player of the room, now and then itself (your
	// own beaker counts; your own bomb never hurts you in the game, but the
	// referee does not ask). Its identity: the attacker's client ID, count 1.
	// 1 in 4 is a decoy the referee must not count: a shield stopped it
	// (NET_KART_BLOCKED), or the right damage type for another reason (a
	// spin from no beaker; a TNT or Nitro blast). The clients replay every
	// one on the bot's kart on the grid, so the spins show.
	if (b->hurtAt && (int)(now - b->hurtAt) >= 0)
	{
		int potion = (b->roundRow == ROOM_ROW_POTION_GAME);
		int arg = potion ? (1 | ((rand() % 2 ? 2 : 4) << 3)) : (2 | (1 << 3));
		int owner = Bot_RandomSeat(b, -1, -1);
		const char* decoy = "";

		if (owner < 0)
			owner = b->clientID;
		if (rand() % 4 == 0)
		{
			if (rand() % 2)
			{
				arg |= NET_KART_BLOCKED;
				decoy = " (decoy, shield-stopped: no verdict due)";
			}
			else
			{
				arg = potion ? (1 | (0 << 3)) : (2 | (2 << 3));
				decoy = potion ? " (decoy, a spin from no beaker: no verdict due)" : " (decoy, a TNT or Nitro blast: no verdict due)";
			}
		}

		Bot_SendEvent(b, NET_EV_KART_HURT, owner, 1, arg);
		PrintPrefix(b->room + 1);
		printf("BOT%d (%d) reports a %s hurt by %d%s\n", index + 1, b->clientID,
			potion ? "beaker" : "bomb blast", owner, decoy);

		// again until a verdict comes: a counted hurt's comes at once (in
		// BOMBARDMENT a life's loss, BOMB_LIVES before the verdict: sooner)
		b->hurtAt = potion ? Bot_In(10000, 40000) : Bot_In(4000, 14000);
	}

	// ELIMINATION: a lap done (drivers[0]->lapIndex raised: 1 the first after
	// the green). The last one is a crossing of the finish line too, and a
	// client crossing it sends CG_ENDRACE with the lap - in either order, as
	// the ROM's lap report and the client's end state race each other; the
	// referee takes whichever comes first as the finish.
	if (b->lapAt && (int)(now - b->lapAt) >= 0)
	{
		int last;

		b->lapsDone++;
		last = (b->roundLaps > 0 && b->lapsDone >= b->roundLaps);
		if (last && rand() % 2)
			Bot_EndRace(b, 0);
		Bot_SendEvent(b, NET_EV_LAP, 0, 0, b->lapsDone);
		if (last)
			Bot_EndRace(b, 0);

		PrintPrefix(b->room + 1);
		printf("BOT%d (%d) reports lap %d%s\n", index + 1, b->clientID, b->lapsDone,
			last ? ", its last (and CG_ENDRACE)" : "");

		b->lapAt = last ? 0 : b->lapAt + (enet_uint32)(b->lapMs + Bot_Jitter());
	}

	// TAG: "it" hits another bot still in with a missile (damage type 2,
	// reason 3), and the victim bot reports it - a victim's machine is the
	// one that sees the hit. 1 in 4 is a decoy that must pass nothing on: a
	// shield stopped it, or the missile was not "it"'s (another player's,
	// which just blasts). Tried again 5-20 s on until a tag takes - the
	// next NET_EV_IT naming somebody else stops it (tagAt is set only while
	// this bot is "it": the NET_EV_IT and NET_EV_OUT cases above).
	if (b->tagAt && (int)(now - b->tagAt) >= 0)
	{
		int cand[BACKFILL_MAX];
		int n = 0;

		b->tagAt = Bot_In(5000, 20000);
		for (int k = 0; k < botCount; k++)
		{
			const Bot* v = &bots[k];
			if (v == b || v->peer == 0 || !v->connected || !v->inRoom)
				continue;
			if (v->room != b->room || v->roundRow == 0 || v->raceStart == 0 || v->placed)
				continue;
			cand[n++] = k;
		}

		if (n > 0)
		{
			int k = cand[rand() % n];
			Bot* v = &bots[k];
			int owner = b->clientID;
			int arg = 2 | (3 << 3);
			const char* decoy = "";

			if (rand() % 4 == 0)
			{
				int other = Bot_RandomSeat(b, b->clientID, v->clientID);
				if (other >= 0 && rand() % 2)
				{
					owner = other;
					decoy = " (decoy, not it's missile: no tag due)";
				}
				else
				{
					arg |= NET_KART_BLOCKED;
					decoy = " (decoy, shield-stopped: no tag due)";
				}
			}

			Bot_SendEvent(v, NET_EV_KART_HURT, owner, 1, arg);
			PrintPrefix(b->room + 1);
			printf("BOT%d (%d) reports a missile hit by %d, BOT%d (%d) being it%s\n",
				k + 1, v->clientID, owner, index + 1, b->clientID, decoy);
		}
	}
}

// Each loop of the server (main): every bot's network, then what it has due.
static void Backfill_Tick(void)
{
	for (int i = 0; i < botCount; i++)
	{
		Bot* b = &bots[i];
		ENetEvent ev;
		enet_uint32 now = Bot_Now();

		if (b->host == 0)
			continue;

		while (enet_host_service(b->host, &ev, 0) > 0)
		{
			switch (ev.type)
			{
				case ENET_EVENT_TYPE_CONNECT:
					b->connected = 1;
					break;
				case ENET_EVENT_TYPE_RECEIVE:
					if (ev.packet->dataLength >= 1)
						Bot_OnMessage(b, i, ev.packet->data, ev.packet->dataLength);
					enet_packet_destroy(ev.packet);
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					// removed: on the room list again, connected anew shortly
					b->connected = 0;
					b->inRoom = 0;
					b->spectating = b->watching = 0;
					b->peer = 0;
					b->reconnectAt = Bot_In(3000, 6000);
					break;
				default:
					break;
			}
		}

		if (b->peer == 0)
		{
			if (b->reconnectAt && (int)(now - b->reconnectAt) >= 0)
				Bot_Connect(b);
			continue;
		}
		if (!b->connected || !b->inRoom)
			continue;

		if (b->voteAt && (int)(now - b->voteAt) >= 0)
		{
			struct CG_MessageVote m;
			b->voteAt = 0;
			if (b->votePhase != VOTE_NONE)
			{
				memset(&m, 0, sizeof(m));
				m.type = CG_VOTE;
				m.phase = b->votePhase;
				m.option = (unsigned char)(rand() % (b->votePhase == VOTE_TRACK ? VOTE_TRACKS : VOTE_LAP_ROWS));
				Bot_Send(b, &m, sizeof(m));
				b->voted = b->votePhase;
			}
		}

		if (b->pickAt && (int)(now - b->pickAt) >= 0)
		{
			struct CG_MessageTrack m;
			b->pickAt = 0;
			if (b->isHost && !b->trackIn && b->votePhase == VOTE_NONE)
			{
				memset(&m, 0, sizeof(m));
				m.type = CG_TRACK;
				m.trackID = rand() % VOTE_TRACKS;
				m.lapID = rand() % VOTE_LAP_ROWS;
				Bot_Send(b, &m, sizeof(m));
			}
		}

		if (b->charAt && (int)(now - b->charAt) >= 0)
		{
			struct CG_MessageCharacter m;
			b->charAt = 0;
			memset(&m, 0, sizeof(m));
			m.type = CG_CHARACTER;
			m.characterID = rand() % 16;
			m.boolLockedIn = 1;
			Bot_Send(b, &m, sizeof(m));
		}

		if (b->raceAt && (int)(now - b->raceAt) >= 0)
		{
			struct CG_Header m;
			b->raceAt = 0;
			memset(&m, 0, sizeof(m));
			m.type = CG_STARTRACE;
			Bot_Send(b, &m, sizeof(m));
		}

		// --botkick: its vote against that seat, as a ROM's VOTE TO KICK
		// has its client send it (HostAction_Service, CL_main.cpp)
		if (b->kickAt && (int)(now - b->kickAt) >= 0)
		{
			struct CG_MessageHostAction m;
			b->kickAt = 0;
			memset(&m, 0, sizeof(m));
			m.type = CG_HOSTACTION;
			m.action = HOST_ACTION_KICKVOTE;
			m.clientID = (unsigned char)botKick;
			memcpy(m.name, b->kickName, NAME_LEN);
			Bot_Send(b, &m, sizeof(m));
		}

		// --botspecname (1110): a spectator's name, held till now
		if (b->nameAt && (int)(now - b->nameAt) >= 0)
		{
			b->nameAt = 0;
			Bot_SendName(b, i);
		}

		// --botfinish (1110): across the line, as a client says it - with
		// ENDRACE_BOT for --botfinishbot - its camera then on --botwatch's
		// seat (1111: a client's CG_ENDRACE always goes ahead of its first
		// CG_WATCH) and, with --botleave, gone a while on
		if (b->finishAt && (int)(now - b->finishAt) >= 0)
		{
			b->finishAt = 0;
			if (!b->endSent)
			{
				Bot_EndRace(b, botFinishBot ? ENDRACE_BOT : 0);
				PrintPrefix(b->room >= 0 ? b->room + 1 : -1);
				printf("BOT%d (%d) crosses the line (--botfinish)%s\n", i + 1, b->clientID,
					botFinishBot ? ", its kart driven on by the bot (ENDRACE_BOT)" : "");
				Bot_SendWatch(b, i);
				if (botLeaveSecs > 0)
					b->leaveAt = now + (enet_uint32)botLeaveSecs * 1000;
			}
		}

		// --botleave (1110): gone, as a player closing the game - finished,
		// or watching. Nothing more from it; its DISCONNECT puts it back on
		// the room list (above).
		if (b->leaveAt && (int)(now - b->leaveAt) >= 0)
		{
			b->leaveAt = 0;
			PrintPrefix(b->room >= 0 ? b->room + 1 : -1);
			printf("BOT%d (%d) leaves (--botleave)\n", i + 1, b->clientID);
			enet_peer_disconnect(b->peer, 0);
			b->inRoom = 0;
			continue;
		}

		// a game mode's round: what its ROM would report
		Bot_RoundTick(b, i, now);
	}
}

// After the server's host is up (ServerState_FirstBoot).
static void Backfill_Init(int port)
{
	if (botCount > BACKFILL_MAX) botCount = BACKFILL_MAX;
	if (botCount < 0) botCount = 0;
	if (botSpectate > botCount) botSpectate = botCount;
	// (--botload, 1111: inside the server's own wait on a load, --loadsecs,
	// with 5 s to spare - the room holds at the start line, nobody removed)
	if (botLoadSecs > loadSeconds - 5) botLoadSecs = (loadSeconds > 5) ? loadSeconds - 5 : 0;
	botPort = port;

	for (int i = 0; i < botCount; i++)
	{
		// --botspectate (1110): the last N, for good
		bots[i].spectator = (i >= botCount - botSpectate);

		bots[i].host = enet_host_create(NULL, 1, 2, 0, 0);
		if (bots[i].host == 0)
		{
			fprintf(stderr, "Backfill: bot %d's host not created\n", i + 1);
			continue;
		}
		Bot_Connect(&bots[i]);
	}

	PrintPrefix(-1);
	printf("DEBUG BUILD: %d bots (BOT1-BOT%d) join the first room a player is in\n", botCount, botCount);
	if (botSpectate > 0)
	{
		PrintPrefix(-1);
		printf("DEBUG BUILD: BOT%d-BOT%d join only to watch a race in progress (--botspectate)\n",
			botCount - botSpectate + 1, botCount);
	}
	if (botFinish > 0)
	{
		PrintPrefix(-1);
		printf("DEBUG BUILD: in plain races BOT1-BOT%d cross the line from %ds after the green (--botfinish)%s\n",
			botFinish, botFinishAtSecs, botFinishBot ? ", with ENDRACE_BOT" : "");
	}
	if (botWatch >= 0)
	{
		PrintPrefix(-1);
		printf("DEBUG BUILD: spectator and finished bots report their camera on %d (--botwatch)\n", botWatch);
	}
	if (botLoadSecs > 0)
	{
		PrintPrefix(-1);
		printf("DEBUG BUILD: racing bots load for %ds (--botload)\n", botLoadSecs);
	}
}
