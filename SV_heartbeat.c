// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2024-2026 OnlineCTR Team.
// OnlineCTR is free software under the GNU Affero General Public License,
// version 3 (LICENSE), with the additional terms in ADDITIONAL-TERMS under
// its section 7. It comes with ABSOLUTELY NO WARRANTY.

/*
 * SV_heartbeat.c - see SV_heartbeat.h for the design notes.
 *
 * SHA-256 / HMAC are vendored here rather than taken from OpenSSL on purpose:
 * libcurl may be built against GnuTLS, NSS or Schannel instead of OpenSSL, so
 * <openssl/hmac.h> is not guaranteed to be available just because libcurl is.
 * This keeps the module dependent on libcurl alone.
 */

#include "SV_config.h"
#include "SV_heartbeat.h"

#ifdef OCTR_HEARTBEAT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#ifdef _WIN32
	#include <windows.h>
	typedef CRITICAL_SECTION octr_mutex;
	#define MUTEX_INIT(m)    InitializeCriticalSection(m)
	#define MUTEX_LOCK(m)    EnterCriticalSection(m)
	#define MUTEX_UNLOCK(m)  LeaveCriticalSection(m)
	#define SLEEP_SEC(s)     Sleep((DWORD)(s) * 1000)
#else
	#include <pthread.h>
	#include <unistd.h>
	typedef pthread_mutex_t octr_mutex;
	#define MUTEX_INIT(m)    pthread_mutex_init((m), NULL)
	#define MUTEX_LOCK(m)    pthread_mutex_lock(m)
	#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
	#define SLEEP_SEC(s)     sleep((unsigned int)(s))
#endif

/* ---------------------------------------------------------------- SHA-256 */

typedef struct
{
	unsigned int  state[8];
	unsigned long long bitLen;
	unsigned char buf[64];
	unsigned int  bufLen;
} sha256_ctx;

