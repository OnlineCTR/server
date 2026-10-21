// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3, with additional terms under its section 7 (LICENSE and
// ADDITIONAL-TERMS in the OnlineCTR server's repository). It comes with
// ABSOLUTELY NO WARRANTY.

#ifndef ONLINE_GLOBAL_H
#define ONLINE_GLOBAL_H

// Two numbers, shown in the client and server console banners. VERSION
// reads as V<a>.<b> BUILD <cd>: 1101 is V1.1 BUILD 1, as the server list's
// About view shows it (about.c).
//
// VERSION is the build. It steps every release. The ROM and the client must
// agree on it (the launcher ships them together from data/<VERSION>/), and it
// names the server release zip.
//
// SERVER_VERSION is what goes on the wire: the server sends it in the room
// list (SG_MessageRooms), the client sends it in the join request
// (CG_MessageRoom), and each refuses the other on a mismatch. It is the
// VERSION of the last release that changed the protocol - a message struct
// or type in this header, what the server does in reply to one, or the seats
// in a room (MAX_NUM_PLAYERS: the messages stay the same size). A release
// that leaves those alone bumps only VERSION, and every server already
// running keeps accepting the new client; a release that touches them bumps
// both, and that one is a server release too.
#define VERSION 1110
#define SERVER_VERSION 1110

// The server's SV_main.c names the protocol it was written for (SV_PROTOCOL,
// defined before it includes this header; 1110). One from another release
// than this header must not build: it would relay messages of a size and
// meaning this header no longer gives them (an 1103 relay of 1104's kart
// message sends no lap word, and every kart's place goes wrong).
#if defined(SV_PROTOCOL) && (SV_PROTOCOL != SERVER_VERSION)
#error "global.h and SV_main.c are from different releases: replace both with the ones from this release's zip (see README)"
#endif

// The weapons' sync (netWeapons.c): the weapon message carries the
// shooter's pose, shot count and target, and SG/CG_WEAPONEVENT what
// happened to a weapon or a kart, which the server arbitrates. SV_main.c
// checks for this to catch a stale global.h. 2: the server's arbitration,
// the projectiles' and the karts' events, the wider event. 3: Tiger
// Temple's door (NET_EV_DOOR) and the item-less rooms' ghost bombs.
// 4: 4-bit client IDs and owners on the wire, the 12-bit identity, the
// rooms' closed mask. 5: the game modes' events (NET_EV_LAP, NET_EV_OUT,
// NET_EV_IT) and SG_STARTLOADING's round code. 6 (1102): a contested touch's
// claim marked as waiting (CG_MessageWeaponEvent.tentative), judged by the
// server on the claimants' pings (WeaponHold_*, SV_main.c). 7 (1103): a
// kart's hurt numbered (NET_EV_KART_HURT's count: NET_HURT_SEQ) and carried
// in its kart message too (CG/SG_EverythingKart.hurt), so the others' copy
// of it is hurt with that message's position. 8 (1107): a masked kart's
// bump claimed by its own machine (NET_EV_KART_CLAIM), which the server
// sends to the kart it bumped alone, with both players' round trips. 9
// (1109): a shield's bump claimed the same way, mask or not (the server
// takes one a shield fired), and a shield's bump told as its own hurt
// reason (NET_HURT_SHIELD_BUMP), for every machine to pop the bumper's
// shield too. 10 (1110): a squish (NET_CLAIM_HIT(3, 5)) claimed the same
// way, mask or not (the server takes one from a player whose kart it saw
// with Saffi, FLAME_SAFFI, or who fired a TURBO lately), and Saffi's blue
// sent apart from TAG YOU'RE IT's "it" Saffi (FLAME_SAFFI).
#define NET_WEAPON_EVENTS 10

// The game modes of room rows 2-5 (POTION GAME, BOMBARDMENT, ELIMINATION,
// TAG YOU'RE IT): the server referees every knockout from the karts' own
// reports (NET_EV_KART_HURT, NET_EV_LAP) and tells the room (NET_EV_OUT,
// NET_EV_IT); the ROM ghosts and spectates the out (gameModes.c). SV_main.c
// checks for this to catch a stale global.h. 2 (1102): BOMBARDMENT's lives
// (BOMB_LIVES, NET_EV_LIVES, octr->roundHits).
#define NET_ROUND_MODES 2

// The lobby's OPTIONS (hostOptions.c): CG_HOSTACTION, the host handing the
// room to another player, DISCONNECT_REASON_KICKED and the server's kick
// ban. SV_main.c checks for this to catch a stale global.h. 2 (1110):
// VOTE TO KICK - HOST_ACTION_KICKVOTE / KICKUNVOTE from any player, the
// room's secret count against KICK_VOTES_BY_PLAYERS, answered by SG_VOTE's
// VOTE_KICK_REPLY to the voter alone; the host's instant KICK PLAYER gone.
#define NET_HOST_ACTIONS 2

// The players' vote for the track and then the laps (vote.c, SV_main.c's
// Vote_*): SG_VOTE (in the old SG_SERVERCLOSED's place), CG_VOTE, and the
// host's PLAYER VOTING (HOST_ACTION_VOTING). SV_main.c checks for this to
// catch a stale global.h.
#define NET_VOTING 1

// The kart message's lap word and number (CG/SG_EverythingKart.lap and .seq,
// NET_LAP_* below; 1104): every machine ranks the other players' karts by
// their own screens' laps and progress, and finishes them when the server
// says so (netLap.c). SV_main.c checks for this to catch a stale global.h.
#define NET_KART_LAPS 1

// A race in progress watched, and the bot driver past the finish line
// (1110). A room racing past its green, not over, with a seat that was
// empty at its load can be watched (SG_MessageRooms.spectateMask,
// octr->clientCount's ROOM_SPECTATE): a join with JOIN_SPECTATE
// (CG_MessageRoom.flags) takes that seat as a spectator
// (SG_MessageClientStatus.special's NEWCLIENT_SPECTATOR), kept out of the
// race, its rules and the lobby's counts, and caught up once its race has
// loaded (the server's Spectator_*; the ROM's spectate.c,
// OCTR_SPECIAL_SPECTATOR). The racers are told how many watch
// (SG_MessageLobbyCountdown's LOBBYCOUNT_WATCHERS, octr->watchers: WATCHERS
// on their HUD). A player over the line is handed to the bot driver as in
// the game, and their kart drives on: their CG_ENDRACE says so
// (ENDRACE_BOT), the server's SG_ENDRACE passes it on, and the other
// machines hold no brake on that copy (octr->copyParked). SV_main.c checks
// for this to catch a stale global.h.
#define NET_SPECTATE 1

// A vote: VOTE_NONE (the host picks, or no pick due), VOTE_TRACK, VOTE_LAPS.
// Each runs VOTE_SECONDS, or until every player has voted (the server's
// --votesecs, for testing), over the VOTE_TRACKS race tracks of the track
// list (menu.c, NUM_TRACKS) and the VOTE_LAP_ROWS lap rows (LAP_COUNT_BY_ID).
#define VOTE_NONE      0
#define VOTE_TRACK     1
#define VOTE_LAPS      2
#define VOTE_SECONDS   30
#define VOTE_TRACKS    18
#define VOTE_LAP_ROWS  8

// The ROM's vote box (vote.c): the list this far left of the host's list's
// place, the VOTES column at its right, the box round both centred where the
// host's list is - and the page arrows this far right of the list (menu.c).
#define VOTE_LIST_SHIFT 25

// The ROM crash screen's self test (crash.c): hold L1+R1+L2+R2 and tap
// SQUARE for a deliberate BREAK. 0 for a release - the chord and its
// per-frame check compile out; 1 to test the screen. The screen itself is
// always in.
#define CRASH_SELF_TEST 0

// ModsC, the code block in expansion RAM (ModsC.c): where it is linked and
// loaded to, its ceiling, and the disc file it travels in - lang/ja.lng,
// bigfile.txt line 235, which the USA build never reads.
// (kartVideo.c keeps the video decoder's table just under it, from
// 0x8078F000, the kart model cache's end - kartCache.c, from the
// allocator's ceiling at 0x80720000.) The ceiling: the Saffi flame tables
// (saffiFlame.h); a whole number of sectors from MODSC_ADDR, so the block's
// whole-sector read stays under it (saffiFlame.c checks both). 0x807B0000
// until 16 Oct 2026, under the kart view's old 48 KB model buffer - unused
// since the models moved to their cache.
#define MODSC_ADDR 0x807A0000
#define MODSC_END  0x807BC000
#define BI_MODSC   234

// The game modes' knockout chime (leaverSound.c): KART.HWL sample 156, raw
// SPU ADPCM, in its own disc file - lang/uk.lng, bigfile.txt line 237, which
// the USA build never reads (tools/make_knock_sample.py) - read once a
// session with ModsC (trackPic.c) to the top 8 KB of expansion RAM, above
// the interpolation tables (interp.h; leaverSound.c checks the fit).
#define KNOCK_SAMPLE_ADDR  0x807FE000
#define KNOCK_SAMPLE_BYTES 7104
#define BI_KNOCK_SAMPLE    236

// The 60 FPS disc's kart rows in its interpolation tables (struct
// InterpTables, interp.h, which asserts these), one a kart slot. From
// Interp_BeforeRender to Interp_AfterInstances - the 3D pass of the first
// draw after a logic step - every kart's posCurr is halfway back to the step
// before, its true one kept in drvSave and put back after the pass. The client
// reads its own kart's row, to send the true position (Kart_OwnPos,
// CL_main.cpp), and writes another kart's drvSave with its posCurr, so the
// put-back keeps the kart message's (1109). Never written by the 30 FPS disc.
#define INTERP_DRV_PREV(slot)  (0x807FD400u + 12u * (unsigned int)(slot))   // Vec3: the step before's
#define INTERP_DRV_SAVE(slot)  (0x807FD490u + 12u * (unsigned int)(slot))   // Vec3: this step's, while the half step is drawn
#define INTERP_DRV_PTR(slot)   (0x807FD520u + 4u * (unsigned int)(slot))    // the Driver the row was last kept for
#define INTERP_DRV_STAMP(slot) (0x807FD550u + 4u * (unsigned int)(slot))    // gGT->timer then
//#define ONLINE_BETA_MODE

// Post-race countdown drawn under RACE COMPLETE (seconds until the server
// returns everybody to the lobby).
//   1 = flicker red/white, in step with the RACE COMPLETE text above it
//   0 = solid red
#define RACE_OVER_COUNTDOWN_FLICKER 1

#ifndef WINDOWS_INCLUDE
	#include <common.h>
#endif

#ifdef __GNUC__ // GCC and Clang

	#ifdef WINDOWS_INCLUDE
		#include <unistd.h> // for the 'usleep()' function
	#endif

	#define STATIC_ASSERT2(test_for_true, message) _Static_assert((test_for_true), message)

#else // MSVC (Visual Studio)

	#define STATIC_ASSERT2 static_assert

#endif

//#define true				1
//#define false				0

#define DONT_SHOW_NAME		            0
#define SHOW_NAME			            1

#define DEFAULT_IP			            "127.0.0.1" // the default IP address we want to use for private lobbies
#define IP_ADDRESS_SIZE		            16 // assuming IPv4 (which is "xxx.xxx.xxx.xxx" + '\0')
#define PORT_SIZE			            6 // the port number as a string (0-65535 + '\0')

// countryNames[] in menu.c: the entry that asks the client for an address
// (the ROM's "see client window" prompt is only right for this one)
#define PRIVATE_ROOM_INDEX 6

