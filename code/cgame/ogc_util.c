//Copyright (C) 2002 noskill
#include"cg_local.h"
#include"engine.h"

int OGC_EntityIsVisible(centity_t * ent)
{
	trace_t t;

	trap_CM_BoxTrace(&t, cg.refdef.vieworg, ent->predictedOrigin, NULL, NULL, 0, MASK_SOLID);
	if (t.fraction != 1.0f) {
		//CG_Printf("Aim_EntityIsVisible: 0\n");
		return 0;
	}

	//CG_Printf("Aim_EntityIsVisible: 1\n");
	return 1;
}

int OGC_CheckFov(vec3_t origin)
{
	vec3_t vec, ang;

	VectorSubtract(origin, cg.refdef.vieworg, vec);
	vectoangles(vec, ang);
	AnglesSubtract(ang, cg.refdefViewAngles, ang);
	if ((ang[0] * ang[0] + ang[1] * ang[1]) <= (cg_fov.value * cg_fov.value)) {
		//CG_Printf("Aim_CheckFov: 1\n");
		return 1;
	}

	//CG_Printf("Aim_CheckFov: 0\n");
	return 0;
	//This is used relatively, so we do not need sqrt
}

vec_t OGC_AngleToPoint(vec3_t origin)
{
	vec3_t vec, ang;

	if (ogc_mode.integer) {
		VectorSubtract(origin, cg.refdef.vieworg, vec);
		vectoangles(vec, ang);
		AnglesSubtract(ang, cg.refdefViewAngles, ang);
		return (ang[0] * ang[0] + ang[1] * ang[1]);
	}
	VectorSubtract(origin, cg.refdef.vieworg, vec);
	return (vec[0] * vec[0] * vec[1] * vec[1] + vec[2] * vec[2]);
}

#if 0
float radar_colors[][3] = {
	{1.0f, 1.0f, 1.0f},
	{1.0f, 0.0f, 0.0f},
	{0.0f, 0.0f, 1.0f}
};

int OGC_Radar(centity_t * cent, float *color, int *screen)
{
	vec3_t vec, rot;
	float distance;

	if (cent->currentState.eFlags & EF_DEAD)
		return 0;
	VectorCopy(radar_colors[cgs.clientinfo[cent->currentState.clientNum].team], color);
	VectorSubtract(cent->lerpOrigin, cg.refdef.vieworg, vec);
	vec[2] = 0.0f;
	distance = VectorLength(vec) * 0.1;
	vectoangles(vec, rot);
	rot[1] = AngleNormalize180(rot[1] - cg.refdefViewAngles[1]);
	AngleVectors(rot, vec, 0, 0);
	VectorScale(vec, distance, vec);
	if (vec[0] > 100.0f)
		VectorScale(vec, 100.0f / vec[0], vec);
	else if (vec[0] < -100.0f)
		VectorScale(vec, -100.0f / vec[0], vec);
	if (vec[1] > 100.0f)
		VectorScale(vec, 100.0f / vec[1], vec);
	else if (vec[1] < -100.0f)
		VectorScale(vec, -100.0f / vec[1], vec);
	screen[0] = (int)-vec[1] + 125;
	screen[1] = (int)-vec[0] + 200;

	return 1;
}
#endif
