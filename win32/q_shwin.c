/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#define WIN32_LEAN_AND_MEAN
#include "../qcommon/qcommon.h"
#include "winquake.h"
#include <mmsystem.h>
#include <direct.h>
#include <io.h>

//===============================================================================

int		hunkcount;


int		hunkmaxsize;
int		cursize;

#define	VIRTUAL_ALLOC 1
//#define CREATE_HEAP 1

#if CREATE_HEAP
HANDLE	membase;
#else
byte	*membase;
#endif

void *Hunk_Begin (int maxsize)
{
	// reserve a huge chunk of memory, but don't commit any yet
	cursize = 0;
	hunkmaxsize = maxsize;
#if VIRTUAL_ALLOC
	membase = VirtualAlloc (NULL, maxsize, MEM_RESERVE, PAGE_NOACCESS);
#elif CREATE_HEAP
	{
		ULONG lfh = 2;
		membase = HeapCreate (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS, 0, maxsize);
		HeapSetInformation (membase, HeapCompatibilityInformation, &lfh, sizeof(lfh));
	}
#else
	membase = malloc (maxsize);
#endif
	if (!membase)
		Sys_Error ("VirtualAlloc reserve failed");
	return (void *)membase;
}

void *Hunk_Alloc (int size)
{
#if VIRTUAL_ALLOC || CREATE_HEAP
	void	*buf;
#endif

	// round to cacheline
	size = (size+31)&~31;

#if CREATE_HEAP
	buf = HeapAlloc (membase, 0, size);
	if (!buf)
	{
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &buf, 0, NULL);
		Sys_Error ("HeapAlloc failed.\n%s", buf);
	}
	cursize += size;
	if (cursize > hunkmaxsize)
		Sys_Error ("HeapAlloc overflow");
	return buf;
#else

#if VIRTUAL_ALLOC
	// commit pages as needed
//	buf = VirtualAlloc (membase+cursize, size, MEM_COMMIT, PAGE_READWRITE);
	buf = VirtualAlloc (membase, cursize+size, MEM_COMMIT, PAGE_READWRITE);
	if (!buf)
	{
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &buf, 0, NULL);
		Sys_Error ("VirtualAlloc commit failed.\n%s", buf);
	}
#endif
	cursize += size;
	if (cursize > hunkmaxsize)
		Sys_Error ("Hunk_Alloc overflow");

	return (void *)(membase+cursize-size);
#endif
}

int Hunk_End (void)
{

	// free the remaining unused virtual memory
#if VIRTUAL_ALLOC
#if 0
	void	*buf;

	// write protect it
	buf = VirtualAlloc (membase, cursize, MEM_COMMIT, PAGE_READONLY);
	if (!buf)
		Sys_Error ("VirtualAlloc commit failed");
#endif
#else
	/**base = realloc (membase, cursize);
	if (!*base)
		ri.Sys_Error (ERR_FATAL, "realloc (%p, %d) failed", membase, cursize);*/
#endif

	hunkcount++;
//Com_Printf ("hunkcount: %i\n", hunkcount);
	return cursize;
}

void Hunk_Free (void *base)
{
	if ( base )
#if VIRTUAL_ALLOC
		VirtualFree (base, 0, MEM_RELEASE);
#elif CREATE_HEAP
		HeapDestroy (membase);
#else
		free (base);
#endif

	hunkcount--;
}

//===============================================================================


/*
================
Sys_Milliseconds
================
*/
int	curtime;
//int oldcurtime;
#ifndef REF_GL
int Sys_Milliseconds (void)
{
	static int		base;
	static qboolean	initialized = false;

	if (!initialized)
	{	// let base retain 16 bits of effectively random data
		base = timeGetTime() & 0xffff0000;
		initialized = true;
//		oldcurtime = 0;
	}
	
	curtime = timeGetTime() - base;

	/*if (curtime < oldcurtime)
		Com_Printf ("Sys_Milliseconds: Uh oh! Timer wrapped!\n");

	oldcurtime = curtime;*/

	return curtime;
}
#endif

void Sys_Mkdir (char *path)
{
	_mkdir (path);
}

//============================================

//r1: changed to use Win32 API rather than libc

char	findbase[MAX_OSPATH];
char	findpath[MAX_OSPATH];
HANDLE	findhandle;