// A server's 16 rooms: the room list's two pages (menu.c), the same eight
// kinds on each, named by their row: NO ITEMS 1, CLASSIC 2, POTION GAME 3,
// BOMBARDMENT 4, ELIMINATION 5, TAG YOU'RE IT 6, NO ITEMS USF 7 and
// CLASSIC USF 8 (USF, Ultra Sacred Fire: the retro rooms' Super Turbo Pads,
// ROOM_IS_RETRO; saffiFlame.c). POTION GAME, BOMBARDMENT, ELIMINATION and
// TAG YOU'RE IT are the game modes (ROOM_IS_MODE; the rounds below MAX_LAPS,
// gameModes.c, the server's Round_*). Page 1 (rooms 0-7) is "SHORTCUTS ENABLED",
// page 2 (rooms 8-15) "SHORTCUTS DISABLED", and ROOM_NO_SHORTCUTS is only a
// name so far too: the tracks keep their shortcuts until their data blocks
// them. The ROM, the client and the server all read the kinds from here.
#define ROOM_PAGE_ROWS 8
#define ROOM_ROW(rn) ((rn) & (ROOM_PAGE_ROWS - 1))
#define ROOM_ROW_NO_ITEMS     0
#define ROOM_ROW_POTION_GAME  2
#define ROOM_ROW_BOMBARDMENT  3
#define ROOM_ROW_ELIMINATION  4
#define ROOM_ROW_TAG          5
#define ROOM_ROW_NO_ITEMS_USF 6
#define ROOM_IS_ITEMS(rn) (ROOM_ROW(rn) != ROOM_ROW_NO_ITEMS && ROOM_ROW(rn) != ROOM_ROW_NO_ITEMS_USF)
#define ROOM_IS_RETRO(rn) (ROOM_ROW(rn) >= ROOM_ROW_NO_ITEMS_USF)
// a game mode's room (gameModes.c, the server's Round_*): POTION GAME,
// BOMBARDMENT, ELIMINATION, TAG YOU'RE IT
#define ROOM_IS_MODE(rn) (ROOM_ROW(rn) >= ROOM_ROW_POTION_GAME && ROOM_ROW(rn) <= ROOM_ROW_TAG)
#define ROOM_NO_SHORTCUTS(rn) ((rn) >= ROOM_PAGE_ROWS)
// The pages of rooms on offer: page 1 only until the shortcutless rooms
// (page 2) are built (Matt, 2026-10-12; their code all stays). The server
// keeps rooms 8-15 shut - closed on every room list and in Quick Play's
// query, a join refused (RoomOpenForJoin, SV_main.c) - and the ROM's room
// list stays on page 1, its page arrows and 1/2 greyed and LEFT/RIGHT dead
// (UpdateMenu, menu.c). 2 opens page 2 again.
#define ROOM_PAGES_OPEN 1
#define ROOM_OPEN_INDEX(rn) ((rn) < ROOM_PAGES_OPEN * ROOM_PAGE_ROWS)
// a room list row's text, "TAG YOU'RE IT - 12/12", with its 0 and the space
// menu.c writes after the count and cuts off beside two digits; and a
// region list row's, "AUSTRALIA  999MS  999 ONLINE" and its 0 (ServerList_Rows,
// home.c), which share the buffer (roomRows, in Mods4.c)
#define ROOM_LABEL_SIZE 29

// The server and room lists' column: the boxes' centre and the lines under
// them. The room list's box, the widest (20-character rows), has its frame
// at x 292-492 here and its shadow to 495; the logo is centred left of it
// (menu.c, logo.c).
#define ONLINE_LIST_X 0x188

// The region list's place (Matt, 1105): its box in the middle of the
// screen, under the logo, which moves to the top in the middle at half its
// size as the list opens (logoDraw.c) while the box slides up from the
// bottom (ServerList_Box, home.c); PLAYERS ONLINE under the box
// (lobbyBusy.c). Its rows are too wide for the column right of the logo.
#define REGION_LIST_X 0x100
#define REGION_LIST_Y 0x74

// Every online screen's button hints - SELECT, BACK, PRIVATE, ABOUT, CANCEL
// - centred on the screen, on one row (y 0xc6), so they stay put from
// screen to screen. Nothing else is on that row since the date stamp moved
// to the About view (about.c).
#define ONLINE_HINTS_X 0x100

// The in-race rank list's columns (rankRow.c), at the row's x at rest (10,
// UI_43_DrawRankedDrivers; the icon RANK_ROW_ICON_DX left of it): the place
// at RANK_PLACE_X1, one digit at the icon's top right, or RANK_PLACE_X2, two
// (01-12, from a ninth kart); the name left-aligned RANK_NAME_GAP after the
// place's last digit, five letters at most, so every name has the same gap
// whatever its length; on one-line rows (9-12 karts) the lap split,
// "m:ss.cc", RANK_SPLIT_GAP after a fifth letter, ending at RANK_SPLIT_RIGHT,
// which the lobby countdown beside the second row keeps clear of (hooks.c).
// RANK_SMALL_W and RANK_SMALL_P: the small font's letter and its ':' and '.'
// (data.font_charPixWidth, font_puncPixWidth). ROM only (WIDE_34).
#define RANK_PLACE_X1    29
#define RANK_PLACE_X2    26
#define RANK_NAME_GAP    6
#define RANK_SPLIT_GAP   6
#define RANK_SMALL_W     WIDE_34(13)
#define RANK_SMALL_P     WIDE_34(7)
#define RANK_NAME_X1     (RANK_PLACE_X1 + RANK_SMALL_W + RANK_NAME_GAP)        // 44
#define RANK_NAME_X2     (RANK_PLACE_X2 + 2 * RANK_SMALL_W + RANK_NAME_GAP)    // 50
#define RANK_SPLIT_X     (RANK_NAME_X2 + 5 * RANK_SMALL_W + RANK_SPLIT_GAP)    // 101
#define RANK_SPLIT_RIGHT (RANK_SPLIT_X + 5 * RANK_SMALL_W + 2 * RANK_SMALL_P)   // 156

// octr->clientCount[room], as the client hands the room list (SG_ROOMS) to
// the ROM: the players in it (0 to MAX_NUM_PLAYERS) plus ROOM_CLOSED when the
// server lets nobody in now (RoomOpenForJoin, SV_main.c). A full room is not
// joinable either. Up to 8 players "closed" was +8 on the count. And
// ROOM_SPECTATE (1110), the char's last bit, unused before: the race in it
// can be watched now - past its green, not over, with a seat that was empty
// at its load (RoomOpenForSpectate, SV_main.c; SG_MessageRooms.spectateMask)
// - which the room list offers as MATCH IN PROGRESS, SPECTATE on a row that
// cannot be joined (hostOptions.c). Every reader of the count masks it out
// (ROOM_PLAYERS, ROOM_JOINABLE).
#define ROOM_CLOSED      0x40
#define ROOM_SPECTATE    0x80
#define ROOM_PLAYERS(c)  ((c) & 0x3F)
#define ROOM_JOINABLE(c) ((((c) & ROOM_CLOSED) == 0) && (ROOM_PLAYERS(c) < MAX_NUM_PLAYERS))
#define ROOM_SPECTATABLE(c) (((c) & ROOM_SPECTATE) != 0)
// a number in a string literal ("/" OCTR_STR(MAX_NUM_PLAYERS))
#define OCTR_STR2(x) #x
#define OCTR_STR(x)  OCTR_STR2(x)

// octr->menuMusic, the launcher's Menu Theme Music as the client hands it
// on: off, the menu's own theme, or MENU_MUSIC_RANDOM + a race track's
// levelID, the client's pick, whose song the lobby then plays (its bank
// levelID + 1, its song levelID: data.levBank_FX, levBank_Song). The ROM
// before Random only tested it against 0, so any pick plays the theme there.
#define MENU_MUSIC_OFF     0
#define MENU_MUSIC_DEFAULT 1
#define MENU_MUSIC_RANDOM  2
#define MENU_MUSIC_TRACKS  18                  // the picks the ROM takes: the race tracks, to TURBO_TRACK
#define MENU_MUSIC_BANK    0x20                // the menu theme's own bank (Music_AsyncParseBanks)

 // 2 seconds to be very tolerant on client
#ifdef USE_60FPS
#define DISCONNECT_AT_UNSYNCED_FRAMES   120
#else
#define DISCONNECT_AT_UNSYNCED_FRAMES   60
#endif

// Five seconds from everybody locked in to the load, counted down on the
// idle clock's rows with the lobby still on screen (StatePS1_Lobby_StartLoading,
// states.c); the lobby's panels leave over its last second (kartView.c).
// Lobby logic frames, 30 a second (interp.c); CountPressX is an unsigned
// char, so keep this under 255.
#define START_COUNTDOWN_SECS   5
#define START_COUNTDOWN_FRAMES (START_COUNTDOWN_SECS * 30)

enum ClientState
{
	LAUNCH_ENTER_PID,
	LAUNCH_PICK_SERVER,
	LAUNCH_PICK_ROOM,
	LAUNCH_ERROR,
	LOBBY_ASSIGN_ROLE,
	LOBBY_HOST_TRACK_PICK,
	LOBBY_GUEST_TRACK_WAIT,
	LOBBY_CHARACTER_PICK,
	LOBBY_WAIT_FOR_LOADING,
	LOBBY_START_LOADING,
	GAME_WAIT_FOR_RACE,
	GAME_START_RACE,
	GAME_END_RACE,
	NUM_STATES
};

// The longest name, on the wire (CG_MessageName, SG_MessageNewClient) and in
// nameBuffer; the launcher's field holds exactly this many (ui.cpp, c_nameLen).
// Was 9 up to build 1039: a change here is a protocol change (SERVER_VERSION).
#define NAME_LEN 7
#define MAX_NUM_PLAYERS 12
// A remote slot's controller, which the PC client writes the slot's buttons
// into: the game's gamepad[slot] (0x80096804, 0x50 apart) for slots 1-7; for
// 8-9 the same + 0x1000, and for 10-11 + 0x1020, in the game tracker's
// unused 2P-4P pixel-LOD area (gGT + 0xF64, + 0xFB4, + 0x1024, + 0x1074).
// The ROM reads it as DRIVER_PAD (include/common.h, ONLINE_PAD_EXTRA).
#define ONLINE_PAD_ADDR(slot) (0x80096804u + (unsigned int)(slot) * 0x50u + \
	((((unsigned int)(slot) & 8u) << 9) + ((((unsigned int)(slot) & 8u) & ((unsigned int)(slot) << 2)) << 2)))