static const unsigned int SHA256_K[64] = {
	0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
	0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
	0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
	0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
	0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
	0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
	0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
	0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define ROTR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_compress(sha256_ctx* c, const unsigned char* p)
{
	unsigned int w[64], a, b, d, e, f, g, h, t1, t2, cc;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((unsigned int)p[i*4] << 24) | ((unsigned int)p[i*4+1] << 16) |
		       ((unsigned int)p[i*4+2] << 8) | ((unsigned int)p[i*4+3]);

	for (i = 16; i < 64; i++)
	{
		unsigned int s0 = ROTR32(w[i-15],7) ^ ROTR32(w[i-15],18) ^ (w[i-15] >> 3);
		unsigned int s1 = ROTR32(w[i-2],17) ^ ROTR32(w[i-2],19)  ^ (w[i-2] >> 10);
		w[i] = w[i-16] + s0 + w[i-7] + s1;
	}

	a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
	e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];

	for (i = 0; i < 64; i++)
	{
		unsigned int S1 = ROTR32(e,6) ^ ROTR32(e,11) ^ ROTR32(e,25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int S0 = ROTR32(a,2) ^ ROTR32(a,13) ^ ROTR32(a,22);
		unsigned int maj = (a & b) ^ (a & cc) ^ (b & cc);
		t1 = h + S1 + ch + SHA256_K[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = cc; cc = b; b = a; a = t1 + t2;
	}

	c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
	c->state[4] += e; c->state[5] += f; c->state[6] += g;  c->state[7] += h;
}

static void sha256_init(sha256_ctx* c)
{
	c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
	c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
	c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
	c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
	c->bitLen = 0;
	c->bufLen = 0;
}

static void sha256_update(sha256_ctx* c, const unsigned char* data, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
	{
		c->buf[c->bufLen++] = data[i];
		if (c->bufLen == 64)
		{
			sha256_compress(c, c->buf);
			c->bitLen += 512;
			c->bufLen = 0;
		}
	}
}

static void sha256_final(sha256_ctx* c, unsigned char out[32])
{
	unsigned int i = c->bufLen;

	c->bitLen += (unsigned long long)c->bufLen * 8;

	c->buf[i++] = 0x80;
	if (i > 56)
	{
		while (i < 64) c->buf[i++] = 0;
		sha256_compress(c, c->buf);
		i = 0;
	}
	while (i < 56) c->buf[i++] = 0;

	for (i = 0; i < 8; i++)
		c->buf[56 + i] = (unsigned char)(c->bitLen >> (56 - 8 * i));
	sha256_compress(c, c->buf);

	for (i = 0; i < 8; i++)
	{
		out[i*4]     = (unsigned char)(c->state[i] >> 24);
		out[i*4 + 1] = (unsigned char)(c->state[i] >> 16);
		out[i*4 + 2] = (unsigned char)(c->state[i] >> 8);
		out[i*4 + 3] = (unsigned char)(c->state[i]);
	}
}

/* ------------------------------------------------------------ HMAC-SHA256 */

static void hmac_sha256(const char* key, const char* msg, char outHex[65])
{
	unsigned char k[64], inner[32], outer[32], ipad[64], opad[64];
	unsigned char digest[32];
	size_t keyLen = strlen(key);
	sha256_ctx c;
	int i;

	memset(k, 0, sizeof(k));
	if (keyLen > 64)
	{
		sha256_init(&c);
		sha256_update(&c, (const unsigned char*)key, keyLen);
		sha256_final(&c, k);
	}
	else
	{
		memcpy(k, key, keyLen);
	}

	for (i = 0; i < 64; i++)
	{
		ipad[i] = (unsigned char)(k[i] ^ 0x36);
		opad[i] = (unsigned char)(k[i] ^ 0x5c);
	}

	sha256_init(&c);
	sha256_update(&c, ipad, 64);
	sha256_update(&c, (const unsigned char*)msg, strlen(msg));
	sha256_final(&c, inner);

	sha256_init(&c);
	sha256_update(&c, opad, 64);
	sha256_update(&c, inner, 32);
	sha256_final(&c, outer);

	memcpy(digest, outer, 32);

	/* Hand-rolled hex rather than sprintf("%02x"): the Server project builds
	   with SDLCheck, which promotes the C4996 deprecation warning on sprintf
	   to a hard error. A nibble lookup avoids both that and the _s/non-_s
	   portability split between MSVC and gcc. */
	{
		static const char hexDigits[] = "0123456789abcdef";
		for (i = 0; i < 32; i++)
		{
			outHex[i * 2]     = hexDigits[(digest[i] >> 4) & 0x0F];
			outHex[i * 2 + 1] = hexDigits[digest[i] & 0x0F];
		}
	}
	outHex[64] = '\0';
}

/* ------------------------------------------------------------------ state */

static octr_mutex   g_lock;
static OctrStatus   g_status;
static int          g_published = 0; /* g_status has been published (guarded by g_lock) */
static int          g_running = 0;
static char         g_url[512];
static char         g_secret[256];
static char         g_seenKey[256];  /* "" = key the seen hashes with g_secret */
static char         g_id[64];
static int          g_intervalSec = HEARTBEAT_INTERVAL_SECONDS;
static long long    g_nonceCounter = 0;
static time_t       g_startTime;

/* The endpoint's reply, kept for the bans in it (bans_from_reply; 1115 -
   discarded before that, which also kept libcurl from printing it into the
   log). What does not fit REPLY_MAX is dropped: the status is read first. */
#define REPLY_MAX 16384
static char   g_reply[REPLY_MAX + 1];
static size_t g_replyLen;
static size_t sink(void* p, size_t sz, size_t n, void* u)
{
	size_t len = sz * n;
	(void)u;
	if (g_replyLen < REPLY_MAX)
	{
		size_t take = len;
		if (take > REPLY_MAX - g_replyLen) take = REPLY_MAX - g_replyLen;
		memcpy(g_reply + g_replyLen, p, take);
		g_replyLen += take;
		g_reply[g_replyLen] = '\0';
	}
	return len;
}

/* -------------------------------------------------- anti-cheat bans (1115) */

#define BAN_HEX_LEN 16   /* 8 bytes of the HMAC, as SEEN_HEX_LEN below */

/* The addresses barred for cheating, this server's and the others' from the
   site, as keyed hashes (ban_hash) with when they end. Guarded by g_lock. */
typedef struct
{
	char   hex[BAN_HEX_LEN + 1];   /* "" = free */
	time_t until;
} BanEntry;
static BanEntry g_bans[HEARTBEAT_BANS_MAX];

static void ban_hash(unsigned int host, char outHex[BAN_HEX_LEN + 1]);

/* With g_lock held: the hash's slot, made (over the soonest to end) if none,
   its end the later of the two. */
static void bans_put(const char* hex, time_t until)
{
	int i, slot = -1, soonest = 0;
	for (i = 0; i < HEARTBEAT_BANS_MAX; i++)
	{
		if (g_bans[i].hex[0] && strcmp(g_bans[i].hex, hex) == 0)
		{
			if (until > g_bans[i].until) g_bans[i].until = until;
			return;
		}
		if (!g_bans[i].hex[0]) { if (slot < 0) slot = i; }
		else if (g_bans[i].until < g_bans[soonest].until) soonest = i;
	}
	if (slot < 0) slot = soonest;
	snprintf(g_bans[slot].hex, sizeof(g_bans[slot].hex), "%s", hex);
	g_bans[slot].until = until;
}

static void bans_expire(time_t now)
{
	int i;
	for (i = 0; i < HEARTBEAT_BANS_MAX; i++)
		if (g_bans[i].hex[0] && g_bans[i].until <= now)
			g_bans[i].hex[0] = '\0';
}

/* With g_lock held: the bans as the body's list, {"h":"...","left":n}
   each; the count. */
static int bans_list(char* out, size_t outSize, time_t now)
{
	size_t used = 0;
	int i, count = 0;
	out[0] = '\0';
	for (i = 0; i < HEARTBEAT_BANS_MAX; i++)
	{
		if (!g_bans[i].hex[0] || g_bans[i].until <= now) continue;
		if (used + 48 >= outSize) break;
		used += (size_t)snprintf(out + used, outSize - used, "%s{\"h\":\"%s\",\"left\":%ld}",
			count ? "," : "", g_bans[i].hex, (long)(g_bans[i].until - now));
		count++;
	}
	return count;
}

/* The site's list out of the reply: every {"h":"<16 hex>","left":n} after
   "bans", kept with its end (bounded: the reply is REPLY_MAX at most, a
   hash 16 lowercase hex characters, left capped at CHEAT_BAN_SECONDS). */
static void bans_from_reply(time_t now)
{
	const char* p = strstr(g_reply, "\"bans\"");
	int taken = 0;
	if (!p) return;
	MUTEX_LOCK(&g_lock);
	while ((p = strstr(p, "{\"h\":\"")) != NULL && taken < HEARTBEAT_BANS_MAX)
	{
		char hex[BAN_HEX_LEN + 1];
		long left = 0;
		int i, ok = 1;
		p += 6;
		for (i = 0; i < BAN_HEX_LEN; i++)
		{
			char c = p[i];
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { ok = 0; break; }
			hex[i] = c;
		}
		hex[BAN_HEX_LEN] = '\0';
		if (!ok || strncmp(p + BAN_HEX_LEN, "\",\"left\":", 10) != 0) continue;
		left = strtol(p + BAN_HEX_LEN + 10, NULL, 10);
		if (left <= 0) continue;
		if (left > CHEAT_BAN_SECONDS) left = CHEAT_BAN_SECONDS;
		bans_put(hex, now + left);
		taken++;
	}
	MUTEX_UNLOCK(&g_lock);
}

void Heartbeat_BanPublish(unsigned int host)
{
	char hex[BAN_HEX_LEN + 1];
	if (!g_running) return;
	ban_hash(host, hex);
	MUTEX_LOCK(&g_lock);
	bans_put(hex, time(NULL) + CHEAT_BAN_SECONDS);
	MUTEX_UNLOCK(&g_lock);
}

int Heartbeat_Banned(unsigned int host)
{
	char hex[BAN_HEX_LEN + 1];
	time_t now = time(NULL);
	int i, banned = 0;
	if (!g_running) return 0;
	ban_hash(host, hex);
	MUTEX_LOCK(&g_lock);
	for (i = 0; i < HEARTBEAT_BANS_MAX; i++)
		if (g_bans[i].hex[0] && g_bans[i].until > now && strcmp(g_bans[i].hex, hex) == 0) { banned = 1; break; }
	MUTEX_UNLOCK(&g_lock);
	return banned;
}

/* -------------------------------------------------- players seen, 7 days */

/* Kept a week (1114, Matt: "Instead of 24 hours of player data saved on the
   server, let's make it 7 days"; 24 hours until then). The heartbeat sends
   the past 24 hours as "seen", as before - so a site snippet from before
   this still counts its 24h line right - and the rest of the week as
   "seenWeek". */
#define SEEN_WINDOW_SEC (7 * 24 * 60 * 60)
#define SEEN_RECENT_SEC (24 * 60 * 60)
#define SEEN_HEX_LEN    16              /* 8 bytes of the HMAC */

typedef struct
{
	char   hex[SEEN_HEX_LEN + 1];       /* "" = free slot */
	time_t lastSeen;
} SeenEntry;

static SeenEntry g_seen[OCTR_HEARTBEAT_SEEN_MAX];   /* guarded by g_lock */

/* HMAC(key, "<utc week number>|<address>|<name>"), first 8 bytes as hex. The
   key is OCTR_HEARTBEAT_SEEN_KEY when set - the same value on every server,
   so the site's union of the lists counts a player once however many
   regions they visit - and otherwise this server's own heartbeat secret.
   The week (whole SEEN_WINDOW_SEC periods since the epoch; the day until
   1114) rather than a date string: no gmtime, so no MSVC _s variant to
   juggle - and one player is one hash all week, so the week's count is
   not every day's added up. name may be NULL (address only, at connect). */
static void seen_hash(unsigned int host, const char* name, char outHex[SEEN_HEX_LEN + 1])
{
	char msg[96];
	char full[65];

	snprintf(msg, sizeof(msg), "%lld|%08x|%s",
		(long long)(time(NULL) / SEEN_WINDOW_SEC), host, name ? name : "");
	hmac_sha256(g_seenKey[0] ? g_seenKey : g_secret, msg, full);
	memcpy(outHex, full, SEEN_HEX_LEN);
	outHex[SEEN_HEX_LEN] = '\0';
}

/* HMAC(key, "ban|<address>"), first 8 bytes as hex (1115): the anti-cheat
   bans' hash, the same on every server with the shared key, no date in it.
   Said at start-up when there is no shared key: bans then match this
   server's own alone. */
static void ban_hash(unsigned int host, char outHex[BAN_HEX_LEN + 1])
{
	char msg[32];
	char full[65];
	snprintf(msg, sizeof(msg), "ban|%08x", host);
	hmac_sha256(g_seenKey[0] ? g_seenKey : g_secret, msg, full);
	memcpy(outHex, full, SEEN_HEX_LEN);
	outHex[SEEN_HEX_LEN] = '\0';
}

/* The rest run with g_lock held. */

static int seen_find(const char* hex)
{
	int i;
	for (i = 0; i < OCTR_HEARTBEAT_SEEN_MAX; i++)
		if (g_seen[i].hex[0] && strcmp(g_seen[i].hex, hex) == 0)
			return i;
	return -1;
}

static void seen_expire(time_t now)
{
	int i;
	for (i = 0; i < OCTR_HEARTBEAT_SEEN_MAX; i++)
		if (g_seen[i].hex[0] && now - g_seen[i].lastSeen > SEEN_WINDOW_SEC)
			g_seen[i].hex[0] = '\0';
}

/* A free slot, or the least recently seen one when the set is full. */
static int seen_slot(void)
{
	int i, oldest = 0;
	for (i = 0; i < OCTR_HEARTBEAT_SEEN_MAX; i++)
	{
		if (!g_seen[i].hex[0]) return i;
		if (g_seen[i].lastSeen < g_seen[oldest].lastSeen) oldest = i;
	}
	return oldest;
}

/* Records hex as seen now; returns its slot. */
static int seen_touch(const char* hex, time_t now)
{
	int i = seen_find(hex);
	if (i < 0)
	{
		i = seen_slot();
		memcpy(g_seen[i].hex, hex, SEEN_HEX_LEN + 1);
	}
	g_seen[i].lastSeen = now;
	return i;
}

/* The JSON array body for "seen" (recent: seen in the past SEEN_RECENT_SEC)
   or "seenWeek" (the rest still inside the window) - quoted and
   comma-separated - and how many there are. Expire first. */
static int seen_list(char* out, size_t outSize, time_t now, int recent)
{
	int i, count = 0;
	size_t used = 0;

	out[0] = '\0';
	for (i = 0; i < OCTR_HEARTBEAT_SEEN_MAX && used + SEEN_HEX_LEN + 4 < outSize; i++)
	{
		if (!g_seen[i].hex[0]) continue;
		if ((now - g_seen[i].lastSeen <= SEEN_RECENT_SEC) != (recent != 0)) continue;
		used += (size_t)snprintf(out + used, outSize - used, "%s\"%s\"",
			count ? "," : "", g_seen[i].hex);
		count++;
	}
	return count;
}

void* Heartbeat_SeenConnect(unsigned int host)
{
	char hex[SEEN_HEX_LEN + 1];
	time_t now;
	int i;

	if (!g_running) return NULL;

	now = time(NULL);
	seen_hash(host, NULL, hex);

	MUTEX_LOCK(&g_lock);
	seen_expire(now);
	i = seen_touch(hex, now);
	MUTEX_UNLOCK(&g_lock);

	return (void*)(intptr_t)(i + 1);
}

void Heartbeat_SeenName(void* handle, unsigned int host, const char* name)
{
	char hexConnect[SEEN_HEX_LEN + 1];
	char hex[SEEN_HEX_LEN + 1];
	time_t now;
	int i, existing;

	if (!g_running || !handle || !name) return;

	now = time(NULL);
	seen_hash(host, NULL, hexConnect);
	seen_hash(host, name, hex);
	i = (int)(intptr_t)handle - 1;

	MUTEX_LOCK(&g_lock);
	/* The connect-time record is only this peer's if it still holds the
	   address-only hash: the slot may have been recycled, or the week rolled. */
	if (i >= 0 && i < OCTR_HEARTBEAT_SEEN_MAX && strcmp(g_seen[i].hex, hexConnect) == 0)
	{
		existing = seen_find(hex);
		if (existing >= 0)
		{
			/* Already counted this week under this address and name: keep that
			   record, release the one made at connect. */
			g_seen[existing].lastSeen = now;
			g_seen[i].hex[0] = '\0';
		}
		else
		{
			memcpy(g_seen[i].hex, hex, SEEN_HEX_LEN + 1);
			g_seen[i].lastSeen = now;
		}
	}
	else
	{
		seen_touch(hex, now);
	}
	MUTEX_UNLOCK(&g_lock);
}

static void build_json(const OctrStatus* s, const char* seenList, int seenCount, const char* weekList, int weekCount,
	const char* bansList, char* out, size_t outSize)
{
	int i;
	size_t used;

	used = (size_t)snprintf(out, outSize,
		"{\"id\":\"%s\",\"total\":%d,\"version\":%d,\"build\":%d,\"uptime\":%ld,\"rooms\":[",
		g_id, s->total, s->version, s->build, s->uptimeSec);

	for (i = 0; i < OCTR_HEARTBEAT_ROOMS && used < outSize; i++)
		used += (size_t)snprintf(out + used, outSize - used,
			"%s{\"n\":%d,\"locked\":%d}",
			(i == 0) ? "" : ",", s->rooms[i], s->locked[i] ? 1 : 0);

	if (used < outSize)
		used += (size_t)snprintf(out + used, outSize - used, "],\"seen24h\":%d,\"seen\":[", seenCount);

	if (used < outSize)
		used += (size_t)snprintf(out + used, outSize - used, "%s],\"seen7d\":%d,\"seenWeek\":[", seenList,
			seenCount + weekCount);

	if (used < outSize)
		used += (size_t)snprintf(out + used, outSize - used, "%s],\"bans\":[", weekList);

	/* the anti-cheat bans (1115): the site keeps the union and sends it back */
	if (used < outSize)
		snprintf(out + used, outSize - used, "%s]}", bansList);
}

static void post_once(void)
{
	OctrStatus snapshot;
	/* body: rooms ~1 KB plus up to OCTR_HEARTBEAT_SEEN_MAX quoted hashes at
	   19 bytes each, split between the two lists; signing wraps it with id,
	   ts and nonce. */
	static char seenList[OCTR_HEARTBEAT_SEEN_MAX * (SEEN_HEX_LEN + 3) + 16];
	static char weekList[OCTR_HEARTBEAT_SEEN_MAX * (SEEN_HEX_LEN + 3) + 16];
	static char bansList[HEARTBEAT_BANS_MAX * 48 + 16];
	static char body[sizeof(seenList) + sizeof(bansList) + 2048];
	static char signing[sizeof(body) + 256];
	int seenCount, weekCount;
	char sig[65];
	char hdrTs[64], hdrSig[96], hdrId[96], hdrNonce[64];
	char nonce[48];
	long long ts;
	CURL* curl;
	struct curl_slist* headers = NULL;
	CURLcode rc;
	long httpCode = 0;

	MUTEX_LOCK(&g_lock);
	snapshot = g_status;
	snapshot.uptimeSec = (long)(time(NULL) - g_startTime);
	g_nonceCounter++;
	snprintf(nonce, sizeof(nonce), "%lld-%lld", (long long)time(NULL), g_nonceCounter);
	seen_expire(time(NULL));
	seenCount = seen_list(seenList, sizeof(seenList), time(NULL), 1);
	weekCount = seen_list(weekList, sizeof(weekList), time(NULL), 0);
	bans_expire(time(NULL));
	bans_list(bansList, sizeof(bansList), time(NULL));
	MUTEX_UNLOCK(&g_lock);

	ts = (long long)time(NULL);
	build_json(&snapshot, seenList, seenCount, weekList, weekCount, bansList, body, sizeof(body));

	/* Sign id + ts + nonce + body together. Binding all four means a captured
	   request cannot be replayed against a different server id or a later
	   time window. */
	snprintf(signing, sizeof(signing), "%s.%lld.%s.%s", g_id, ts, nonce, body);
	hmac_sha256(g_secret, signing, sig);

	curl = curl_easy_init();
	if (!curl) return;

	snprintf(hdrId,    sizeof(hdrId),    "X-OCTR-Id: %s", g_id);
	snprintf(hdrTs,    sizeof(hdrTs),    "X-OCTR-Ts: %lld", ts);
	snprintf(hdrNonce, sizeof(hdrNonce), "X-OCTR-Nonce: %s", nonce);
	snprintf(hdrSig,   sizeof(hdrSig),   "X-OCTR-Sig: %s", sig);

	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, hdrId);
	headers = curl_slist_append(headers, hdrTs);
	headers = curl_slist_append(headers, hdrNonce);
	headers = curl_slist_append(headers, hdrSig);

	curl_easy_setopt(curl, CURLOPT_URL, g_url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sink);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "OnlineCTR-Server/1.0");

	/* Bounded so a hung endpoint can never wedge the worker thread. */
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	/* Explicit rather than relying on defaults - this is the whole reason for
	   choosing libcurl over a hand-rolled socket. */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

	g_replyLen = 0;
	g_reply[0] = '\0';
	rc = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	if (rc == CURLE_OK && httpCode == 200)
		bans_from_reply(time(NULL));   /* every server's anti-cheat bans (1115) */

	/* Only complain on failure. A healthy heartbeat must stay silent or it
	   drowns the room logs. */
	if (rc != CURLE_OK)
	{
		PrintPrefix(-1);
		printf("Heartbeat: post failed (%s)\n", curl_easy_strerror(rc));
	}
	else if (httpCode != 200)
	{
		PrintPrefix(-1);
		printf("Heartbeat: endpoint returned HTTP %ld\n", httpCode);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg)
#else
static void* worker(void* arg)
#endif
{
	(void)arg;

	/* The first post waits for the main loop's first Heartbeat_Publish (its
	   first tick, right after start-up). Posted at once, it carried the
	   zeroed status - version 0, no players - and the site showed that for a
	   whole interval after every restart. */
	for (;;)
	{
		int published;
		MUTEX_LOCK(&g_lock);
		published = g_published;
		MUTEX_UNLOCK(&g_lock);
		if (published || !g_running) break;
		SLEEP_SEC(1);
	}

	while (g_running)
	{
		post_once();
		SLEEP_SEC(g_intervalSec);
	}
#ifdef _WIN32
	return 0;
#else
	return NULL;
#endif
}

int Heartbeat_Init(void)
{
	const char* secret = getenv("OCTR_HEARTBEAT_SECRET");
	const char* url    = getenv("OCTR_HEARTBEAT_URL");
	const char* id     = getenv("OCTR_HEARTBEAT_ID");
	const char* iv     = getenv("OCTR_HEARTBEAT_INTERVAL");
	const char* seenKey = getenv("OCTR_HEARTBEAT_SEEN_KEY");

	/* Both or nothing: no address is compiled in. Which one is missing is
	   said, for a host that set one of them (an older server needed no URL). */
	if (!secret || !*secret || !url || !*url)
	{
		PrintPrefix(-1);
		if (secret && *secret)
			printf("Heartbeat: Not Enabled (OCTR_HEARTBEAT_URL is not set)\n");
		else if (url && *url)
			printf("Heartbeat: Not Enabled (OCTR_HEARTBEAT_SECRET is not set)\n");
		else
			printf("Heartbeat: Not Enabled\n");

		return 0;
	}

	snprintf(g_secret, sizeof(g_secret), "%s", secret);
	snprintf(g_url, sizeof(g_url), "%s", url);
	snprintf(g_id, sizeof(g_id), "%s", (id && *id) ? id : "unknown");
	snprintf(g_seenKey, sizeof(g_seenKey), "%s", (seenKey && *seenKey) ? seenKey : "");
	if (!g_seenKey[0])
		printf("Heartbeat: no OCTR_HEARTBEAT_SEEN_KEY: the anti-cheat bans shared through the site will not match the other servers'\n");

	if (iv && *iv)
	{
		g_intervalSec = atoi(iv);
		/* Floor it: a tight interval would hammer the site and, more to the
		   point, spend the worker's life in TLS handshakes. */
		if (g_intervalSec < HEARTBEAT_INTERVAL_MIN_SECONDS) g_intervalSec = HEARTBEAT_INTERVAL_MIN_SECONDS;
	}

	MUTEX_INIT(&g_lock);
	memset(&g_status, 0, sizeof(g_status));
	memset(g_seen, 0, sizeof(g_seen));
	g_startTime = time(NULL);

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
	{
		PrintPrefix(-1);
		printf("Heartbeat: 'curl_global_init' Failed (Heartbeat Disabled)\n");

		return 0;
	}

	g_running = 1;

#ifdef _WIN32
	{
		HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
		if (h == NULL)
		{
			PrintPrefix(-1);
			printf("Heartbeat: Failed to start the worker thread\n");

			g_running = 0;

			return 0;
		}

		CloseHandle(h);
	}
#else
	{
		pthread_t t;
		if (pthread_create(&t, NULL, worker, NULL) != 0)
		{
			PrintPrefix(-1);
			printf("Heartbeat: Failed to start the worker thread\n");

			g_running = 0;

			return 0;
		}

		pthread_detach(t);
	}
#endif

	PrintPrefix(-1);
	printf("Heartbeat: Enabled (updating as \"%s\" to \"%s\" every %d seconds)\n", g_id, g_url, g_intervalSec);

	PrintPrefix(-1);
	printf("Heartbeat: Player seen hashes are keyed with %s\n", g_seenKey[0]
		? "OCTR_HEARTBEAT_SEEN_KEY (shared where a player is one hash across servers)"
		: "This server's secret (set OCTR_HEARTBEAT_SEEN_KEY on every server to de-duplicate across them)");

	return 1;
}

void Heartbeat_Publish(const OctrStatus* status)
{
	if (!g_running || !status) return;

	MUTEX_LOCK(&g_lock);
	g_status = *status;
	g_published = 1;
	MUTEX_UNLOCK(&g_lock);
}

void Heartbeat_Shutdown(void)
{
	if (!g_running) return;
	g_running = 0;
	curl_global_cleanup();
}

#else /* !OCTR_HEARTBEAT */

// Nothing above is compiled in this configuration, so NULL has to come from
// somewhere: without this the no-libcurl build failed on Linux right here.
#include <stddef.h>

int   Heartbeat_Init(void)                          { return 0; }
void  Heartbeat_Publish(const OctrStatus* s)        { (void)s; }
void  Heartbeat_Shutdown(void)                      { }
void* Heartbeat_SeenConnect(unsigned int host)      { (void)host; return NULL; }
void  Heartbeat_SeenName(void* h, unsigned int host, const char* n) { (void)h; (void)host; (void)n; }
void  Heartbeat_BanPublish(unsigned int host)       { (void)host; }
int   Heartbeat_Banned(unsigned int host)           { (void)host; return 0; }

#endif /* OCTR_HEARTBEAT */