static qboolean CompareAttributes( DWORD found, uint32 musthave, uint32 canthave )
{
	if ( ( found & FILE_ATTRIBUTE_READONLY ) && ( canthave & SFF_RDONLY ) )
		return false;
	if ( ( found & FILE_ATTRIBUTE_HIDDEN ) && ( canthave & SFF_HIDDEN ) )
		return false;
	if ( ( found & FILE_ATTRIBUTE_SYSTEM ) && ( canthave & SFF_SYSTEM ) )
		return false;
	if ( ( found & FILE_ATTRIBUTE_DIRECTORY ) && ( canthave & SFF_SUBDIR ) )
		return false;
	if ( ( found & FILE_ATTRIBUTE_ARCHIVE ) && ( canthave & SFF_ARCH ) )
		return false;

	if ( ( musthave & SFF_RDONLY ) && !( found & FILE_ATTRIBUTE_READONLY ) )
		return false;
	if ( ( musthave & SFF_HIDDEN ) && !( found & FILE_ATTRIBUTE_HIDDEN ) )
		return false;
	if ( ( musthave & SFF_SYSTEM ) && !( found & FILE_ATTRIBUTE_SYSTEM ) )
		return false;
	if ( ( musthave & SFF_SUBDIR ) && !( found & FILE_ATTRIBUTE_DIRECTORY ) )
		return false;
	if ( ( musthave & SFF_ARCH ) && !( found & FILE_ATTRIBUTE_ARCHIVE ) )
		return false;

	return true;
}

char *Sys_FindFirst (char *path, uint32 musthave, uint32 canthave )
{
	WIN32_FIND_DATA	findinfo;

	if (findhandle)
		Sys_Error ("Sys_BeginFind without close");

	COM_FilePath (path, findbase);
	findhandle = FindFirstFile (path, &findinfo);

	if (findhandle == INVALID_HANDLE_VALUE)
		return NULL;

	if (!CompareAttributes( findinfo.dwFileAttributes, musthave, canthave ) )
		return NULL;

	Com_sprintf (findpath, sizeof(findpath), "%s/%s", findbase, findinfo.cFileName);
	return findpath;
}

char *Sys_FindNext ( uint32 musthave, uint32 canthave )
{
	WIN32_FIND_DATA	findinfo;

	if (findhandle == INVALID_HANDLE_VALUE)
		return NULL;

	if (!FindNextFile (findhandle, &findinfo))
		return NULL;

	if (!CompareAttributes( findinfo.dwFileAttributes, musthave, canthave ) )
		return NULL;

	Com_sprintf (findpath, sizeof(findpath), "%s/%s", findbase, findinfo.cFileName);
	return findpath;
}

void Sys_FindClose (void)
{
	if (findhandle != INVALID_HANDLE_VALUE)
		FindClose (findhandle);

	findhandle = 0;
}

#ifdef _WIN32
#ifdef _DEBUG
#include <windows.h>
LARGE_INTEGER start;
double totalTime = 0;
void _START_PERFORMANCE_TIMER (void)
{
	QueryPerformanceCounter (&start);
}
void _STOP_PERFORMANCE_TIMER (void)
{
	double res;
	LARGE_INTEGER stop;
	__int64 diff;
	LARGE_INTEGER freq;
	QueryPerformanceCounter (&stop);
	QueryPerformanceFrequency (&freq);
	diff = stop.QuadPart - start.QuadPart;
	res = ((double)((double)diff / (double)freq.QuadPart));
	Com_Printf ("Function executed in %.5f secs.\n", LOG_GENERAL, res);
	totalTime += res;
}
#endif
#endif

/*
::/ \::::::.
:/___\:::::::.
/|    \::::::::.
:|   _/\:::::::::.
:| _|\  \::::::::::.                                               Feb/March 98
:::\_____\::::::::::.                                              Issue      3
::::::::::::::::::::::.........................................................

            A S S E M B L Y   P R O G R A M M I N G   J O U R N A L
                      http://asmjournal.freeservers.com
                           asmjournal@mailcity.com

____________________________________________________________________________
  ::::::::::.___    .                                                       ```
  ::::::::::| _/__. |__   ____    .      __.  ____  ____   __.               \\
  ::::::    |____ | __/_ _\_ (.___|  .___) |__\_ (._)  /___) |                ,
  ::::::::::/   / | \   |  -  |   \  |  -  |   -  |  \/|  -  |
.=:::::::::/______|_____|_____|  (___|_____|______|____|_____|===============.
'=::::::::::==================|   . ____ | (____====[ The C Standard lib ]==='
  ::::::::::                  |   |------|  -   |                       
  ::::::::::                  |   |______|______|CE                     
                              .   :
                                      C string functions: introduction, _strlen
                                      by Xbios2						   
*/