// Weapons online (netWeapons.c). A weapon (a placed one: TNT, Nitro, a
// beaker; a projectile: missile, bomb, thrown shield) is known everywhere
// by its shooter's client ID and that shooter's shot count
// (ShotPose.serial): its identity, 12 bits. Every machine tests its own
// kart only against them, and what happens to one travels as an event,
// applied the same way on every other machine. The server keeps each
// identity's fate and relays only the first claim on it (SV_main.c,
// NET_EV_FINAL_MASK below): two machines that each saw their own kart take
// the same weapon end up agreeing on the one the server heard first.
#define NET_EV_DESTROYED  0   // gone to something other than a kart; arg 0 quietly (a missile, a hazard: RB_Hazard_ThCollide_Generic), 1 blown up (RB_GenericMine_ThDestroy: off the world, a full pool, spawned in a hitbox), 2 blown up at Tiger Temple's door, which it opened
#define NET_EV_POTION     1   // the sender's kart drove into a beaker; arg bit 0 it was moving (hurt reason 4), bit 1 the hurt took
#define NET_EV_TNT_ONHEAD 2   // the sender's kart drove into a TNT, which jumps onto its head
#define NET_EV_MINE_BLAST 3   // the sender's kart was blasted by a TNT or Nitro; arg 0 a Nitro, 1 it was squished, 2 it already had a TNT on its head (that one went off first: NET_EV_HEAD_BOOM arg 2)
#define NET_EV_MINE_SPENT 4   // the sender's kart drove into a TNT its shield, mask or invincibility stopped: the TNT just goes off
#define NET_EV_HEAD_BOOM  5   // the TNT on the sender's head went off; arg 0 its time ran out (the kart blasted), 1 the kart crashed or spun, 2 the kart was already blasted
#define NET_EV_HEAD_OFF   6   // the sender hopped the TNT off its head
#define NET_EV_PROJ_END   7   // a projectile ended; arg 0 it hit the sender's kart (on the sender's machine), 1 it went off against anything else on its shooter's machine (a wall, a mine, a bomb set off), 2 it went quietly (a missile an explosion took out on its shooter's machine: RB_Hazard_ThCollide_Missile; a shield its shooter's machine had no room to throw), 3 (1109) a held shield bumped off on its shooter's machine (VehPhysCrash_Attack), whoever the bump hurt: the others pop their copy's
#define NET_EV_KART_HURT  8   // the sender's kart was hurt, by anything (VehPickState_NewState): no weapon, the identity's shooter field is the attacker's client ID and its count bit 0 set with an attacker, bits 1-4 the hurt's number (NET_HURT_SEQ, 1103: each kart's hurts counted, 1-15 round again, as its kart message's hurt says them too); arg bits 0-2 the damage type, 3-5 the reason (NET_HURT_SHIELD_BUMP: the attacker's shield's bump, 1109), bit 6 a shield stopped it (and popped)
#define NET_EV_DOOR       9   // Tiger Temple's door opened on the sender's machine (by anything but this event): it opens everywhere; no weapon, identity 0
#define NET_EV_LAP        10  // ELIMINATION, ROM -> server only: the sender's kart reached lap arg (drivers[0]->lapIndex just raised); identity 0; the server takes it and relays nothing
#define NET_EV_OUT        11  // server -> clients only: a player placed - clientID the player (0xF none: a round's end alone, with ROUND_LAST; or, no cause and not last, the "round off" marker: under 2 players at the green, a plain race), owner the attacker (0xF none), serial ROUND_CAUSE_* | ROUND_LAST, arg the place (0xF none)
#define NET_EV_IT         12  // server -> clients only (TAG): clientID who is it (0xF none), owner the tagger (0xF none), serial bit 0 the clock running, arg its seconds left
#define NET_EV_LIVES      13  // server -> clients only (BOMBARDMENT): clientID lost a life and is still in, owner the attacker (0xF none), arg the lives left (1 to BOMB_LIVES - 1); the last life's loss is NET_EV_OUT
#define NET_EV_KART_CLAIM 14  // 1107: the sender's masked kart bumped another player's on the sender's screen - or, 1109, its shield did, mask or not, or, 1110, it squished it (VehPhysCrash_Attack's squish: Saffi or the TURBO item's fire), mask or not; no weapon, the identity's shooter field is the bumped kart's client ID and its count the hit VehPhysCrash_Attack gave it there (NET_CLAIM_HIT: the mask's, type 2 reason 6; the sender's own shield bumped off on it, 2 and 0; a squish, 3 and 5: 1110, mask or not, by a kart with Saffi or the TURBO item's fire). The server sends it to that player alone, arg the two players' round trips added, in 4 ms (ROM -> server: 0); that kart's machine judges it (NetKart_ClaimApply, netWeapons.c) and hurts its kart as the game would, which goes out as its NET_EV_KART_HURT
#define NET_EV_COUNT      15
#define NET_CLAIM_HIT(type, reason) ((type) | ((reason) << 3))   // NET_EV_KART_CLAIM's count
// the kinds only the server sends: refused from a client
#define NET_EV_SERVER_MASK ((1 << NET_EV_OUT) | (1 << NET_EV_IT) | (1 << NET_EV_LIVES))
// For the server: the kinds that end an identity (the first one relayed,
// any later one dropped), the kinds only the kart carrying it may send, and
// NET_EV_TNT_ONHEAD, which hands it to a head; NET_EV_KART_HURT has no
// identity and always goes through
#define NET_EV_FINAL_MASK   ((1 << NET_EV_DESTROYED) | (1 << NET_EV_POTION) | (1 << NET_EV_MINE_BLAST) | (1 << NET_EV_MINE_SPENT) | (1 << NET_EV_PROJ_END))
#define NET_EV_CARRIER_MASK ((1 << NET_EV_HEAD_BOOM) | (1 << NET_EV_HEAD_OFF))
// the kinds with no weapon (always relayed, every room), and those that go
// in a room without items too (its bombs, shared as ghosts, end there)
#define NET_EV_NOID_MASK    ((1 << NET_EV_KART_HURT) | (1 << NET_EV_DOOR))
#define NET_EV_ANYROOM_MASK (NET_EV_NOID_MASK | (1 << NET_EV_PROJ_END))
#define NET_KART_BLOCKED  0x40   // NET_EV_KART_HURT's arg: a shield stopped it
// NET_EV_KART_HURT's damage type for a hit a mask or invincibility stopped
// (1109), always with NET_KART_BLOCKED: nothing happens to the kart, its
// machine laughed (VehPickState_NewState) and the others laugh with it. 6, a
// type the game never gives (it knows 0-5); never a hurt to a game mode's
// referee (SV_main.c, Round_OnHurt: blocked).
#define NET_KART_DODGED   6
// ...and 7 (1109), always with NET_KART_BLOCKED and NET_HURT_SHIELD_BUMP: a
// shield's bump on a kart it did nothing to (blasted or tumbling already),
// told for the bumper's shield alone - every machine pops it; no voice, no hurt
#define NET_KART_BUMPED   7
// NET_EV_KART_HURT's reason for a kart bumped by another's shield (1109):
// the game's own is 0 (VehPhysCrash_Attack), which a TNT going off on a head
// gives too; 7, a reason the game never gives (VehPickState_NewState knows
// 1-6), says the bumper's shield went in the bump - every machine pops it
// (NetKart_Replay; the bumper's own too, when only the bumped kart's screen
// saw the bump) and hurts the kart with reason 0. In the kart message's
// hurt word too (its 3 reason bits).
#define NET_HURT_SHIELD_BUMP 7
// one event in an int: bits 0-11 the identity (count, then the shooter's
// client ID at 8-11), 12-16 the kind, 17-24 its argument, and on the way in
// 25-28 the sender's client ID
#define NET_ID_MASK       0xfff
#define NET_ID_OWNER(id)  (((id) >> 8) & 0xf)
#define NET_EV_PACK(id, kind, arg) (((id) & NET_ID_MASK) | (((kind) & 0x1f) << 12) | (((arg) & 0xff) << 17))
#define NET_EV_ID(ev)     ((ev) & NET_ID_MASK)
#define NET_EV_KIND(ev)   (((ev) >> 12) & 0x1f)
#define NET_EV_ARG(ev)    (((ev) >> 17) & 0xff)
#define NET_EV_SENDER_SHIFT 25
#define NET_EV_SENDER(ev) (((ev) >> NET_EV_SENDER_SHIFT) & 0xf)
// on the way out, bit 29: a placed weapon's claim whose kart waits on the
// server's word before it is hurt (a contested touch: Net_Pend,
// netWeapons.c) - the client sends it as CG_MessageWeaponEvent.tentative
#define NET_EV_TENTATIVE  (1 << 29)
#define NET_EV_RING 32

typedef struct raceStats
{
	int slot;
	int finalTime;
	int bestLap;
} raceStats;

// One shot's pose (ShotPose in struct OnlineCTR, which says what each
// field is). Shorts and chars, naturally aligned: 16 bytes on both
// compilers. Out here, not inside OnlineCTR, so that C and C++ both know it
// by this name.
struct OnlineShotPose
{
	short pos[3];
	short angle;
	short fwd[2];
	unsigned char serial;
	unsigned char held;
	unsigned char target;
	unsigned char padding;
};

// This can be 0x400 (1024) bytes max:
// 0x8000C000 to 0x8000C400
struct OnlineCTR
{
	// 0x0
	int CurrState;

	// 0x4
	char PageNumber; // allow negative
	unsigned char CountPressX;
	unsigned char NumDrivers;
	unsigned char DriverID;

	// 0x8
	unsigned char boolLockedInLap;
	unsigned char boolLockedInLevel;
	unsigned char lapID;
	unsigned char levelID;

	// 0xC
	unsigned char IsBootedPS1;
	unsigned char boolLockedInCharacter;
	unsigned char numRooms;
	unsigned char numDriversEnded;

	// 0x10
	unsigned char serverCountry;
	unsigned char serverRoom;
	unsigned char serverLockIn1;
	unsigned char serverLockIn2;

	// 0x14
	unsigned char boolPlanetLEV;
	unsigned char boolClientBusy;

	// OCTR_SPECIAL_SPECTATOR (1110) while this machine watches a race it
	// joined in progress: the client's, from a spectator's SG_NEWCLIENT
	// (NEWCLIENT_SPECTATOR), 0 again at the room reset's ordinary one. The
	// ROM's spectate.c parks and hides its own kart while it is set. Was the
	// weekday events' byte, which no server sends.
	unsigned char special;

	// Which client ID currently owns the lobby (track/lap picking).
	// Was the dead "padding" byte, so every field after this keeps its offset -
	// deliberate, because this struct is read at a fixed address by both the
	// MIPS build and the x86 client and any shift would desync them silently.
	// 0xFF means "no host yet".
	unsigned char hostID;

	// 0x18
	char clientCount[16];

	// 0x28
	// determines if client and
	// emulator are still connected
	char windowsClientSync;

	// 0x29
	char boolLockedInCharacters[MAX_NUM_PLAYERS];

	// 0x29 + MAX_NUM_PLAYERS
	char nameBuffer[MAX_NUM_PLAYERS][NAME_LEN + 1]; //+1 for nullterm

	raceStats raceStats[MAX_NUM_PLAYERS];

	int ver_psx;
	int ver_pc;
	int ver_server;

	// slot[0] is for game to tell client to send
	// slot[1+] is for client to tell game to shoot
	struct
	{
		unsigned char boolJuiced;
		unsigned char Weapon;
		unsigned char flags;
		unsigned char boolNow;
	} Shoot[MAX_NUM_PLAYERS];

    // Frames that the client didn't update
    int frames_unsynced;

    // Last windowsClientSync counter
	char lastWindowsClientSync;

	// 0x169 (1110), in what was the alignment gap up to readyToSend: the
	// spectators watching this race - players who joined it in progress,
	// caught up (spectate.c on their machines) - 0 to MAX_NUM_PLAYERS, from
	// the server's SG_LOBBYCOUNTDOWN with LOBBYCOUNT_WATCHERS, sent as one
	// starts watching and as one leaves. Written by the client only;
	// cleared at SG_NEWCLIENT, SG_STARTLOADING and a drop, not with the
	// round (Round_Clear also runs at the green). The ROM reads it for the
	// racers' WATCHERS on the HUD (playerCount.c), shown while it is 1 or
	// more.
	unsigned char watchers;

	// 0x16A (1110), the same gap: the other players' karts this machine's
	// client holds braked, SQUARE on their controllers - bit n for KART SLOT
	// n (1 to MAX_NUM_PLAYERS - 1): one over the line whose CG_ENDRACE had no
	// ENDRACE_BOT (not handed to the bot driver: a round's out, END or TAG
	// kart, a track with no nav path), a finished player's bot whose kart
	// messages stopped (the client's watchdog, a second without one: its
	// player left or crashed), or a player who left. The client's
	// g_slotEnded | g_botStalled, written by the client only, in one 16-bit
	// store each time either changes; 0 with the round (Round_Clear). The ROM
	// reads it (Net_Parked, netWeapons.c): a copy over the line without its
	// bit is driven on by its player's bot - also one over the line on this
	// screen before its SG_ENDRACE came - and is a target as a racer is.
	unsigned short copyParked;

	// when to start the client.exe loop
	int readyToSend;

	int autoRetryJoinRoomIndex;

	// Round-trip time to the server in ms, written by the PC client from
	// ENet's smoothed estimate once its ping warmup has converged; 0 while
	// disconnected or still warming up, so the ROM draws nothing rather than
	// a placeholder. Appended at the END of the struct on purpose: every
	// existing offset is untouched, and an int following an int has no
	// padding for the MIPS and x86 compilers to disagree about.
	int latencyMs;

	// Seconds until the server resets the room after a race, counted down by
	// the PC client from the server's SG_LOBBYCOUNTDOWN; 0 = none running.
	// Same placement rule as latencyMs: appended, int after int, no padding.
	int lobbyCountdownSecs;

