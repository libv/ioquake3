/*
 * Copyright (C) 2009  Nokia Corporation.  All rights reserved.
 */

#ifndef __ANDROID_GLIMP_H__
#define __ANDROID_GLIMP_H__

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include <GLES/gl.h>

#include "android_input.h"

#ifndef GLAPI
#define GLAPI extern
#endif

#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif

#ifndef APIENTRY
#define APIENTRY GLAPIENTRY
#endif

/* "P" suffix to be used for a pointer to a function */
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GLAPIENTRYP
#define GLAPIENTRYP GLAPIENTRY *
#endif

void GLimp_Init(void);
void GLimp_LogComment(char *comment);
void GLimp_EndFrame(void);
void GLimp_Shutdown(void);
void qglArrayElement(GLint i);
void qglCallList(GLuint list);
void qglDrawBuffer(GLenum mode);
void qglLockArrays(GLint i, GLsizei size);
void qglUnlockArrays(void);
void GLimp_SetGamma(unsigned char red[256], unsigned char green[256],
		    unsigned char blue[256]);
qboolean GLimp_SpawnRenderThread(void (*function) (void));
void GLimp_FrontEndSleep(void);
void *GLimp_RendererSleep(void);
void GLimp_RenderThreadWrapper(void *data);
void GLimp_WakeRenderer(void *data);

#endif
