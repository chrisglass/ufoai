/**
 * @file vid_dll.c
 * @brief Main windowed and fullscreen graphics interface module.
 * @note This module is used for the OpenGL rendering versions of the UFO refresh engine.
 */

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

#include <assert.h>
#include <float.h>

#include "../../client/client.h"
#include "win_local.h"

#if defined DEBUG && defined _MSC_VER >= 1300
#include <intrin.h>
#endif

LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* Structure containing functions exported from refresh DLL */
refexport_t re;

static cvar_t *win_noalttab;

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  /* message that will be supported by the OS */
#endif

#ifndef SPI_SCREENSAVERRUNNING
#define SPI_SCREENSAVERRUNNING 97
#endif

static UINT MSH_MOUSEWHEEL;

/* Console variables that we need to access from this module */
extern cvar_t *vid_xpos;			/* X coordinate of window position */
extern cvar_t *vid_ypos;			/* Y coordinate of window position */
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_ref;			/* Name of Refresh DLL loaded */

/* Global variables used internally by this module */
extern viddef_t viddef;				/* global video state; used by other modules */
HINSTANCE	reflib_library;		/* Handle to refresh DLL */
qboolean	reflib_active = qfalse;

LRESULT WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static qboolean s_alttab_disabled = qfalse;

extern uint32_t sys_msg_time;

extern qboolean s_win95, s_winxp, s_vista;

/**
 * @brief
 */
static void WIN_DisableAltTab (void)
{
	if (s_alttab_disabled)
		return;

	if (s_win95) {
		BOOL old;
		SystemParametersInfo(SPI_SCREENSAVERRUNNING, 1, &old, 0);
	} else {
		RegisterHotKey(0, 0, MOD_ALT, VK_TAB);
		RegisterHotKey(0, 1, MOD_ALT, VK_RETURN);
	}
	s_alttab_disabled = qtrue;
}

/**
 * @brief
 */
static void WIN_EnableAltTab (void)
{
	if (s_alttab_disabled) {
		if (s_win95) {
			BOOL old;
			SystemParametersInfo(SPI_SCREENSAVERRUNNING, 0, &old, 0);
		} else {
			UnregisterHotKey(0, 0);
			UnregisterHotKey(0, 1);
		}
		s_alttab_disabled = qfalse;
	}
}

/*
==========================================================================
DLL GLUE
==========================================================================
*/

/**
 * @brief
 */
void AppActivate (BOOL fActive, BOOL minimize)
{
	Minimized = minimize;

	Key_ClearStates();

	/* we don't want to act like we're active if we're minimized */
	if (fActive && !Minimized)
		ActiveApp = qtrue;
	else
		ActiveApp = qfalse;

	/* minimize/restore mouse-capture on demand */
	if (!ActiveApp) {
		IN_Activate(qfalse);
		CDAudio_Activate(qfalse);
		S_Activate(qfalse);

		if (win_noalttab->integer)
			WIN_EnableAltTab();
	} else {
		IN_Activate(qtrue);
		CDAudio_Activate(qtrue);
		S_Activate(qtrue);
		if (win_noalttab->integer)
			WIN_DisableAltTab();
	}
}

/**
 * @brief main window procedure
 */