	// TRIANGLE (back) in the online menus is decided by the ROM, screen by
	// screen (Lobby_OnTriangle, states.c). What it cannot do itself it asks
	// the PC client for here (BACK_*), and the client clears it once done.
	// Same placement rule: appended, int after int, no padding.
	int backRequest;

	// The launcher's options, written by the PC client every frame from its
	// command line (--music, --sfx, --menu-music; CL_main.cpp), so they
	// survive the entry hook's memset: the music and sound effect volumes
	// as percentages, what plays in the menus (MENU_MUSIC_*, below), and a
	// flag that says the three are in, since zeros would otherwise read as
	// silence. Applied by the ROM every frame (Options_Apply, options.c).
	// Same placement rule: appended, ints after ints, no padding.
	int musicVolume;
	int sfxVolume;
	int menuMusic;
	int optionsSet;

	// Players online across every server, written by the PC client every
	// frame from the site's status feed (PlayersOnline_Tick, CL_main.cpp),
	// for PLAYERS ONLINE under the server list (lobbyBusy.c). -1 until the
	// first answer, and when the site cannot be reached: the ROM prints ...
	// for it. Same placement rule: appended, int after int, no padding.
	int playersOnline;

	// Weapons online (netWeapons.c). Each shot's pose, beside Shoot[]
	// (which cannot grow where it is): the shooter's kart position (its
	// instance's matrix.t, world units) and facing at the shot (the kart's
	// heading, driver->rotCurr.y, for a missile or bomb, which flies along
	// it; driver->angle for the rest), the kart's forward column (matrix
	// m[0][2], m[2][2], for a beaker thrown forward and a thrown shield),
	// its d-pad UP (bit 0) and DOWN (bit 1), its shot count - with its
	// client ID, the identity of what it places - and a missile's or bomb's
	// target, client ID + 1 (0 none). A held shield's release sends the
	// shield's count again, with the pose at the release.
	// Slot 0 is the local shot, for the client to send; 1 to MAX_NUM_PLAYERS - 1 the others',
	// written by the client before it sets Shoot[slot].boolNow (struct
	// OnlineShotPose, above).
	struct OnlineShotPose ShotPose[MAX_NUM_PLAYERS];

	// What happens to weapons and karts (NET_EV_*), one packed int each
	// (NET_EV_PACK). Out: this ROM's, for the client to send - the ROM
	// writes an entry, then advances evOutHead; the client sends up to it
	// and sets evOutTail. In: everyone else's - the client writes an entry
	// and advances evInHead; the ROM applies up to it and sets evInTail.
	// Each index has one writer. Ints, no padding.
	int evOut[NET_EV_RING];
	int evOutHead;
	int evOutTail;
	int evIn[NET_EV_RING];
	int evInHead;
	int evInTail;

	// Quick Play (home.c on the ROM, the QuickPlay_* functions of
	// CL_main.cpp). quickPlayCmd is the ROM's, a QP_CMD_* the client takes
	// and puts back to QP_CMD_NONE. The rest are the client's, written
	// every loop (the entry hook's memset): the search's QP_* stage, and
	// the game it found - its server (an octr->serverCountry number) and
	// its room. Same placement rule: appended, ints after ints.
	int quickPlayCmd;
	int quickPlayState;
	int quickPlayCountry;
	int quickPlayRoom;

	// Players on each server by its octr->serverCountry number, from the
	// site's status feed (PlayersOnline_Tick, CL_main.cpp), beside each
	// region on the server list (ServerList_Rows, home.c). -1 where the
	// feed has nothing to say (not fetched yet, the site unreachable, the
	// private room, Beta). Written by the client every loop.
	int serverPlayers[8];

	// The lobby's OPTIONS (hostOptions.c): the host's CHANGE HOST, anybody's
	// VOTE TO KICK (cast or taken back) at another player of the room,
	// HOST_ACTION_* | (that player's client ID << 8), written by the ROM and
	// taken by the client (HostAction_Service, CL_main.cpp), which sends
	// CG_HOSTACTION. 0 for none.
	int hostAction;

	// From the client when the server removed this player from their room:
	// KICKED_NOTICE_VOTE, the room's vote to kick (DISCONNECT_REASON_KICKED) -
	// the ROM picks the same server straight back, as TRIANGLE out of a
	// room does (thread.c), and says why on its room list - or
	// KICKED_NOTICE_IDLE, the idle clock (DISCONNECT_REASON_IDLE) - said on
	// the server list it lands on. hostOptions.c says it, and clears it.
	int kickedNotice;

	// Each server's round trip in ms by its octr->serverCountry number,
	// beside its players on the server list (ServerList_Rows, home.c): from
	// the client's status queries every few seconds while on the server
	// list state (ServerPing_Tick, CL_main.cpp). -1 while it is being asked
	// with nothing to show yet, PING_NA where the last ask went unanswered
	// (an old server that does not answer the query, one down), PING_UPDATE
	// where the answer named another SERVER_VERSION; -1 too for those never
	// asked: a server not open, the private room. Written by the client
	// every loop.
	int serverPing[8];

	// The room's vote for the track and laps (vote.c), from the server's
	// SG_VOTE by the client: whether the room votes at all (the host's
	// PLAYER VOTING), the vote running (VOTE_*), and each player's vote by
	// client ID (the row's option + 1; 0 none). The track a lap vote is for
	// goes in levelID. voteCast is the ROM's own vote for the client to
	// send (CG_VOTE): VOTE_* << 8 | (option + 1), 0 none.
	int voteEnabled;
	int votePhase;
	int voteCast;
	unsigned char voteBy[MAX_NUM_PLAYERS];

	// This round's track was put in by the room's vote (SG_TRACK's voted),
	// not picked by the host: the host's TRIANGLE on the character pick
	// leaves the room rather than taking the track back (lobbyBack.c) - by
	// how this round's track went in, not by PLAYER VOTING, which the host
	// may have turned the other way for the next round since. Cleared with
	// the track (SG_TRACKCANCEL) and on a new lobby (SG_NEWCLIENT).
	int trackVoted;

	// The game modes' round (gameModes.c), from the server's NET_EV_OUT and
	// NET_EV_IT by the client: each player's place and cause by client ID
	// (ROUND_PLACE, ROUND_CAUSE; 0 still in), who is "it" (client ID + 1, 0
	// none: the boot's memset is safe), the "it" clock's seconds left
	// (counted down by the client), ROUND_FLAG_*. Cleared at SG_STARTLOADING
	// and SG_NEWCLIENT. Bytes: the same layout on MIPS and x86.
	unsigned char roundPlace[MAX_NUM_PLAYERS];
	unsigned char roundIt;
	unsigned char roundItSecs;
	unsigned char roundFlags;
	unsigned char roundPad;
	// BOMBARDMENT: the lives each player has lost (NET_EV_LIVES, by client
	// ID; BOMB_LIVES - this is what is left; 0 at the round's start, which
	// the clear above gives). Cleared with the rest.
	unsigned char roundHits[MAX_NUM_PLAYERS];

	// TAG YOU'RE IT: this machine's "it" has their Saffi on (TRIANGLE:
	// TagFire_Step, gameModes.c; the blue flame, SaffiFlame_Look). The ROM's
	// own: 0 at each race load (GameMode_RaceLoad).
	unsigned char tagFire;

	// The karts' flame colours over the wire (FLAME_*; SaffiFlame_Step and
	// SaffiFlame_Look, saffiFlame.c): this machine's kart's, written by the
	// ROM every race step for the client to send (CG_RACEDATA's flame), and
	// the other karts', two bits each by KART SLOT (FLAME_BY), written by the
	// client alone from SG_RACEDATA's (cleared with the round, Round_Clear).
	unsigned char flameOwn;

	// This player's own votes to kick (1110), bit n for client ID n, as the
	// server last answered (SG_VOTE's VOTE_KICK_REPLY: SG_MessageKickVote's
	// mine) - nobody else's are ever sent. Written by the client only, in one
	// 16-bit store; cleared as a room is joined, at SG_STARTLOADING (the
	// server wipes every vote at the countdown), a seat changing hands and a
	// drop. The ROM reads it (hostOptions.c: the pick's VOTE / REMOVE VOTE,
	// the voted names in red).
	unsigned short kickVotes;

	unsigned int flameBy;

	// This machine's kart's last hurt, for the client to send in every kart
	// message (CG_RACEDATA's hurt: the others hurt their copy of it with that
	// message's position, 1103): NET_HURT_* - its number, the attacker, the
	// damage (NET_EV_KART_HURT's arg). Written by the ROM as it sends the
	// hurt (NetKart_Gate); 0 at each race's wait.
	unsigned int hurtOwn;
};

STATIC_ASSERT2(sizeof(struct OnlineCTR) <= 0x400, "Size of OnlineCTR must be lte 1kb");

// The two compilers (GCC for the ROM, MSVC for the client) must put the
// placed weapons' mailboxes in the same place.
#ifdef __GNUC__
#define OCTR_OFFSETOF(m) __builtin_offsetof(struct OnlineCTR, m)
#else
#include <stddef.h>
#define OCTR_OFFSETOF(m) offsetof(struct OnlineCTR, m)
#endif
// the alignment gap's two fields (1110), between a char and an int
STATIC_ASSERT2(OCTR_OFFSETOF(lastWindowsClientSync) == 0x168, "OnlineCTR layout: lastWindowsClientSync");
STATIC_ASSERT2(OCTR_OFFSETOF(watchers) == 0x169, "OnlineCTR layout: watchers");
STATIC_ASSERT2(OCTR_OFFSETOF(copyParked) == 0x16A, "OnlineCTR layout: copyParked");
STATIC_ASSERT2(OCTR_OFFSETOF(readyToSend) == 0x16C, "OnlineCTR layout: readyToSend");
STATIC_ASSERT2(MAX_NUM_PLAYERS <= 16, "copyParked: a 16-bit mask by kart slot");
STATIC_ASSERT2(OCTR_OFFSETOF(playersOnline) == 0x190, "OnlineCTR layout: playersOnline");
STATIC_ASSERT2(OCTR_OFFSETOF(ShotPose) == 0x194, "OnlineCTR layout: ShotPose");
STATIC_ASSERT2(OCTR_OFFSETOF(evOut) == 0x254, "OnlineCTR layout: evOut");
STATIC_ASSERT2(OCTR_OFFSETOF(evIn) == 0x254 + 4 * (NET_EV_RING + 2), "OnlineCTR layout: evIn");
STATIC_ASSERT2(OCTR_OFFSETOF(evInTail) == 0x254 + 4 * (2 * NET_EV_RING + 3), "OnlineCTR layout: evInTail");
STATIC_ASSERT2(OCTR_OFFSETOF(quickPlayCmd) == 0x364, "OnlineCTR layout: quickPlayCmd");
STATIC_ASSERT2(OCTR_OFFSETOF(serverPlayers) == 0x374, "OnlineCTR layout: serverPlayers");
STATIC_ASSERT2(OCTR_OFFSETOF(hostAction) == 0x394, "OnlineCTR layout: hostAction");
STATIC_ASSERT2(OCTR_OFFSETOF(serverPing) == 0x39C, "OnlineCTR layout: serverPing");
STATIC_ASSERT2(OCTR_OFFSETOF(voteEnabled) == 0x3BC, "OnlineCTR layout: voteEnabled");
STATIC_ASSERT2(OCTR_OFFSETOF(voteBy) == 0x3C8, "OnlineCTR layout: voteBy");
STATIC_ASSERT2(OCTR_OFFSETOF(trackVoted) == 0x3D4, "OnlineCTR layout: trackVoted");
STATIC_ASSERT2(OCTR_OFFSETOF(roundPlace) == 0x3D8, "OnlineCTR layout: roundPlace");
STATIC_ASSERT2(OCTR_OFFSETOF(roundIt) == 0x3E4, "OnlineCTR layout: roundIt");
STATIC_ASSERT2(OCTR_OFFSETOF(roundHits) == 0x3E8, "OnlineCTR layout: roundHits");
STATIC_ASSERT2(OCTR_OFFSETOF(tagFire) == 0x3F4, "OnlineCTR layout: tagFire");
STATIC_ASSERT2(OCTR_OFFSETOF(flameOwn) == 0x3F5, "OnlineCTR layout: flameOwn");
STATIC_ASSERT2(OCTR_OFFSETOF(kickVotes) == 0x3F6, "OnlineCTR layout: kickVotes");
STATIC_ASSERT2(OCTR_OFFSETOF(flameBy) == 0x3F8, "OnlineCTR layout: flameBy");
STATIC_ASSERT2(OCTR_OFFSETOF(hurtOwn) == 0x3FC, "OnlineCTR layout: hurtOwn");
STATIC_ASSERT2(sizeof(struct OnlineCTR) == 0x400, "OnlineCTR layout: size");
STATIC_ASSERT2(sizeof(struct OnlineShotPose) == 16, "OnlineCTR layout: OnlineShotPose size");
#ifdef __GNUC__
STATIC_ASSERT2(__builtin_offsetof(struct OnlineShotPose, target) == 14, "OnlineCTR layout: ShotPose target");
#else
STATIC_ASSERT2(offsetof(struct OnlineShotPose, target) == 14, "OnlineCTR layout: ShotPose target");
#endif
STATIC_ASSERT2((NET_EV_RING & (NET_EV_RING - 1)) == 0, "NET_EV_RING must be a power of two");
STATIC_ASSERT2(NET_EV_COUNT <= 16, "NET_EV_* kinds: 4 bits on the wire, and the server's masks");

