/*
 * Android input code for Quake3
 * Copyright (C) 2010 Roderick Colenbrander
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "android_glimp.h"
#include "../client/client.h"
#include "../renderer/tr_local.h"
#include "../qcommon/q_shared.h"

/* These variables are for confining menu input to 640x480 */
static float scale_ratio;
static int offset_x;

/* Shared cursor position between touch and trackball code */
static float cursor_x=0, cursor_y=0;

static qboolean motion_event = qfalse;
static float motion_dx=0;
static float motion_dy=0;

static qboolean trackball_event = qfalse;
static float trackball_dx = 0;
static float trackball_dy = 0;

void (*setMenuState)(int shown);

void setInputCallbacks(void *set_menu_state)
{
    setMenuState = set_menu_state;
}

void queueKeyEvent(int key, int state)
{
    int t = Sys_Milliseconds();
    char character = key;

    /* The game distinguishes between key code and character for some keys (not sure which all).
     * Perform such a fixup for backspace. If there are more important keys then we might have to do
     * it somewhere else.
     */
    if(key == K_BACKSPACE)
        character = '\b';

    Com_QueueEvent(t, SE_KEY, key, state, 0, NULL);
    Com_DPrintf("SE_KEY key=%d state=%d\n", key, state);

    /* For ASCII characters we need both a SE_CHAR, so that we can type and a SE_KEY */
    if(state == 1 && (key >= ' ' && key <= 127))
    {
        Com_DPrintf("SE_CHAR key=%d state=%d\n", character, state);
        Com_QueueEvent(t, SE_CHAR, character, 0, 0, NULL);
    }
}

enum Action
{
    ACTION_DOWN=0,
    ACTION_UP=1,
    ACTION_MOVE=2
};


void queueMotionEvent(int action, float x, float y, float pressure)
{
    static float motion_initial_x=0, motion_initial_y=0;
    int t = Sys_Milliseconds();

    static float motion_x_down;
    static float motion_y_down;

    /* The actual motion code is in processMotionEvents. Here we track the movement. On touch down
     * we record the initial coordinates and during movement we store the last coordinate. When the
     * game requests new movement data we provide it with the difference between the initial and final
     * coordinates.
     */

    switch(action)
    {
        case ACTION_DOWN:
            /* When the position change between ACTION_DOWN and ACTION_UP is small we generate a mouse click.
             * Pressure can be used as well but the values differ a lot between devices, so calibration
             * or a list of thresholds would be needed. The position change works reliably.
             */
            motion_x_down = x;
            motion_y_down = y;
            break;

        case ACTION_UP:
            if((fabsf(motion_x_down - x) < 3) && (fabsf(motion_y_down - y) < 3))
            {
                Com_QueueEvent(t, SE_KEY, K_MOUSE1, 1, 0, NULL);
                Com_QueueEvent(t, SE_KEY, K_MOUSE1, 0, 0, NULL);
            }
            break;

        case ACTION_MOVE:
            motion_event = qtrue;
            motion_dx += x-motion_initial_x;
            motion_dy += y-motion_initial_y;
            break;
    }

    motion_initial_x = x;
    motion_initial_y = y;
    cursor_x = x;
    cursor_y = y;
}

/* The quake3 menu is 640x480, make sure the coordinates originating from this area are scaled.
 * The UI code already contains clamping to 640x480, so don't perform it here.
 */
inline int scale_x_input(float x)
{
    return (int)((x - offset_x)*scale_ratio);
}

inline int scale_y_input(float y)
{
    return (int)(y*scale_ratio);
}

static void processMotionEvents(void)
{
    if(motion_event)
    {
        int t = Sys_Milliseconds(); //what time should we use?

        /* For some reason we have to use absolute coordinates in the main menu (CA_DISCONNECTED).
         * The N900 version performs some coordinate translation in cl_input.c
         * when in game but not in other places. I'm not sure why it did that
         * and why he doesn't have to distringuish between relative and absolute while
         * I have.
         */
        if(cls.state == CA_DISCONNECTED)
            Com_QueueEvent(t, SE_MOUSE, scale_x_input(cursor_x), scale_y_input(cursor_y), 0, NULL);
        else
            Com_QueueEvent(t, SE_MOUSE, (int)(motion_dx), (int)(motion_dy), 0, NULL);

        motion_event = qfalse;
        motion_dx = 0;
        motion_dy = 0;
    }
}

void queueTrackballEvent(int action, float dx, float dy)
{
    int t = Sys_Milliseconds(); //what time should we use?
    static int keyPress=0;

    switch(action)
    {
        case ACTION_DOWN:
            Com_QueueEvent(t, SE_KEY, K_MOUSE1, 1, 0, NULL);
            keyPress=1;
            break;
        case ACTION_UP:
            if(keyPress)
                Com_QueueEvent(t, SE_KEY, K_MOUSE1, 0, 0, NULL);
            keyPress=0;
    }

    /* We accumulate all displacements until we draw a new frame. */
    trackball_dx += dx;
    trackball_dy += dy;
    trackball_event = qtrue;
}

inline float clamp_to_screen_width(float x)
{
    if(x > SCREEN_WIDTH)
        return SCREEN_WIDTH;
    else if(x < 0)
        return 0;
    return x;
}

inline float clamp_to_screen_height(float y)
{
    if(y > SCREEN_HEIGHT)
        return SCREEN_HEIGHT;
    else if(y < 0)
        return 0;
    return y;
}

static void processTrackballEvents(void)
{
    if(trackball_event)
    {
        int t = Sys_Milliseconds(); //what time should we use?

        /* Trackball dx/dy are <1.0, so make them a bit bigger to prevent kilometers of scrolling */
        trackball_dx *= 50.0;
        trackball_dy *= 50.0;
        cursor_x += trackball_dx;
        cursor_y += trackball_dy;

        cursor_x = clamp_to_screen_width(cursor_x);
        cursor_y = clamp_to_screen_height(cursor_y);

        if(cls.state == CA_DISCONNECTED)
            Com_QueueEvent(t, SE_MOUSE, (int)cursor_x, (int)cursor_y, 0, NULL);
        else
            Com_QueueEvent(t, SE_MOUSE, (int)trackball_dx, (int)trackball_dy, 0, NULL);

        trackball_event = qfalse;
        trackball_dx = 0;
        trackball_dy = 0;
    }
}

void IN_Frame(void)
{
    static int prev_state = -1;
    int state = -1;
    processMotionEvents();
    processTrackballEvents();

    /* We are in game and neither console/ui is active */
    if (cls.state == CA_ACTIVE && Key_GetCatcher() == 0)
        state = 1;
    else
        state = 0;

    if (state != prev_state)
    {
        setMenuState(state);
        prev_state = state;
    }
}

void IN_Init(void)
{
    scale_ratio = (float)SCREEN_HEIGHT/glConfig.vidHeight;
    offset_x = (glConfig.vidWidth - ((float)SCREEN_WIDTH/scale_ratio))/2;
}

qboolean IN_MotionPressed(void)
{
    return qfalse;
}

void IN_Shutdown(void)
{
}

void IN_Restart(void)
{
    IN_Init();
}