LRESULT WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam)
{
	LRESULT lRet = 0;

	if (uMsg == MSH_MOUSEWHEEL) {
		if (((int)wParam) > 0) {
			Key_Event(K_MWHEELUP, qtrue, sys_msg_time);
			Key_Event(K_MWHEELUP, qfalse, sys_msg_time);
		} else {
			Key_Event(K_MWHEELDOWN, qtrue, sys_msg_time);
			Key_Event(K_MWHEELDOWN, qfalse, sys_msg_time);
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	switch (uMsg) {
	case WM_MOUSEWHEEL:
		/*
		** this chunk of code theoretically only works under NT4 and Win98
		** since this message doesn't exist under Win95
		*/
		if ((short)HIWORD(wParam) > 0) {
			Key_Event(K_MWHEELUP, qtrue, sys_msg_time);
			Key_Event(K_MWHEELUP, qfalse, sys_msg_time);
		} else {
			Key_Event(K_MWHEELDOWN, qtrue, sys_msg_time);
			Key_Event(K_MWHEELDOWN, qfalse, sys_msg_time);
		}
		break;

	case WM_HOTKEY:
		return 0;

	case WM_CREATE:
		cl_hwnd = hWnd;

		MSH_MOUSEWHEEL = RegisterWindowMessage("MSWHEEL_ROLLMSG");
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_PAINT:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_DESTROY:
		/* let sound and input know about this? */
		cl_hwnd = NULL;
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_ACTIVATE:
		{
			int	fActive, fMinimized;

			/* KJB: Watch this for problems in fullscreen modes with Alt-tabbing. */
			fActive = LOWORD(wParam);
			fMinimized = (BOOL) HIWORD(wParam);

			AppActivate(fActive != WA_INACTIVE, fMinimized);

			if (reflib_active)
				re.AppActivate(!(fActive == WA_INACTIVE));
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_MOVE:
		{
			int		xPos, yPos;
			RECT r;
			int		style;

			if (!vid_fullscreen->integer) {
				xPos = (short) LOWORD(lParam);    /* horizontal position */
				yPos = (short) HIWORD(lParam);    /* vertical position */

				r.left   = 0;
				r.top    = 0;
				r.right  = 1;
				r.bottom = 1;

				style = GetWindowLong(hWnd, GWL_STYLE);
				AdjustWindowRect(&r, style, FALSE);

				Cvar_SetValue("vid_xpos", xPos + r.left);
				Cvar_SetValue("vid_ypos", yPos + r.top);
				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
				if (ActiveApp)
					IN_Activate(qtrue);
			}
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	/* this is complicated because Win32 seems to pack multiple mouse events into */
	/* one update sometimes, so we always check all states and look for events */
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		{
			int	temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

			IN_MouseEvent (temp);
		}
		break;

	case WM_SYSCOMMAND:
		if (wParam == SC_SCREENSAVE)
			return 0;
		return DefWindowProc (hWnd, uMsg, wParam, lParam);
	case WM_SYSKEYDOWN:
		if (wParam == 13) {
			if (vid_fullscreen)
				Cvar_SetValue("vid_fullscreen", !vid_fullscreen->value);
			return 0;
		}
		/* fall through */
	case WM_KEYDOWN:
		Key_Event(IN_MapKey(wParam, lParam), qtrue, sys_msg_time);
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		Key_Event(IN_MapKey(wParam, lParam), qfalse, sys_msg_time);
		break;

	case MM_MCINOTIFY:
		lRet = CDAudio_MessageHandler(hWnd, uMsg, wParam, lParam);
		break;

	default:	/* pass all unhandled messages to DefWindowProc */
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

	/* return 0 if handled message, 1 if not */
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/**
 * @brief
 */
static void VID_Front_f (void)
{
	SetWindowLong(cl_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST);
	SetForegroundWindow(cl_hwnd);
}

int maxVidModes;
/**
 * @brief
 */
const vidmode_t vid_modes[] =
{
	{ 320, 240,   0 },
	{ 400, 300,   1 },
	{ 512, 384,   2 },
	{ 640, 480,   3 },
	{ 800, 600,   4 },
	{ 960, 720,   5 },
	{ 1024, 768,  6 },
	{ 1152, 864,  7 },
	{ 1280, 1024, 8 },
	{ 1600, 1200, 9 },
	{ 2048, 1536, 10 },
	{ 1024,  480, 11 }, /* Sony VAIO Pocketbook */
	{ 1152,  768, 12 }, /* Apple TiBook */
	{ 1280,  854, 13 }, /* Apple TiBook */
	{ 640,  400, 14 }, /* generic 16:10 widescreen*/
	{ 800,  500, 15 }, /* as found modern */
	{ 1024,  640, 16 }, /* notebooks    */
	{ 1280,  800, 17 },
	{ 1680, 1050, 18 },
	{ 1920, 1200, 19 },
	{ 1400, 1050, 20 },
	{ 1440, 900, 21 }
};

/**
 * @brief Moves the window to the given location
 */
static void VID_UpdateWindowPosAndSize (int x, int y)
{
	RECT r;
	int		style;
	int		w, h;

	r.left   = 0;
	r.top    = 0;
	r.right  = viddef.width;
	r.bottom = viddef.height;

	style = GetWindowLong(cl_hwnd, GWL_STYLE);
	AdjustWindowRect(&r, style, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;

	MoveWindow(cl_hwnd, vid_xpos->value, vid_ypos->value, w, h, TRUE);
}

/**
 * @brief
 */
static void VID_NewWindow (int width, int height)
{
	viddef.width  = width;
	viddef.height = height;
	viddef.rx = (float)width  / VID_NORM_WIDTH;
	viddef.ry = (float)height / VID_NORM_HEIGHT;

	cl.force_refdef = qtrue;		/* can't use a paused refdef */
}

/**
 * @brief
 */
static void VID_FreeReflib (void)
{
	if (reflib_library && !FreeLibrary(reflib_library))
		Com_Error(ERR_FATAL, "Reflib FreeLibrary failed");
	memset(&re, 0, sizeof(re));
	reflib_library = NULL;
	reflib_active  = qfalse;
}

/**
 * @brief
 */
static qboolean VID_LoadRefresh (const char *name)
{
	refimport_t	ri;
	GetRefAPI_t	GetRefAPI;
	qboolean restart = qfalse;

	if (reflib_active) {
		re.Shutdown();
		VID_FreeReflib();
		restart = qtrue;
	}

	Com_Printf("------- Loading %s -------\n", name);

	if ((reflib_library = Sys_LoadLibrary(name, 0)) == 0) {
		Com_Printf("LoadLibrary(\"%s\") failed\n", name);
		return qfalse;
	}

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.FS_CreatePath = FS_CreatePath;
	ri.FS_LoadFile = FS_LoadFile;
	ri.FS_WriteFile = FS_WriteFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_CheckFile = FS_CheckFile;
	ri.FS_ListFiles = FS_ListFiles;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Cvar_ForceSet = Cvar_ForceSet;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_NewWindow = VID_NewWindow;
	ri.CL_WriteAVIVideoFrame = CL_WriteAVIVideoFrame;
	ri.CL_GetFontData = CL_GetFontData;
	ri.RenderTrace = SV_RenderTrace;

	ri.genericPool = &vid_genericPool;
	ri.imagePool = &vid_imagePool;
	ri.lightPool = &vid_lightPool;
	ri.modelPool = &vid_modelPool;

	ri.TagMalloc = VID_TagAlloc;
	ri.TagFree = VID_MemFree;
	ri.FreeTags = VID_FreeTags;

	if ((GetRefAPI = (GetRefAPI_t)Sys_GetProcAddress(reflib_library, "GetRefAPI")) == 0)
		Com_Error(ERR_FATAL, "GetProcAddress failed on %s", name);

	re = GetRefAPI(ri);

	if (re.api_version != API_VERSION) {
		VID_FreeReflib();
		Com_Error(ERR_FATAL, "%s has incompatible api_version", name);
	}

	if (re.Init(global_hInstance, MainWndProc) == qfalse) {
		re.Shutdown();
		VID_FreeReflib();
		return qfalse;
	}

	/* vid_restart */
	if (restart)
		CL_InitFonts();

	Com_Printf("------------------------------------\n");
	reflib_active = qtrue;

	return qtrue;
}

/**
 * @brief This function gets called once just before drawing each frame, and it's sole purpose in life
 * is to check to see if any of the video mode parameters have changed, and if they have to
 * update the rendering DLL and/or video mode to match.
 */
void VID_CheckChanges (void)
{
	char name[MAX_VAR];

	if (win_noalttab->modified) {
		if (win_noalttab->integer)
			WIN_DisableAltTab();
		else
			WIN_EnableAltTab();
		win_noalttab->modified = qfalse;
	}

	if (vid_ref->modified) {
		/* can't use a paused refdef */
		cl.force_refdef = qtrue;
		S_StopAllSounds();
	}
	while (vid_ref->modified) {
		/* refresh has changed */
		vid_ref->modified = qfalse;
		vid_fullscreen->modified = qtrue;
		cl.refresh_prepped = qfalse;
		cls.disable_screen = qtrue;

		/* try generic refresh lib first */
		Com_sprintf(name, sizeof(name), "ref_%s", vid_ref->string);
		if (!VID_LoadRefresh(name)) {
			Cmd_ExecuteString("condump gl_debug");
			Com_Error(ERR_FATAL, "Couldn't initialize OpenGL renderer!\nConsult gl_debug.txt for further information.");
		}
		cls.disable_screen = qfalse;
	}

	/* update our window position */
	if (vid_xpos->modified || vid_ypos->modified) {
		if (!vid_fullscreen->integer)
			VID_UpdateWindowPosAndSize(vid_xpos->integer, vid_ypos->integer);

		vid_xpos->modified = qfalse;
		vid_ypos->modified = qfalse;
	}
}

/**
 * @brief
 * @sa VID_Shutdown
 */
void Sys_Vid_Init (void)
{
	/* Create the video variables so we know how to start the graphics drivers */
	vid_ref = Cvar_Get("vid_ref", "gl", CVAR_ARCHIVE, "Video renderer");
	win_noalttab = Cvar_Get("win_noalttab", "0", CVAR_ARCHIVE, NULL);

	/* Add some console commands that we want to handle */
	Cmd_AddCommand("vid_front", VID_Front_f, "Set UFO:AI window to front");

	maxVidModes = VID_NUM_MODES;
}

/**
 * @brief
 * @sa VID_Init
 */
void VID_Shutdown (void)
{
	if (reflib_active) {
		re.Shutdown();
		VID_FreeReflib();
	}
}