// The servers a player can pick, by octr->serverCountry number, which Quick
// Play asks (QuickPlay_ProbeThread, CL_main.cpp): the region list's rows
// not greyed (NewPage_ServerCountry, menu.c, which keeps its own test for
// Mods1's bytes - change the two together). Mexico (2) and Singapore (5)
// have nobody hosting them for now; a beta build has Beta (7) alone, and
// the private room (6) is never a row.
#ifdef ONLINE_BETA_MODE
#define SERVER_OPEN(c) ((c) == 7)
#else
#define SERVER_OPEN(c) ((c) != 2 && (c) != 5 && (c) < 6)
#endif

// The server list state's screens (lobbyView, logo.c; home.c): HOME, the
// first - MAIN MENU: QUICK PLAY, SERVER BROWSER, PRIVATE ROOM, ABOUT - then the region
// list it opens (SERVER BROWSER), Quick Play's search, or, for QUICK PLAY
// with nobody online, the offer to start a game.
#define LOBBY_VIEW_HOME    0
#define LOBBY_VIEW_SERVERS 1
#define LOBBY_VIEW_QUICK   2
#define LOBBY_VIEW_NOGAMES 3   // QUICK PLAY with nobody online: NO GAMES AVAILABLE, START ONE?

// octr->quickPlayCmd, the ROM to the client
#define QP_CMD_NONE   0
#define QP_CMD_START  1   // search from the first tier: QUICK PLAY, and RETRY on NO GAMES FOUND.
#define QP_CMD_YES    2   // wait for a seat in the full game found
#define QP_CMD_NO     3   // not that one: search again from the first tier, without it
#define QP_CMD_CANCEL 4   // stop, and drop any connection: back to the home screen

// octr->quickPlayState, the client to the ROM. The four searches are in
// tier order, each taking in the servers that answer within its time.
#define QP_IDLE        0
#define QP_SEARCH_30   1   // SEARCHING FOR GAMES <30MS
#define QP_SEARCH_80   2   // <80MS
#define QP_SEARCH_140  3   // <140MS
#define QP_SEARCH_MORE 4   // >140MS
#define QP_NONE_FOUND  5   // NO GAMES FOUND.
#define QP_FULL_FOUND  6   // FULL GAME FOUND! WAIT TO JOIN?
#define QP_BUSY_FOUND  7   // a game mid-race (its room closed): GAME IN PROGRESS! WAIT TO JOIN?
#define QP_WAITING     8   // WAITING TO JOIN <SERVER> <ROOM>
#define QP_JOINING     9   // a seat free: JOINING <SERVER> <ROOM>

// A ping not in yet, wherever one is shown (the server list, the room
// list, the lobby's SERVER line): nothing, ".", "..", "..." in turn, a step
// every third of a second (logic steps: FPS_HALF of the drawn frames), as
// long as it is awaited. Where the dots are centred, they are placed as
// "..." is, so nothing moves while they count.
#define PING_WAITING_DOTS (&"..."[3 - (FPS_HALF(sdata->frameCounter) / 10) % 4])

// ...and N/A in red where the dots were for a ping that has not come: on
// the server list, a status query gone unanswered (octr->serverPing,
// PING_NA, from the client, which asks again every 5 seconds, the dots
// back while it does); for the connection's ping (octr->latencyMs), one
// awaited PING_NA_SECONDS, by the ROM's own count of the logic steps it
// has waited (pingWaitSteps, logo.c).
#define PING_NA_SECONDS 15
#define PING_NA_STEPS   (PING_NA_SECONDS * 30)
#define PING_NA         (-2)

// ...and UPDATE REQUIRED in red, in place of the players and the round trip, on
// the server list, for a server whose answer to the status query names
// another SERVER_VERSION than this client's (Matt, 1105): the two cannot
// play together until one of them updates (octr->serverPing, from the
// client's ServerPing_Tick; home.c ServerList_Rows). It stays up while the
// next query is out, rather than the waiting dots.
#define PING_UPDATE     (-3)

// octr->backRequest
#define BACK_NONE          0
#define BACK_LEAVE_ROOM    1  // drop the connection; the ROM re-picks the same server, so the room list comes back
#define BACK_LEAVE_SERVER  2  // drop the connection; back to the server list
#define BACK_CANCEL_TRACK  3  // host in the character pick: CG_TRACKCANCEL, the whole room goes back to the lap pick
#define BACK_CANCEL_CONNECT 4 // the private room's address prompt: the client takes it down (and drops a connection it had begun); back to the server list

// octr->kickedNotice
#define KICKED_NOTICE_VOTE 1   // KICKED BY VOTE, on the room list it rejoins (KICKED_NOTICE_HOST, the host's KICK PLAYER, up to 1109)
#define KICKED_NOTICE_IDLE 2   // KICKED FOR IDLING, on the server list

// octr->special (1110)
#define OCTR_SPECIAL_SPECTATOR 1   // watching a race joined in progress (NEWCLIENT_SPECTATOR): the own kart parked off the track, hidden and finished, the camera on the racers (spectate.c)

// octr->hostAction and CG_MessageHostAction.action: the lobby's OPTIONS
#define HOST_ACTION_NONE 0
#define HOST_ACTION_GIVE 1   // CHANGE HOST (the host's): the room is that player's to run
// 2: KICK PLAYER, the host's instant kick (1101-1109) - gone in 1110 for the
// room's vote; never sent, and the server refuses it
// A player the room votes out is kept out of it for a while. How long: the
// server keeps the kicked address out of that room this long
// (SV_main.c KickBan_*), and the client takes the site's count for that
// server as long (its status answers leave the room out: ServerLive_Kicked,
// CL_main.cpp). The server's --kickban changes its own only, for tests.
#define KICK_BAN_SECONDS 300
#define HOST_ACTION_VOTING 3 // PLAYER VOTING: on (the target byte 1) or off (0) for the room - its players vote the track and laps, or the host picks
#define HOST_ACTION_KICKVOTE 4    // VOTE TO KICK (anybody, 1110): this player's vote against that one
#define HOST_ACTION_KICKUNVOTE 5  // ...taken back (the same player picked again)
// The votes a kick needs by the players in the room (0 to MAX_NUM_PLAYERS,
// the one voted against among them; nobody votes against themselves): 0,
// no vote to kick - in a room of one or two players (Matt, 1110: VOTE TO
// KICK only from three players on, greyed below that).
#define KICK_VOTES_BY_PLAYERS 0, 0, 0, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10
STATIC_ASSERT2(MAX_NUM_PLAYERS <= 16, "kick votes: a 16-bit mask a voter");

// Sizes checkpointTimes (Mods7.c): must cover the largest entry in
// LAP_COUNT_BY_ID, or the split tracker writes past the array from lap 8.
#define MAX_LAPS 20

// ---------------------------------------------------- the game modes' rounds
//
// A round runs in a mode room (ROOM_IS_MODE) when 2 or more players are
// seated as the race loads; it is refereed from the green light on. A player
// is IN until placed: OUT (a cause below, placed from the bottom), FINISHED
// (the last lap's line, placed from the top) or END (still in when the round
// ended: the last one standing takes the best place left - 1st if nobody
// finished). The round ends the moment 1 or none are still in. Matt's rules
// (2026-10-12):
//   - POTION GAME: ? boxes give a beaker only; a beaker's hurt (driven into,
//     or touched while thrown - your own too) puts you out. Laps as voted.
//   - BOMBARDMENT: ? boxes give a single bomb only; a bomb's blast costs a
//     life, of BOMB_LIVES (Matt, 1102), and the last puts you out (your own
//     never hurts you: the game's rule). Laps as voted.
//   - ELIMINATION: normal items; each lap the last across is out (as soon as
//     all the others still in have crossed). Laps = players at the start
//     minus 1, so the final lap decides it; the HUD counts down "n LEFT".
//   - TAG YOU'RE IT: ? boxes give a single missile only, and to "it" alone
//     (Matt, 1102: anybody else drives through them, and loses a missile
//     held when "it" passes on); a random first "it";
//     "it" hitting someone with a missile passes "it" on; whoever has been
//     "it" for TAG_SECONDS straight is out (the clock restarts on every tag),
//     and a new random "it" follows. Endless laps. "It" alone has Saffi on
//     TRIANGLE (Matt, 1102: a tap on, a tap off; the sacred fire's speed and
//     the blue flame on the reserves they have, none added - off when they
//     run out, on the brake, a wall, or no longer "it": TagFire_Step,
//     gameModes.c). The blue shows on every screen (CG/SG_RACEDATA's
//     flame, saffiFlame.c).
// A player who leaves mid-round is out with no place (ROUND_CAUSE_LEFT).
#define ROUND_CAUSE_POTION 1
#define ROUND_CAUSE_BOMB   2
#define ROUND_CAUSE_LAST   3   // ELIMINATION: last across a lap
#define ROUND_CAUSE_TIME   4   // TAG: "it" when the clock ran out
#define ROUND_CAUSE_LEFT   5   // left mid-round: no place
#define ROUND_CAUSE_FINISH 6   // crossed the last lap's line
#define ROUND_CAUSE_END    7   // still in when the round ended (place 1: the winner)
#define ROUND_IS_OUT(c)    ((c) >= ROUND_CAUSE_POTION && (c) <= ROUND_CAUSE_LEFT)
#define ROUND_LAST         0x80   // NET_EV_OUT's serial: this verdict ends the round
#define ROUND_NONE         0xF    // NET_EV_OUT / NET_EV_IT: no player, no place

#define BOMB_LIVES    3

// A kart's hurt in its kart message (CG/SG_EverythingKart.hurt, octr->hurtOwn,
// 1103): bits 0-6 the damage (NET_EV_KART_HURT's arg), bit 7 there was an
// attacker, 8-11 the attacker's client ID, 12-15 the hurt's number (1-15
// round again; 0 none yet). NET_EV_KART_HURT's count carries the same number
// (bits 1-4) and the attacker bit (bit 0), so a hurt heard both ways is
// played once (CL_main.cpp: Hurt_Newer).
#define NET_HURT_ATTACKER 0x80
#define NET_HURT_SEQ(count) (((count) >> 1) & 15)