//r1: using this as for some reason with MSVC, using -O2 inlines a shitty byte-scanning strlen!
//this is 2x faster on strings of ~ 10+ bytes. its still a little faster than the strlen.asm too.

//XXX: this breaks on strings that contain high ascii chars! not safe to use for measuring string
//length to strcpy() into.
size_t __cdecl fast_strlen(const char *s)
{
    __asm
	{
		; ------------ version 8 ------------
		; My fast version
		; 92 bytes
		; c=17+1*n

			mov	eax, [s]			;grab string to eax
			xor	ecx, ecx				;ecx = 0 FIXME? why is this needed?
			test	al, 3				;ive no idea how this works. something to do with alignment?
			jz	loop1
			cmp	byte ptr [eax], cl		;empty string
			jz	short ret0
			cmp	byte ptr [eax+1], cl	;string of length 1
			jz	short ret1
			cmp	byte ptr [eax+2], cl	;if 3rd char isn't zero, adjust?
			jnz	short adjust
			inc	eax						;3rd char is zero, so inc counter

		ret1:
			inc	eax						;inc counter

		ret0:
			sub	eax, [s]			;subtract pointer so eax stores number of chars
			jmp short gotlength

		adjust:
			add	eax, 3					;eax now points to 3rd char
			and	eax, 0FFFFFFFCh			;what the hell does this do? eax AND 11111111111111111111111111111100? alignment related?

		loop1:
			mov	edx, [eax]				;load up edx
			mov	ecx, 81010100h			;ecx = 10000001 00000001 00000001 00000000
			sub	ecx, edx				;ecx = ecx - edx
			add	eax, 4					;increment string pointer
			xor	ecx, edx				;test what bits got set???
			and	ecx, 81010100h			;flip them back???
			jz	loop1					;not end of string?
			sub	eax, [s]			;end of string - subtract address
			shr	ecx, 9					;bit shift for extra confusion
			jc	minus4					;jump if char fell off, all 4 bytes set?
			shr	ecx, 8					;more shifting
			jc	minus3					;jump if something fell off, 3 bytes?
			shr	ecx, 8					;shifty shift
			jc	minus2					;2nd byte fell off
			dec	eax						;obviously not, so 1 byte
			//ret							;return result
			jmp short gotlength

		minus4:
			sub	eax, 4
			jmp short gotlength
			//ret	

		minus3:
			sub	eax, 3
			jmp short gotlength
			//ret	

		minus2:
			sub	eax, 2
			//ret	
		gotlength:
	}
}

//fast_strlwr, adapted from http://www.assembly-journal.com/viewarticle.php?id=131&layout=html
//since it only writes to edi if modified, this is faster for strings that are mostly lower.
void __cdecl fast_strlwr(char *s)
{
	__asm
	{
			mov esi, [s]					;get string
			mov edi, esi					;destination
		loop1:
			lodsb							;load byte
			cmp    al, 5Ah					;greater than 'Z'
			ja     short store2
			cmp    al, 41h					;less than 'A'
			jb     short store1
			or     al, 00100000b			;to lower (+32)
			stosb							;store
			jmp    short loop1				;loop
		store1:
			cmp    al, 0h					;end of string
			je     short finish1
		store2:
			inc		edi						;no change, increment pointer
			jmp    short loop1					;loop
		finish1:
	}
}

//i made this one all by myself !
int __cdecl fast_tolower(int c)
{
	__asm
	{
			mov eax, [esp+4]		;get character
			cmp	eax, 5Ah
			ja  short finish1

			cmp	eax, 41h
			jb  short finish1

			or  eax, 00100000b		;to lower (+32)
		finish1:
	}
}
//============================================

