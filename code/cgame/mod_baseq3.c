//Copyright (C) 2002 noskill
#include"cg_local.h"
#include"engine.h"

static qboolean OGC_IsDead(centity_t * ent)
{
	return (qboolean) (ent->currentState.eFlags & EF_DEAD);
}

static qboolean OGC_IsOnSameTeam(centity_t * ent)
{
	clientInfo_t *self = &cgs.clientinfo[cg.snap->ps.clientNum];
	clientInfo_t *other = &cgs.clientinfo[ent->currentState.clientNum];

	if (cgs.gametype != GT_TEAM || cgs.gametype != GT_CTF)
		return qfalse;

	if (self->team == other->team)
		return qtrue;

	return qfalse;
}


int VerifyTarget_BQ3(centity_t * ent)
{
	if (OGC_IsDead(ent))
		return 0;
	if (!OGC_CheckFov(ent->predictedOrigin))
		return 0;
	if (OGC_IsOnSameTeam(ent))
		return 0;

	if (!ogc_ignorewalls.integer && !ent->visible)
		return 0;

	return 1;
}

int AddTarget_BQ3(centity_t * current, centity_t * possible)
{
	qboolean currentVisible = qfalse;

	VectorMA(possible->lerpOrigin, (cg.snap->ping / 1000.0f) * ogc_pingpredict.value, possible->vel, possible->predictedOrigin);
	possible->visible = OGC_EntityIsVisible(possible);

	if (OGC_IsDead(possible))
		return 0;
	if (!OGC_CheckFov(possible->predictedOrigin))
		return 0;
	if (OGC_IsOnSameTeam(possible))
		return 0;

	if (current) {
		currentVisible = current->visible;
	}

	if (!ogc_ignorewalls.integer) {
		/* Never switch to targets hidden behind walls. */
		if (!possible->visible) {
			return 0;
		}

		/* We have no target; switch to the new target now. */
		if (!current) {
			return 1;
		}

		/* Switch to the new target, if he is closer. */
		if (OGC_AngleToPoint(possible->predictedOrigin) < OGC_AngleToPoint(current->predictedOrigin)) {
			return 1;
		}
	} else {
		/* We have no target; switch to the new target if he is visible. */
		if (!current) {
			if (possible->visible) {
				return 1;
			}
		}

		/* Switch to the new target, if he is closer, and has the same
		 * visibility level. */
		if (possible->visible == currentVisible) {
			if (OGC_AngleToPoint(possible->predictedOrigin) < OGC_AngleToPoint(current->predictedOrigin)) {
				return 1;
			}
		}
	}

	return 0;
}

int DrawEsp_BQ3(centity_t * ent, float *color, int *force)
{
	int team;

	if (ent->currentState.eFlags & EF_DEAD)
		return 0;
	switch (cgs.gametype) {
	case GT_TEAM:
	case GT_CTF:
		*force = 1;
		team = cgs.clientinfo[ent->currentState.clientNum].team;
		if (team == TEAM_RED) {
			color[0] = 1.0f;
			color[1] = 0.0f;
			color[2] = 0.0f;
		} else {
			color[0] = 0.0f;
			color[1] = 0.0f;
			color[2] = 1.0f;
		}
		break;
	default:
		*force = 0;
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
	}
	color[3] = 1.0f;
	return 1;
}