// A kart's flame colour on the wire (CG/SG_RACEDATA's flame, octr->flameOwn
// and flameBy; saffiFlame.c): the game's orange, Saffi's blue, Ultra Sacred
// Fire's green. Two blues, drawn the same (bit 0 set): FLAME_SAFFI for
// Saffi itself, wrapped reserves, whose kart squishes every kart it rams
// (1110: actionsFlagSet 0x200, SaffiFlame_Step), and FLAME_BLUE for TAG
// YOU'RE IT's "it" Saffi (TRIANGLE, octr->tagFire), which squishes nobody -
// the others' screens tell the two apart by this alone (a copy's reserves
// are 200 under both).
#define FLAME_GAME    0
#define FLAME_BLUE    1
#define FLAME_GREEN   2
#define FLAME_SAFFI   3
#define FLAME_BY(slot) ((octr->flameBy >> (2 * (slot))) & 3)
#define TAG_SECONDS   60
#define TAG_LAPS      127   // numLaps is a signed char: endless enough (a round lasts at most players x TAG_SECONDS)
// SG_STARTLOADING's header nibble (padding), the round code: 0 no round (a
// plain race: not a mode room, or under 2 players), 1-13 a round over that
// many laps (ELIMINATION: players - 1), ROUND_CODE_VOTED a round over the
// voted laps (POTION GAME, BOMBARDMENT), ROUND_CODE_ENDLESS a round over
// TAG_LAPS (TAG). The client applies it in mode rooms only.
#define ROUND_CODE_VOTED   14
#define ROUND_CODE_ENDLESS 15

// octr->roundPlace[client]: 0 still in; else the place in bits 0-3 (1-12,
// ROUND_NONE for a leaver) and the ROUND_CAUSE_* in bits 4-6
#define ROUND_PLACE(v)     ((v) & 0xF)
#define ROUND_CAUSE(v)     (((v) >> 4) & 7)
// octr->roundFlags
#define ROUND_FLAG_IT_RUNNING 1   // TAG: the clock is running (from the green)
#define ROUND_FLAG_OVER       2   // the round has ended (its last verdict in)
#define ROUND_FLAG_ON         4   // this race is a round (SG_STARTLOADING's code non-zero)

// A kart's lap, behind-the-line flag and progress in one word (1104;
// netLap.c), as its own screen has them: the other machines rank their copy
// of it by these, not by their own reading of its positions - which a
// crossing missed in a gap or an odd stretch of track used to leave a lap out
// until it finished. Bits 0-19 driver->distanceToFinish_curr, signed
// (VehLap_UpdateProgress's % takes it under 0 just past a node; a track is at
// most 8 * 0xFFFF, the restart points' unsigned short, so 20 bits hold it),
// 20 the behind-the-line flag (actionsFlagSet 0x1000000), 21-27 lapIndex
// (to TAG_LAPS), 31 a word is in (0: none yet). One aligned word, written in
// one store and read in one load each way, so a lap is never paired with
// another step's progress: at the line that would be a whole lap out.
#define NET_LAP_IN     0x80000000u
#define NET_LAP_BEHIND 0x00100000u
#define NET_LAP_PACK(lap, behind, dist) (NET_LAP_IN | (((unsigned int)(lap) & 0x7fu) << 21) | \
	((behind) ? NET_LAP_BEHIND : 0u) | ((unsigned int)(dist) & 0xfffffu))
#define NET_LAP_LAP(w)  ((int)(((w) >> 21) & 0x7f))
#define NET_LAP_DIST(w) (((int)((w) << 12)) >> 12)
// The words, one a kart slot, in the game tracker's free 2P-4P pixel-LOD
// area (include/common.h): slot 0 this machine's kart's, written by the ROM
// each race step (NetLap_Step) for the client to send (CG_RACEDATA's lap);
// 1 to MAX_NUM_PLAYERS - 1 the others', written by the client alone from
// SG_RACEDATA's and read by the ROM alone. All zeroed by the ROM at each
// race's load (NetLap_RaceLoad), before the client can write any.
#define NET_LAP_ADDR(slot) (0x80096B20u + 0x12C4u + 4u * (unsigned int)(slot))
STATIC_ASSERT2(0x12C4 + 4 * MAX_NUM_PLAYERS <= 0x136C, "NET_LAP words run into DecalMP[11]'s FinalizeInit word (include/common.h)");
STATIC_ASSERT2(TAG_LAPS <= 0x7f, "NET_LAP_PACK holds 7 bits of lap");

// From gameModes.c (ModsC), for the HUD and the other ModsC files (ROM only;
// all 0 / -1 outside a round): the room row of this race's round (-1 none);
// kart slot i's player out / placed (any cause) / their ROUND_CAUSE_* (0
// still in) / their place (0 none); the kart slot that is "it" (-1 none);
// the players still in.
int GameMode_Row(void);
int GameMode_SlotOut(int slot);
int GameMode_SlotPlaced(int slot);
int GameMode_SlotCause(int slot);
int GameMode_SlotPlace(int slot);
int GameMode_ItSlot(void);
int GameMode_InCount(void);
// BOMBARDMENT: this machine's player's lives left (BOMB_LIVES down to 1; -1
// outside a BOMBARDMENT round, or once out)
int GameMode_LivesLeft(void);
// and a missile's or bomb's target for this machine's kart d, the out karts
// left out (NetShot_Target)
struct Driver* GameMode_PickTarget(struct Driver* d);
// VehFire_Increment's ghost tape call for this machine's kart's every boost
// (exe): TAG YOU'RE IT's Saffi takes it while on, else the ghost tape does
void TagFire_Boost(int reserves, unsigned char type, int fireLevel);
#define CPS_PER_LAP 2

typedef struct TotalTime
{
	int hours;
	int minutes;
	int seconds;
	int miliseconds;
} TotalTime;

void ElapsedTimeToTotalTime(TotalTime * totalTime, int elapsedTime);

typedef struct CheckpointTracker
{
	int timer;
	unsigned short currCheckpoint;
	unsigned char raceFinished;
	unsigned char drawFlags;
	char displayTime[10];
} CheckpointTracker;

extern CheckpointTracker checkpointTracker[MAX_NUM_PLAYERS];

void EndOfRace_Camera();
void EndOfRace_Icons();
// Spectator camera (endOfRaceUI.c): the slot being followed, whether a slot
// is still racing, and the move to the next slot that is.
extern int currCam;
int Spectate_IsRacing(int driverID);
int Spectate_NextRacer();

// The spectator who joined a race in progress (spectate.c, 1110), each race
// step after NetWeapons_Step (Online_RemoteEngines, engineAudio.c): while
// octr->special is OCTR_SPECIAL_SPECTATOR, this machine's own kart parked
// off the track, hidden and finished without the bot, the fly-in skipped,
// and the spectating above handed the camera.
void Spectator_Step(void);

// This machine's kart driven on by the bot past its line (botFinish.c,
// 1110; BOTS_Driver_Convert, drivers[0] alone online). BotNet_Gate, from
// the top of BOTS_ChangeState (the exe, which has no room for global.h and
// declares it itself): a hurt on it sent to the other machines as
// NetKart_Gate sends a racer's - 0 refuse it, 2 go on as the game.
// BotNet_Carry, from BOTS_Driver_Convert, last: what the kart brings over
// the line - Saffi's reserves, ended before the bot's first tick; the hurt
// it crossed the line in, which went out as it took, handed to the bot
// unsent; a hit deferred in the crossing step, taken and sent the bot's way.
// BotFinish_Step, each race step before NetWeapons_Step (engineAudio.c): the
// invincibility and invisibility a bot's kart keeps, which only the player's
// physics count down.
int BotNet_Gate(struct Driver* v, int type, struct Driver* attacker, int reason);
void BotNet_Carry(struct Driver* d);
void BotFinish_Step(void);

#ifdef WINDOWS_INCLUDE

#include <time.h>

#ifndef __GNUC__
	#include <windows.h>
#endif

enum ServerGiveMessageType
{
	// connection
	SG_ROOMS,

	// assign to room
	SG_NEWCLIENT,

	// lobby
	SG_NAME,
	SG_TRACK,
	SG_CHARACTER,
	SG_STARTLOADING,

	SG_STARTRACE,
	SG_RACEDATA,
	SG_WEAPON,
	SG_ENDRACE,

	// The room's vote (struct SG_MessageVote): whether it votes, the vote
	// running, its time left and each player's vote. Sent whenever any of
	// it changes, and to a player on the way in. Was SG_SERVERCLOSED, which
	// nothing ever sent.
	SG_VOTE,

	// "the lobby host is now client N" - sent on join and whenever the
	// current host leaves. Appended at the end so every existing opcode
	// keeps its value; type is 4 bits, so there is room up to 15.
	SG_HOST,

	// "the room returns to the lobby in N seconds". Sent when the first racer
	// finishes (the limit everybody else has to finish within) and again when
	// the last one does (the short results countdown, which replaces the
	// first). The server's own timer is what fires; this only lets the game
	// show it.
	SG_LOBBYCOUNTDOWN,

	// "you have N seconds left to make your picks" (0: that clock is paused).
	// The lobby idle budget: one per player per lobby round, running only
	// while there is a pick to make - the host's through track, laps and
	// character, a guest's once the track is in - and never refilled by
	// stepping back. Whoever runs out is removed by the server. While the
	// host is picking, the guests are sent the host's clock so they can see
	// it too. Sent whenever a clock starts or resumes, and once when it
	// pauses; the client counts the seconds down itself in between.
	SG_IDLECOUNTDOWN,

	// "the host has cancelled the track" (CG_TRACKCANCEL, TRIANGLE in the
	// character pick). Header only. Everybody's character lock is off: the
	// host is back on the lap pick, the guests back to waiting on the host,
	// and the host's idle clock follows in SG_IDLECOUNTDOWN.
	SG_TRACKCANCEL,

	// what happened to a placed weapon (CG_WEAPONEVENT, NET_EV_*), relayed
	// with the sender's client ID. The last value the 4-bit type has.
	SG_WEAPONEVENT,

	SG_COUNT
};

// enet_peer_disconnect() data from the server, so the client can say why
// it was dropped (0 is the plain disconnect everything else uses).
#define DISCONNECT_REASON_IDLE 1
#define DISCONNECT_REASON_KICKED 2   // the room's vote to kick (CG_HOSTACTION's KICKVOTE; the host's KICK PLAYER up to 1109)

// variety of opcodes (start load / start race)
struct SG_Header
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// Left over from TCP prototype, not needed anymore.
	// In new UDP system, some packets have recycled this
	unsigned char padding : 4;
};

// Announces which client owns the lobby. One byte: it reuses the header's
// spare nibble rather than adding a payload, so it can never disturb any
// other message's size assertion.
struct SG_MessageHost
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// client ID of the new host, or 0xF for "no host"
	unsigned char hostID : 4;
};

// SG_VOTE
struct SG_MessageVote
{
	unsigned char type : 4;
	unsigned char phase : 2;     // VOTE_*
	unsigned char enabled : 1;   // the host's PLAYER VOTING
	unsigned char padding : 1;

	unsigned char seconds;       // the vote's time left, 0 with none running
	unsigned char track;         // the track voted in (VOTE_LAPS)

	// each player's vote by client ID: the option + 1, 0 none
	unsigned char votes[MAX_NUM_PLAYERS];
};

// SG_VOTE with phase VOTE_KICK_REPLY (1110): the server's answer to one
// player's VOTE TO KICK, to that player alone - the room never hears of a
// vote, only of the kick when the count is reached (the empty SG_NAME of a
// disconnect). Never a room's votePhase.
#define VOTE_KICK_REPLY  3
#define KICKVOTE_CAST    1
#define KICKVOTE_REMOVED 2
#define KICKVOTE_REFUSED 3   // the target gone, voting closed (the countdown), a room too small
struct SG_MessageKickVote
{
	unsigned char type : 4;     // SG_VOTE
	unsigned char phase : 2;    // VOTE_KICK_REPLY
	unsigned char result : 2;   // KICKVOTE_*
	unsigned char target;       // the client ID voted on
	unsigned short mine;        // this player's votes now, bit n for client ID n (octr->kickVotes)
};

