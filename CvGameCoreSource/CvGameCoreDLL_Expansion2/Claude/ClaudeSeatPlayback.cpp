#include "CvGameCoreDLLPCH.h"
#include "ClaudeSeatPlayback.h"
#include "ClaudeSeat.h"
#include "ClaudeToolSupport.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvGlobals.h"

namespace
{
	// The engine's "pause message playback" code, found in its executable's code at startup:
	//   push 0 / push 1 / push <lock> / call reg / test eax,eax / jnz back   (spin until the lock is taken)
	//   push <counter> / mov byte ptr [<paused>],1                           (paused = 1, then count it)
	// 0x100 marks a byte that may differ between builds. <counter> is the global right after the
	// paused byte; what it counts is unverified, it only serves as a fingerprint.
	const int ANY = 0x100;
	const int PAUSE_PATTERN[] =
	{
		0x6A, 0x00, 0x6A, 0x01, 0x68, ANY, ANY, ANY, ANY, 0xFF, ANY, 0x85, 0xC0, 0x75, ANY,
		0x68, ANY, ANY, ANY, ANY, 0xC6, 0x05, ANY, ANY, ANY, ANY, 0x01
	};
	const int PATTERN_LENGTH = sizeof(PAUSE_PATTERN) / sizeof(PAUSE_PATTERN[0]);
	const int LOCK_AT = 5, CALL_REG_AT = 10, COUNTER_AT = 16, PAUSED_AT = 22;

	volatile LONG* s_readerLock = NULL;   // taken by the message reader while it runs, and held while playback is paused
	volatile const char* s_paused = NULL; // playback is paused
	bool s_releasedThisPause = false;

	DWORD ReadDword(const BYTE* p)
	{
		return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
	}

	bool MatchesAt(const BYTE* p)
	{
		for(int i = 0; i < PATTERN_LENGTH; ++i)
			if(PAUSE_PATTERN[i] != ANY && PAUSE_PATTERN[i] != p[i]) return false;
		return (p[CALL_REG_AT] & 0xF8) == 0xD0;   // FF D0..D7 is "call reg"; other FF forms are not
	}

	bool InsideImage(const BYTE* base, const IMAGE_NT_HEADERS* nt, DWORD address, DWORD size)
	{
		return address >= (DWORD)base && address + size <= (DWORD)base + nt->OptionalHeader.SizeOfImage;
	}

	// Finds the pause code in the executable's code sections; it must appear exactly once, the
	// pause counter must sit right after the paused byte, and both globals must lie inside the
	// executable's image, or the engine is not one this was made for.
	bool FindPauseCode()
	{
		const BYTE* base = (const BYTE*)GetModuleHandleA(NULL);
		const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
		const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
		const BYTE* match = NULL;
		int matches = 0;
		for(int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec)
		{
			if(!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
			const BYTE* p = base + sec->VirtualAddress;
			const BYTE* end = p + sec->Misc.VirtualSize - PATTERN_LENGTH;
			for(; p <= end; ++p)
			{
				if(!MatchesAt(p)) continue;
				++matches;
				if(matches == 1) match = p;
			}
		}
		if(matches != 1)
		{
			ClaudeDiagLog(ClaudeFormat("Seat turns will not play live: the engine's playback pause code was found %d times instead of once", matches));
			return false;
		}
		DWORD lock = ReadDword(match + LOCK_AT);
		DWORD paused = ReadDword(match + PAUSED_AT);
		if(ReadDword(match + COUNTER_AT) != paused + 1 || (lock & 3) != 0 || !InsideImage(base, nt, lock, 4) || !InsideImage(base, nt, paused, 2))
		{
			ClaudeDiagLog("Seat turns will not play live: the engine's playback pause code does not have the expected shape");
			return false;
		}
		s_readerLock = (volatile LONG*)lock;
		s_paused = (volatile const char*)paused;
		ClaudeDiagLog(ClaudeFormat("Seat turns play live: the engine's playback pause code is at +0x%X (lock +0x%X, paused flag +0x%X)", match - base, lock - (DWORD)base, paused - (DWORD)base));
		return true;
	}

	bool SeatTurnOpen()
	{
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
			if(ClaudeIsSeat((PlayerTypes)i) && GET_PLAYER((PlayerTypes)i).isTurnActive()) return true;
		return false;
	}

	// An eliminated human watching on never gets a turn, so the engine's own resume, which
	// comes with the screen player's turn, never comes either.
	bool ScreenIsObserver()
	{
		PlayerTypes eActive = GC.getGame().getActivePlayer();
		return eActive != NO_PLAYER && CvPreGame::slotStatus(eActive) == SS_OBSERVER;
	}
}

void ClaudeKeepSeatTurnVisible()
{
	static bool s_found = FindPauseCode();
	if(!s_found) return;
	if(!*s_paused)
	{
		s_releasedThisPause = false;
		return;
	}
	if(s_releasedThisPause || !(SeatTurnOpen() || ScreenIsObserver())) return;
	// The engine's per-frame pause decision only acts when its wanted state differs from the paused
	// flag, so with the flag left set it neither re-takes the lock nor notices; its own resume when
	// the turn passes on clears both as usual. Released once per pause: after that the lock is the
	// reader's own. A pause that ends and restarts between two frames is missed.
	LONG was = InterlockedCompareExchange(s_readerLock, 0, 1);
	if(was != 1) ClaudeDiagLog(ClaudeFormat("Seat turn playback: the engine's reader lock held %d, not 1, when released", was));
	s_releasedThisPause = true;
}
