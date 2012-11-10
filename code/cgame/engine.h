//Copyright (C) 2002 noskill
#ifndef ENGINE_H
#define ENGINE_H

#define ESP_CHAR_WIDTH 6
#define ESP_CHAR_HEIGHT 12

// ogc_util.c
int OGC_EntityIsVisible(centity_t * ent);
int OGC_CheckFov(vec3_t origin);
vec_t OGC_AngleToPoint(vec3_t origin);

// mod_baseq3.c
int VerifyTarget_BQ3(centity_t * ent);
int AddTarget_BQ3(centity_t * current, centity_t * possible);
int DrawEsp_BQ3(centity_t * ent, float *color, int *force);

#endif