struct SG_MessageRooms
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char padding : 4;

	unsigned char numRooms;

	// This server's SERVER_VERSION. The client refuses the server, and shows
	// its update screen, unless it matches the client's own.
	unsigned short version;

	unsigned char numClients01 : 4;
	unsigned char numClients02 : 4;
	unsigned char numClients03 : 4;
	unsigned char numClients04 : 4;
	unsigned char numClients05 : 4;
	unsigned char numClients06 : 4;
	unsigned char numClients07 : 4;
	unsigned char numClients08 : 4;

	unsigned char numClients09 : 4;
	unsigned char numClients10 : 4;
	unsigned char numClients11 : 4;
	unsigned char numClients12 : 4;
	unsigned char numClients13 : 4;
	unsigned char numClients14 : 4;
	unsigned char numClients15 : 4;
	unsigned char numClients16 : 4;

	// bit N: room N+1 cannot be joined now (RoomOpenForJoin). The counts above
	// are 0 to MAX_NUM_PLAYERS; up to 8 players "closed" was +8 on them.
	unsigned short closedMask;

	// bit N: the race in room N+1 can be watched now (1110,
	// RoomOpenForSpectate: past its green, not over, a seat that was empty at
	// its load, not near the forced return to the lobby), less the rooms this
	// player was voted out of a short while ago - octr->clientCount's
	// ROOM_SPECTATE. Read only once the version above has passed: a server
	// from another release sends a list 14 bytes long.
	unsigned short spectateMask;

	// 16 bytes total
};

// sent to each user when someone connects
struct SG_MessageClientStatus
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// 1-15 for client
	unsigned char clientID : 4;

	// 1-15
	unsigned char numClientsTotal : 4;

	// special event: NEWCLIENT_SPECTATOR (1110), the seat is a spectator's -
	// a race in progress joined to watch (CG_MessageRoom's JOIN_SPECTATE),
	// whose player is kept out of it and out of the lobby's counts until the
	// room's reset welcomes them again with 0 (octr->special). The weekday
	// events of 1-3 are never sent.
	unsigned char special : 4;
};

// SG_MessageClientStatus.special's bit 3 (1110)
#define NEWCLIENT_SPECTATOR 8

// get name from any client
struct SG_MessageName
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char padding : 4;

	// index 0 - 7
	unsigned char clientID : 4;
	unsigned char numClientsTotal : 4;

	char name[NAME_LEN + 1];
};

// get track, assigned by host
struct SG_MessageTrack
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	// the room's vote put the track in (Vote_Finish), not the host's pick:
	// the host cannot take it back (CG_TRACKCANCEL; octr->trackVoted)
	unsigned char voted : 1;
	unsigned char padding : 3;

	unsigned char trackID : 5;
	unsigned char lapID : 3;
};

// Lap count behind each LAP SELECT row, by lapID (0-7). The ROM only shows
// the labels (NewPage_Laps in menu.c); the client and server turn the index
// into a count with this table, so all three have to agree.
#define LAP_COUNT_BY_ID 1, 3, 5, 7, 10, 12, 16, 20

// assign character,
struct SG_MessageCharacter
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// index 0 - 14
	unsigned char clientID : 4;
	// character 0 - 15, boolLockedIn 0/1
	unsigned char characterID : 4;
	unsigned char boolLockedIn : 1;
	unsigned char padding : 3;
};

struct SG_EverythingKart
{
	// byte[0]

	// 15 types, 15 bytes max
	unsigned char type : 4;

	// does not include fire level yet
	unsigned char wumpa : 3;
	unsigned char boolReserves : 1;

	// byte[1-2]

	unsigned char clientID : 4;

	// driver->0x39A, 12 bits: the low 4 here, the top 8 in kartRot2
	unsigned char kartRot1 : 4;
	unsigned char kartRot2;

	// byte[3]

	unsigned char buttonHold;

	// byte[4-12]

	short posX;
	short posY;
	short posZ;

	// byte[10]: the kart's flame colour as its own screen shows it
	// (FLAME_*, the sender's CG_RACEDATA flame; 1102)
	unsigned char flame;

	// byte[11]: the sender's number for this message (its CG_RACEDATA seq,
	// 1-255 round again, never 0; 1104; was spare). Kart messages come
	// unsequenced: the others take the lap word below only from one newer
	// than the last they took (KartSeq_Newer, CL_main.cpp), and none from a
	// 0 - what a relay that does not know the field leaves there.
	unsigned char seq;

	// byte[12-13]: the kart's last hurt (NET_HURT_*, the sender's
	// CG_RACEDATA hurt; 1103)
	unsigned short hurt;

	// byte[14-17]: the kart's lap, behind-the-line flag and progress as its
	// own screen has them (NET_LAP_*, the sender's CG_RACEDATA lap; 1104),
	// its bytes as sent
	unsigned char lap[4];

	// 18 bytes
};

struct SG_MessageWeapon
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// driver who used weapon
	unsigned char clientID : 4;

	// 0-15
	unsigned char weapon : 4;
	unsigned char flags : 2;
	unsigned char held : 2;      // the shooter's d-pad: UP (beaker thrown forward), DOWN (bomb backward)

	// the shooter's pose, shot count and target (ShotPose in struct
	// OnlineCTR): where its weapon is spawned on every machine, with
	// clientID the identity of what it places, and the kart a missile or
	// bomb chases (client ID + 1, 0 none)
	unsigned char serial;
	unsigned char target : 4;    // client ID + 1, 0 none
	unsigned char juiced : 1;
	unsigned char padding : 3;
	short pos[3];
	short angle;
	short fwd[2];
};

// What happened to a weapon or a kart (NET_EV_*), from the machine that
// saw it: for a kart, the kart's own machine (clientID is that kart).
// Relayed only when the server takes it (SV_main.c).
struct SG_MessageWeaponEvent
{
	unsigned char type : 4;
	unsigned char clientID : 4;

	unsigned char owner : 4;     // the weapon: its shooter's client ID...
	unsigned char kind : 4;      // NET_EV_*

	unsigned char serial;        // ...and shot count
	unsigned char arg;
};

// CG_MessageEndRace's bit 0 and SG_MessageEndRace.flags (1110): the
// player's kart drives on as the bot past the line (BOTS_Driver_Convert,
// actionsFlagSet 0x100000), and its kart messages go on, with buttons the
// client makes up for it (StatePC_Game_EndRace) - the others hold no SQUARE
// on that copy, which stays a target as a racer is (octr->copyParked).
#define ENDRACE_BOT 1

struct SG_MessageEndRace
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char clientID : 4;

	// ENDRACE_BOT (1110) as the finisher's CG_ENDRACE said it - or 0, in a
	// spectator's catch-up, for a finisher who has left since (their copy
	// braked at once). Was the int's alignment gap: the size stays 12.
	unsigned char flags;

	int courseTime;
	int lapTime;
};

// Sent at the first and at the last CG_ENDRACE. The length travels with the
// message so it is a server setting, not something baked into the ROM.
// kind (1110; was padding, always 0): LOBBYCOUNT_CLOCK that clock, as ever;
// LOBBYCOUNT_WATCHERS, the spectators now watching the room's race in
// seconds' place (Watchers_Send, SV_main.c: as one has caught up and as one
// leaves), which the racers' HUD shows as WATCHERS (octr->watchers).
struct SG_MessageLobbyCountdown
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char kind : 4;

	// seconds until the room resets - or, LOBBYCOUNT_WATCHERS, the
	// spectators watching
	unsigned char seconds;
};

#define LOBBYCOUNT_CLOCK    0
#define LOBBYCOUNT_WATCHERS 1

STATIC_ASSERT2(sizeof(struct SG_Header) == 1, "Size of SG_Header must be 1 byte");
STATIC_ASSERT2(sizeof(struct SG_MessageHost) == 1, "Size of SG_MessageHost must be 1 byte");
STATIC_ASSERT2(sizeof(struct SG_MessageVote) == 3 + MAX_NUM_PLAYERS, "Size of SG_MessageVote must be 3 + MAX_NUM_PLAYERS bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageKickVote) == 4, "Size of SG_MessageKickVote must be 4 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageRooms) == 16, "Size of SG_MessageRooms must be 16 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageClientStatus) == 2, "Size of SG_MessageClientStatus must be 2 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageName) == 2 + NAME_LEN + 1, "Size of SG_MessageName must be 2 + NAME_LEN + 1 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageCharacter) == 2, "Size of SG_MessageCharacter must be 2 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageTrack) == 2, "Size of SG_MessageTrack must be 2 bytes");
STATIC_ASSERT2(sizeof(struct SG_EverythingKart) == 18, "Size of SG_EverythingKart must be 18 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageWeapon) == 16, "Size of SG_MessageWeapon must be 16 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageWeaponEvent) == 4, "Size of SG_MessageWeaponEvent must be 4 bytes");
STATIC_ASSERT2(sizeof(struct SG_MessageEndRace) == 12, "Size of SG_MessageEndRace must be 12 bytes");
// flags in the gap, the times where they were (1110)
#ifdef __GNUC__
STATIC_ASSERT2(__builtin_offsetof(struct SG_MessageEndRace, flags) == 1, "SG_MessageEndRace layout: flags");
STATIC_ASSERT2(__builtin_offsetof(struct SG_MessageEndRace, courseTime) == 4, "SG_MessageEndRace layout: courseTime");
STATIC_ASSERT2(__builtin_offsetof(struct SG_MessageRooms, spectateMask) == 14, "SG_MessageRooms layout: spectateMask");
#else
STATIC_ASSERT2(offsetof(struct SG_MessageEndRace, flags) == 1, "SG_MessageEndRace layout: flags");
STATIC_ASSERT2(offsetof(struct SG_MessageEndRace, courseTime) == 4, "SG_MessageEndRace layout: courseTime");
STATIC_ASSERT2(offsetof(struct SG_MessageRooms, spectateMask) == 14, "SG_MessageRooms layout: spectateMask");
#endif
STATIC_ASSERT2(sizeof(struct SG_MessageLobbyCountdown) == 2, "Size of SG_MessageLobbyCountdown must be 2 bytes");

// The lobby idle clock (SG_IDLECOUNTDOWN): seconds left to make a pick -
// or, once this client has locked in, seconds left on the longest clock
// still running in the room, the wait for the others - or 0 when there is
// no clock to show. The length is the server's setting.
struct SG_MessageIdleCountdown
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char padding : 4;

	unsigned char seconds;
};

STATIC_ASSERT2(sizeof(struct SG_MessageIdleCountdown) == 2, "Size of SG_MessageIdleCountdown must be 2 bytes");

enum ClientGiveMessageType
{
	CG_JOINROOM,

	// lobby
	CG_NAME,
	CG_TRACK,
	CG_CHARACTER,

	CG_STARTRACE,
	CG_RACEDATA,
	CG_WEAPON,
	CG_ENDRACE,

	// host only, header only: undo CG_TRACK while the room is still on the
	// character pick (see SG_TRACKCANCEL)
	CG_TRACKCANCEL,

	// what happened to a placed weapon (NET_EV_*; see SG_WEAPONEVENT)
	CG_WEAPONEVENT,

	// the host's CHANGE HOST and PLAYER VOTING, anybody's VOTE TO KICK
	// (CG_MessageHostAction). The room hears of a change of host as of a
	// host leaving (SG_HOST), of a vote only when it removes a player (the
	// empty SG_NAME of a disconnect); the voter's answer is SG_VOTE's
	// VOTE_KICK_REPLY.
	CG_HOSTACTION,

	// a player's vote, while one runs (CG_MessageVote, SG_VOTE)
	CG_VOTE,

	CG_COUNT
};

// sent to each user when someone connects
struct CG_Header
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// Left over from TCP prototype, not needed anymore.
	// In new UDP system, some packets have recycled this
	unsigned char padding : 4;
};

