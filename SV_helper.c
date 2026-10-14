// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

#ifdef __WINDOWS__
#define _CRT_SECURE_NO_WARNINGS
// keeps winsock.h out of Windows.h; enet.h (below) brings winsock2.h
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
void usleep(__int64 usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
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

#include "malloc.h"
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