// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

// The main loop's pause between ticks (SV_main.c): usec microseconds, which
// the system rounds up to its timer's period (about 1 ms on Windows once
// ENet has asked for it, enet_initialize; tens of microseconds on Linux and
// macOS).
//
// Windows - Visual Studio and MinGW alike (_WIN32, which both define; only
// Server.vcxproj defines __WINDOWS__) - waits on a waitable timer.
// MinGW-w64's own usleep() is no use there: it is Sleep(usec / 1000), so a
// pause of a microsecond is Sleep(0), which returns at once, and the main
// loop would spin a whole CPU core.
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
// keeps winsock.h out of windows.h; enet.h (below) brings winsock2.h
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void SleepMicro(long long usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}
#else
#include <unistd.h>
void SleepMicro(long long usec)
{
	usleep((useconds_t)usec);
}
#endif

#include <stdio.h>
#include <time.h>

// https://stackoverflow.com/questions/3673226/how-to-print-time-in-format-2009-08-10-181754-811
void PrintTime()
{
	time_t timer;
	char buffer[32];
	struct tm* tm_info;

	timer = time(NULL);
	tm_info = localtime(&timer);

	strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
	printf("%s", buffer);
}

int GetWeekDay()
{
	time_t timer;
	char buffer[32];
	struct tm* tm_info;

	timer = time(NULL);
	tm_info = localtime(&timer);

	strftime(buffer, 26, "%w", tm_info);

	return buffer[0] - '0';
}

// malloc and free: <stdlib.h>, everywhere. Not <malloc.h>, which is glibc's
// and MSVC's only - macOS has none, and a build there stopped at it.
#include <stdlib.h>
// The real type, not the old "typedef ENetPeer;" placeholder (which made
// ENetPeer an int): CountPeersLPL_FromHost looks inside the peer.
#include <enet/enet.h>
struct LoosePeerList;
struct LoosePeerList {
	ENetPeer* peer;
	struct LoosePeerList* next;
};

struct LoosePeerList* lplHead = NULL;

void AddPeerLPL(ENetPeer* peer)
{
	if (lplHead == NULL)
	{
		lplHead = malloc(sizeof(struct LoosePeerList));
		lplHead->next = NULL;
		lplHead->peer = peer;
	}
	else
	{
		struct LoosePeerList* c = lplHead;
		while (c->next != NULL)
			c = c->next;
		c->next = malloc(sizeof(struct LoosePeerList));
		c->next->next = NULL;
		c->next->peer = peer;
	}
}

int RemovePeerLPL(ENetPeer* peer)
{
	int didFree = 0;
	struct LoosePeerList* c = lplHead, * prev = NULL;
	while (c != NULL)
	{
		if (c->peer == peer)
		{
			if (prev == NULL)
			{
				struct LoosePeerList* toFree = c;

				lplHead = c->next;
				c = c->next;

				free(toFree);
				didFree |= 1;

			}
			else
			{

				struct LoosePeerList* toFree = c;

				prev->next = c->next;
				c = c->next;

				free(toFree);
				didFree |= 1;
			}
		}
		else
		{
			//i++
			prev = c;
			c = c->next;
		}
	}
	return didFree;
}

int ForeachPeerLPL(void (*lambda)(ENetPeer*))
{
	int count = 0;
	struct LoosePeerList* c = lplHead;
	while (c != NULL)
	{
		count++;
		(*lambda)(c->peer);
		c = c->next;
	}
	return count;
}

// Connected peers (joined a room or not) whose address matches; used to cap
// connections per address on connect.
int CountPeersLPL_FromHost(unsigned int host)
{
	int count = 0;
	struct LoosePeerList* c = lplHead;
	while (c != NULL)
	{
		if (c->peer != NULL && c->peer->address.host == host)
			count++;
		c = c->next;
	}
	return count;
}

struct IPBanL;
struct IPBanL {
	unsigned int ip;
	struct IPBanL* next;
};

struct IPBanL* ipBanLHead = NULL;

void AddIPBanL(unsigned int IP)
{
	if (ipBanLHead == NULL)
	{
		ipBanLHead = malloc(sizeof(struct IPBanL));
		ipBanLHead->next = NULL;
		ipBanLHead->ip = IP;
	}
	else
	{
		struct IPBanL* c = ipBanLHead;
		while (c->next != NULL)
			c = c->next;
		c->next = malloc(sizeof(struct IPBanL));
		c->next->next = NULL;
		c->next->ip = IP;
	}
}

int AnyMatchIPBanL(unsigned int IP)
{
	int match = 0;
	struct IPBanL* c = ipBanLHead;
	while (c != NULL)
	{
		match |= c->ip == IP;
		c = c->next;
	}
	return match;
}