struct CG_MessageRoom
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// JOIN_SPECTATE (1110; was padding): should the room be closed to a
	// join, take a seat in its race in progress as a spectator, if it can be
	// watched (SG_MessageRooms.spectateMask; SV_main.c's spectator path); a
	// refusal is the room list back, as for any join. 0 a player's join, as
	// ever. The client zeroes the message before it fills it.
	unsigned char flags : 4;

	unsigned char room;

	// The client's SERVER_VERSION (the wire number, not its build). The
	// server refuses a join from any other. The client checks the server's
	// number too (SG_MessageRooms), but that answer is a round trip late, and
	// a quick lock-in used to get a join request out - and accepted - before
	// it arrived.
	unsigned short version;
};

// CG_MessageRoom.flags (1110)
#define JOIN_SPECTATE 1

struct CG_MessageName
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char padding : 4;

	char name[NAME_LEN + 1];
};

// get track, assigned by host
struct CG_MessageTrack
{
	// 15 types, 15 bytes max
	unsigned char type : 4;
	unsigned char padding : 4;

	unsigned char trackID : 5;
	unsigned char lapID : 3;
};

// character message
struct CG_MessageCharacter
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// character 0 - 15
	// lockedIn 0/1
	unsigned char characterID : 4;
	unsigned char boolLockedIn : 1;

	// can be used for Engine type, or more characters
	unsigned char padding : 7;
};

struct CG_EverythingKart
{
	// byte[0]

	// 15 types, 15 bytes max
	unsigned char type : 4;

	// does not include fire level yet
	unsigned char wumpa : 3;
	unsigned char boolReserves : 1;

	// driver->0x39A: kartRot1 its low 4 bits, kartRot2 its top 8
	unsigned char kartRot1 : 4;
	// the flame's colour as this screen shows it (FLAME_*, octr->flameOwn;
	// 1102), in kartRot1's byte
	unsigned char flame : 2;
	unsigned char spare : 2;
	unsigned char kartRot2;

	// byte[3]

	unsigned char buttonHold;

	// byte[4-12]

	short posX;
	short posY;
	short posZ;

	// byte[10-11]: the kart's last hurt (NET_HURT_*, octr->hurtOwn; 1103)
	unsigned short hurt;

	// byte[12-15]: the kart's lap, behind-the-line flag and progress
	// (NET_LAP_*: the ROM's word for this kart, NET_LAP_ADDR(0), as it is;
	// 1104). Bytes, not an int: the struct keeps its 2-byte alignment and
	// size on every compiler, and only memcpy touches it.
	unsigned char lap[4];

	// byte[16]: this message's number, one up on the last, 1-255 round
	// again (0 is skipped: SG_EverythingKart.seq), relayed as SG_RACEDATA's
	// seq; 1104
	unsigned char seq;
	unsigned char spare2;

	// 18 bytes
};

struct CG_MessageWeapon
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// can be used for desired target
	unsigned char juiced : 1;
	unsigned char padding : 1;
	unsigned char flags : 2;

	// 0-15
	unsigned char weapon : 4;
	unsigned char held : 2;      // as SG_MessageWeapon
	unsigned char padding2 : 2;

	// as SG_MessageWeapon: the shooter's pose, shot count and target
	unsigned char serial;
	unsigned char target;
	short pos[3];
	short angle;
	short fwd[2];
};

struct CG_MessageWeaponEvent
{
	unsigned char type : 4;
	// a placed weapon's claim its kart waits on (NET_EV_TENTATIVE): held by
	// the server for an earlier touch on the way (WeaponHold_*, SV_main.c)
	unsigned char tentative : 1;
	unsigned char padding : 3;

	unsigned char owner : 4;
	unsigned char kind : 4;

	unsigned char serial;
	unsigned char arg;
};

// The lobby's OPTIONS: hand the room to a player, or vote one out
// (HOST_ACTION_*), by client ID - and by name, as the player's lobby has it:
// a seat let go and taken again inside a round trip holds somebody the
// player never picked, and the server refuses a name that no longer sits there.
struct CG_MessageHostAction
{
	unsigned char type : 4;
	unsigned char action : 4;
	unsigned char clientID;
	char name[NAME_LEN];
};

// A player's vote: the option (a track's level ID, a lap row) for the
// vote running.
struct CG_MessageVote
{
	unsigned char type : 4;
	unsigned char phase : 4;    // VOTE_TRACK or VOTE_LAPS: the vote it is for
	unsigned char option;
};

struct CG_MessageEndRace
{
	// 15 types, 15 bytes max
	unsigned char type : 4;

	// bit 0 ENDRACE_BOT (1110): this player's kart drives on as the bot past
	// the line, and its kart messages go on - the server keeps it with the
	// finish (a spectator's catch-up) and passes it on in SG_ENDRACE's flags.
	// Bits 1-3 are 0 (the client's message is zeroed).
	unsigned char padding : 4;

	int courseTime;
	int lapTime;
};

#define DRIVER_COURSE_OFFSET 0x514
#define DRIVER_BESTLAP_OFFSET 0x63c

STATIC_ASSERT2(sizeof(struct CG_Header) == 1, "Size of CG_Header must be 1 byte");
STATIC_ASSERT2(sizeof(struct CG_MessageRoom) == 4, "Size of CG_MessageRoom must be 4 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageName) == 1 + NAME_LEN + 1, "Size of CG_MessageName must be 1 + NAME_LEN + 1 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageCharacter) == 2, "Size of CG_MessageCharacter must be 2 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageTrack) == 2, "Size of CG_MessageTrack must be 2 bytes");
STATIC_ASSERT2(sizeof(struct CG_EverythingKart) == 18, "Size of CG_EverythingKart must be 18 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageWeapon) == 16, "Size of CG_MessageWeapon must be 16 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageWeaponEvent) == 4, "Size of CG_MessageWeaponEvent must be 4 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageHostAction) == 2 + NAME_LEN, "Size of CG_MessageHostAction must be 2 + NAME_LEN bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageVote) == 2, "Size of CG_MessageVote must be 2 bytes");
STATIC_ASSERT2(sizeof(struct CG_MessageEndRace) == 12, "Size of CG_MessageEndRace must be 12 bytes");

// Quick Play's status query: one UDP datagram to a server's own port and
// one back, outside ENet - no connection, so nothing that counts a player
// (the seen-24h hashes, the per-address cap) sees it. The client's prober
// (QuickPlay_ProbeThread, CL_main.cpp) times the round trip by the token
// it gets back and reads the rooms from the answer; the server answers
// from ENet's intercept hook, ahead of ENet's own parsing
// (OctrQuery_Intercept, SV_main.c). The query starts 0xFF 0xFE: as an
// ENet header that is peer 0xFFE, past any server's peers, so a server
// from before 1101 drops it unread. The query is as big as the answer,
// so the answer cannot be used to amplify traffic at a forged address.
#define OCTR_QUERY_MAGIC "\xFF\xFEOCQ1"
#define OCTR_REPLY_MAGIC "\xFF\xFEOCR1"
#define OCTR_MAGIC_LEN   6

struct OctrQuery
{
	unsigned char magic[OCTR_MAGIC_LEN];   // OCTR_QUERY_MAGIC
	unsigned short version;                // the client's SERVER_VERSION
	unsigned int token;                    // anything; sent back as it came
	unsigned char pad[20];                 // as big as the answer
};

struct OctrQueryReply
{
	unsigned char magic[OCTR_MAGIC_LEN];   // OCTR_REPLY_MAGIC
	unsigned short version;                // the server's SERVER_VERSION
	unsigned int token;                    // the query's
	unsigned char counts[16];              // the players seated in each room, as SG_ROOMS counts them
	unsigned short closedMask;             // the rooms not open to a join now, as SG_ROOMS has them
	unsigned short spectateMask;           // the rooms whose race can be watched now, as SG_ROOMS has them (1110; was pad)
};

STATIC_ASSERT2(sizeof(struct OctrQuery) == 32, "Size of OctrQuery must be 32 bytes");
STATIC_ASSERT2(sizeof(struct OctrQueryReply) == 32, "Size of OctrQueryReply must be 32 bytes");

// OnlineCTR functions
void StatePC_Launch_EnterPID();
void StatePC_Launch_PickServer();
void StatePC_Launch_PickRoom();
void StatePC_Launch_Error();
void StatePC_Lobby_HostTrackPick();
void StatePC_Lobby_GuestTrackWait();
void StatePC_Lobby_CharacterPick();
void StatePC_Lobby_WaitForLoading();
void StatePC_Lobby_StartLoading();
void StatePC_Game_WaitForRace();
void StatePC_Game_StartRace();
void StatePC_Game_EndRace();

// console functions
void PrintBanner(char show_name);
void StartAnimation();
void StopAnimation();

#endif

#ifndef WINDOWS_INCLUDE
	// set zero to fix DuckStation,
	// is it needed on console?
	#define USE_K1 0

	#if USE_K1 == 1
		register struct OnlineCTR* octr asm("k1");
	#else
		static struct OnlineCTR* octr = (struct OnlineCTR*)0x8000C000;
	#endif

	// Retail tables online code fills by slot, grown by the engine to
	// MAX_KARTS (include/common.h): gGT->drivers, kartSpawnOrderArray,
	// data.characterIDs. Online builds only: offline MAX_KARTS is 8.
	#ifdef USE_ONLINE
	STATIC_ASSERT2(MAX_NUM_PLAYERS <= MAX_KARTS, "MAX_NUM_PLAYERS: past MAX_KARTS (include/common.h)");
	STATIC_ASSERT2(sizeof(((struct GameTracker*)0)->drivers) >= MAX_NUM_PLAYERS * sizeof(struct Driver*), "gGT->drivers is shorter than MAX_NUM_PLAYERS");
	STATIC_ASSERT2(sizeof(((struct sData*)0)->kartSpawnOrderArray) >= MAX_NUM_PLAYERS, "kartSpawnOrderArray is shorter than MAX_NUM_PLAYERS");
	STATIC_ASSERT2(sizeof(((struct Data*)0)->characterIDs) >= MAX_NUM_PLAYERS * sizeof(short), "data.characterIDs is shorter than MAX_NUM_PLAYERS");
	// the client's controller writes and the ROM's reads, slot for slot
	STATIC_ASSERT2(ONLINE_PAD_ADDR(8) == 0x80096804u + 8 * 0x50u + ONLINE_PAD_EXTRA(8), "ONLINE_PAD_ADDR vs DRIVER_PAD: slot 8");
	STATIC_ASSERT2(ONLINE_PAD_ADDR(9) == 0x80096804u + 9 * 0x50u + ONLINE_PAD_EXTRA(9), "ONLINE_PAD_ADDR vs DRIVER_PAD: slot 9");
	STATIC_ASSERT2(ONLINE_PAD_ADDR(10) == 0x80096804u + 10 * 0x50u + ONLINE_PAD_EXTRA(10), "ONLINE_PAD_ADDR vs DRIVER_PAD: slot 10");
	STATIC_ASSERT2(ONLINE_PAD_ADDR(11) == 0x80096804u + 11 * 0x50u + ONLINE_PAD_EXTRA(11), "ONLINE_PAD_ADDR vs DRIVER_PAD: slot 11");
	STATIC_ASSERT2(ONLINE_PAD_ADDR(10) == 0x80097B44u && ONLINE_PAD_ADDR(11) == 0x80097B94u, "slots 10-11's controllers: gGT + 0x1024, + 0x1074");
	#endif

	// my functions
	void StatePS1_Launch_EnterPID();
	void StatePS1_Launch_PickServer();
	void StatePS1_Launch_Error();
	void StatePS1_Launch_PickRoom();
	void StatePS1_Lobby_AssignRole();
	void StatePS1_Lobby_HostTrackPick();
	void StatePS1_Lobby_GuestTrackWait();
	void StatePS1_Lobby_CharacterPick();
	void StatePS1_Lobby_WaitForLoading();
	void StatePS1_Lobby_StartLoading();
	void StatePS1_Game_WaitForRace();
	void StatePS1_Game_StartRace();
	void StatePS1_Game_EndRace();
#endif

#endif
