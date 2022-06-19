/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/string.h"

#include "hll.h"
#include "reign.h"

#define RE_MAX_PLUGINS 1

struct RE_options RE_options;

static struct RE_plugin *plugins[RE_MAX_PLUGINS];

static struct RE_plugin *get_plugin(unsigned plugin)
{
	return plugin < RE_MAX_PLUGINS ? plugins[plugin] : NULL;
}

static struct RE_instance *get_instance(unsigned plugin, unsigned instance)
{
	struct RE_plugin *rp = get_plugin(plugin);
	if (!rp)
		return NULL;
	return instance < RE_MAX_INSTANCES ? rp->instances[instance] : NULL;
}

static struct motion *get_motion(unsigned plugin, unsigned instance)
{
	struct RE_instance *inst = get_instance(plugin, instance);
	return inst ? inst->motion : NULL;
}

static struct motion *get_next_motion(unsigned plugin, unsigned instance)
{
	struct RE_instance *inst = get_instance(plugin, instance);
	return inst ? inst->next_motion : NULL;
}

static struct RE_back_cg *get_back_cg(unsigned plugin, unsigned num)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p || num >= RE_NR_BACK_CGS)
		return NULL;
	return &p->back_cg[num];
}

static int ReignEngine_CreatePlugin(void)
{
	for (int i = 0; i < RE_MAX_PLUGINS; i++) {
		if (!plugins[i]) {
			plugins[i] = RE_plugin_new();
			return plugins[i] ? i : -1;
		}
	}
	WARNING("too many plugins");
	return -1;
}

static bool ReignEngine_ReleasePlugin(int handle)
{
	if ((unsigned)handle >= RE_MAX_PLUGINS || !plugins[handle])
		return false;
	RE_plugin_free(plugins[handle]);
	plugins[handle] = NULL;
	return true;
}

static bool ReignEngine_BindPlugin(int handle, int sprite)
{
	return RE_plugin_bind(get_plugin(handle), sprite);
}

static bool ReignEngine_UnbindPlugin(int handle)
{
	return RE_plugin_unbind(get_plugin(handle));
}

HLL_QUIET_UNIMPLEMENTED(true, bool, ReignEngine, BuildModel, int plugin, int pass_time);

static int ReignEngine_CreateInstance(int plugin)
{
	return RE_create_instance(get_plugin(plugin));
}

static bool ReignEngine_ReleaseInstance(int plugin, int instance)
{
	return RE_release_instance(get_plugin(plugin), instance);
}

static bool ReignEngine_LoadInstance(int plugin, int instance, struct string *name)
{
	return RE_instance_load(get_instance(plugin, instance), name->text);
}

static bool ReignEngine_SetInstanceType(int plugin, int instance, int type)
{
	return RE_instance_set_type(get_instance(plugin, instance), type);
}

static bool ReignEngine_SetInstancePos(int plugin, int instance, float x, float y, float z)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->pos[0] = x;
	ri->pos[1] = y;
	ri->pos[2] = -z;
	ri->local_transform_needs_update = true;
	return true;
}

static bool ReignEngine_SetInstanceVector(int plugin, int instance, float x, float y, float z)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->vec[0] = x;
	ri->vec[1] = y;
	ri->vec[2] = -z;
	return true;
}

static bool ReignEngine_SetInstanceAngle(int plugin, int instance, float angle)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->yaw = -angle;
	ri->local_transform_needs_update = true;
	return true;
}

static bool ReignEngine_SetInstanceAngleP(int plugin, int instance, float angle_p)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->pitch = angle_p;
	ri->local_transform_needs_update = true;
	return true;
}

static bool ReignEngine_SetInstanceAngleB(int plugin, int instance, float angle_b)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->roll = angle_b;
	ri->local_transform_needs_update = true;
	return true;
}

static float ReignEngine_GetInstanceScaleX(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	return ri ? ri->scale[0] : 0.0;
}

static float ReignEngine_GetInstanceScaleY(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	return ri ? ri->scale[1] : 0.0;
}

static float ReignEngine_GetInstanceScaleZ(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	return ri ? ri->scale[2] : 0.0;
}

static bool ReignEngine_SetInstanceScaleX(int plugin, int instance, float scale_x)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->scale[0] = scale_x;
	ri->local_transform_needs_update = true;
	return true;
}

static bool ReignEngine_SetInstanceScaleY(int plugin, int instance, float scale_y)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->scale[1] = scale_y;
	ri->local_transform_needs_update = true;
	return true;
}

static bool ReignEngine_SetInstanceScaleZ(int plugin, int instance, float scale_z)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->scale[0] = scale_z;
	ri->local_transform_needs_update = true;
	return true;
}

//bool ReignEngine_SetInstanceZBias(int plugin, int instance, float fZBias);
//float ReignEngine_GetInstanceZBias(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceVertexPos, int plugin, int instance, int index, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDiffuse, int plugin, int instance, float r, float g, float b);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceGlobeDiffuse, int plugin, int instance, float r, float g, float b);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceAmbient, int plugin, int instance, float r, float g, float b);
//bool ReignEngine_SetInstanceSpecular(int plugin, int instance, float fSpecular);

static bool ReignEngine_SetInstanceAlpha(int plugin, int instance, float alpha)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->alpha = alpha;
	return true;
}

//bool ReignEngine_SetInstanceAttenuationNear(int plugin, int instance, float fNear);
//bool ReignEngine_SetInstanceAttenuationFar(int plugin, int instance, float fFar);

static bool ReignEngine_SetInstanceDraw(int plugin, int instance, bool flag)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->draw = flag;
	return true;
}

HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDrawShadow, int plugin, int instance, bool flag);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDrawBackShadow, int plugin, int instance, bool flag);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDrawMakeShadow, int plugin, int instance, bool flag);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDrawBump, int plugin, int instance, bool flag);

static bool ReignEngine_GetInstanceDraw(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	return ri->draw;
}

//bool ReignEngine_GetInstanceDrawShadow(int plugin, int instance);
//bool ReignEngine_GetInstanceDrawBackShadow(int plugin, int instance);
//bool ReignEngine_GetInstanceDrawMakeShadow(int plugin, int instance);
//bool ReignEngine_GetInstanceDrawBump(int plugin, int instance);
//int ReignEngine_GetInstanceDrawType(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceDrawType, int plugin, int instance, int draw_type);

bool ReignEngine_LoadInstanceMotion(int plugin, int instance, struct string *name)
{
	return RE_instance_load_motion(get_instance(plugin, instance), name->text);
}

static int ReignEngine_GetInstanceMotionState(int plugin, int instance)
{
	return RE_motion_get_state(get_motion(plugin, instance));
}

static float ReignEngine_GetInstanceMotionFrame(int plugin, int instance)
{
	return RE_motion_get_frame(get_motion(plugin, instance));
}

static bool ReignEngine_SetInstanceMotionState(int plugin, int instance, int state)
{
	return RE_motion_set_state(get_motion(plugin, instance), state);
}

static bool ReignEngine_SetInstanceMotionFrame(int plugin, int instance, float frame)
{
	return RE_motion_set_frame(get_motion(plugin, instance), frame);
}

static bool ReignEngine_SetInstanceMotionFrameRange(int plugin, int instance, float begin_frame, float end_frame)
{
	return RE_motion_set_frame_range(get_motion(plugin, instance), begin_frame, end_frame);
}

static bool ReignEngine_SetInstanceMotionLoopFrameRange(int plugin, int instance, float begin_frame, float end_frame)
{
	return RE_motion_set_loop_frame_range(get_motion(plugin, instance), begin_frame, end_frame);
}

static bool ReignEngine_LoadInstanceNextMotion(int plugin, int instance, struct string *name)
{
	return RE_instance_load_next_motion(get_instance(plugin, instance), name->text);
}

static bool ReignEngine_SetInstanceNextMotionState(int plugin, int instance, int state)
{
	return RE_motion_set_state(get_next_motion(plugin, instance), state);
}

static bool ReignEngine_SetInstanceNextMotionFrame(int plugin, int instance, float frame)
{
	return RE_motion_set_frame(get_next_motion(plugin, instance), frame);
}

static bool ReignEngine_SetInstanceNextMotionFrameRange(int plugin, int instance, float begin_frame, float end_frame)
{
	return RE_motion_set_frame_range(get_next_motion(plugin, instance), begin_frame, end_frame);
}

static bool ReignEngine_SetInstanceNextMotionLoopFrameRange(int plugin, int instance, float begin_frame, float end_frame)
{
	return RE_motion_set_loop_frame_range(get_next_motion(plugin, instance), begin_frame, end_frame);
}

static bool ReignEngine_SetInstanceMotionBlendRate(int plugin, int instance, float rate)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->motion_blend_rate = rate;
	return true;
}

static bool ReignEngine_SetInstanceMotionBlend(int plugin, int instance, bool motion_blend)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->motion_blend = motion_blend;
	return true;
}

static bool ReignEngine_IsInstanceMotionBlend(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	return ri ? ri->motion_blend : false;
}

//bool ReignEngine_GetInstanceMotionBlendPutTime(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceMotionBlendPutTime, int plugin, int instance, bool flag);

static bool ReignEngine_SwapInstanceMotion(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	struct motion *tmp = ri->motion;
	ri->motion = ri->next_motion;
	ri->next_motion = tmp;
	return true;
}

static bool ReignEngine_FreeInstanceNextMotion(int plugin, int instance)
{
	return RE_instance_free_next_motion(get_instance(plugin, instance));
}

//int ReignEngine_GetInstanceNumofMaterial(int plugin, int instance);
//bool ReignEngine_GetInstanceMaterialName(int plugin, int instance, int num, struct string **pIName);
//float ReignEngine_GetInstanceMaterialParam(int plugin, int instance, int nMaterial, int type);
//bool ReignEngine_SetInstanceMaterialParam(int plugin, int instance, int nMaterial, int type, float param);
//bool ReignEngine_SaveInstanceAddMaterialData(int plugin, int instance);
//bool ReignEngine_SetInstancePointPos(int plugin, int instance, int index, float x, float y, float z);
//bool ReignEngine_GetInstancePointPos(int plugin, int instance, int index, float *pfX, float *pfY, float *pfZ);
//bool ReignEngine_GetInstanceColumnPos(int plugin, int instance, float *fX, float *fY, float *fZ);
//float ReignEngine_GetInstanceColumnHeight(int plugin, int instance);
//float ReignEngine_GetInstanceColumnRadius(int plugin, int instance);
//float ReignEngine_GetInstanceColumnAngle(int plugin, int instance);
//float ReignEngine_GetInstanceColumnAngleP(int plugin, int instance);
//float ReignEngine_GetInstanceColumnAngleB(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceColumnPos, int plugin, int instance, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceColumnHeight, int plugin, int instance, float height);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceColumnRadius, int plugin, int instance, float radius);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceColumnAngle, int plugin, int instance, float angle);
//bool ReignEngine_SetInstanceColumnAngleP(int plugin, int instance, float fAngleP);
//bool ReignEngine_SetInstanceColumnAngleB(int plugin, int instance, float fAngleB);
//bool ReignEngine_GetInstanceDrawColumn(int plugin, int instance);
//bool ReignEngine_SetInstanceDrawColumn(int plugin, int instance, bool bDraw);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceTarget, int plugin, int instance, int index, int target);
//int ReignEngine_GetInstanceTarget(int plugin, int instance, int index);

static float ReignEngine_GetInstanceFPS(int plugin, int instance)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	return ri ? ri->fps : 0.0;
}

static bool ReignEngine_SetInstanceFPS(int plugin, int instance, float fps)
{
	struct RE_instance *ri = get_instance(plugin, instance);
	if (!ri)
		return false;
	ri->fps = fps;
	return true;
}

HLL_WARN_UNIMPLEMENTED(0, int, ReignEngine, GetInstanceBoneIndex, int plugin, int instance, struct string *name);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, TransInstanceLocalPosToWorldPosByBone, int plugin, int instance, int bone, float offset_x, float offset_y, float offset_z, float *x, float *y, float *z);
//int ReignEngine_GetInstanceNumofPolygon(int plugin, int instance);
//int ReignEngine_GetInstanceTextureMemorySize(int plugin, int instance);
//void ReignEngine_GetInstanceInfoText(int plugin, int instance, struct string **pIText);
//void ReignEngine_GetInstanceMaterialInfoText(int plugin, int instance, struct string **pIText);
HLL_WARN_UNIMPLEMENTED(0.0, float, ReignEngine, CalcInstanceHeightDetection, int plugin, int instance, float x, float z);
//float ReignEngine_GetInstanceSoftFogEdgeLength(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceSoftFogEdgeLength, int plugin, int instance, float length);
//float ReignEngine_GetInstanceShadowVolumeBoneRadius(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceShadowVolumeBoneRadius, int plugin, int instance, float radius);
//bool ReignEngine_GetInstanceDebugDrawShadowVolume(int plugin, int instance);
//bool ReignEngine_SetInstanceDebugDrawShadowVolume(int plugin, int instance, bool flag);
//bool ReignEngine_SaveEffect(int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, GetEffectFrameRange, int plugin, int instance, int *begin_frame, int *end_frame);
//int ReignEngine_GetEffectObjectType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectMoveType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectUpVecType(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectMoveCurve(int plugin, int instance, int nObject);
//void ReignEngine_GetEffectObjectFrame(int plugin, int instance, int nObject, int *pnBeginFrame, int *pnEndFrame);
//int ReignEngine_GetEffectObjectStopFrame(int plugin, int instance, int nObject);
HLL_WARN_UNIMPLEMENTED(0, int, ReignEngine, GetEffectNumofObject, int plugin, int instance);
//void ReignEngine_GetEffectObjectName(int plugin, int instance, int nObject, struct string **pIName);
//int ReignEngine_GetEffectNumofObjectPos(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectPosUnit(int plugin, int instance, int nObject, int nPos);
//int ReignEngine_GetEffectObjectPosUnitType(int plugin, int instance, int nObject, int nPos, int nPosUnit);
//int ReignEngine_GetEffectObjectPosUnitIndex(int plugin, int instance, int nObject, int nPos, int nPosUnit);
//int ReignEngine_GetEffectNumofObjectPosUnitParam(int plugin, int instance, int nObject, int nPos, int nPosUnit);
//float ReignEngine_GetEffectObjectPosUnitParam(int plugin, int instance, int nObject, int nPos, int nPosUnit, int nParam);
//int ReignEngine_GetEffectNumofObjectPosUnitString(int plugin, int instance, int nObject, int nPos, int nPosUnit);
//void ReignEngine_GetEffectObjectPosUnitString(int plugin, int instance, int nObject, int nPos, int nPosUnit, int nParam, struct string **pIResult);
//int ReignEngine_GetEffectNumofObjectTexture(int plugin, int instance, int nObject);
//void ReignEngine_GetEffectObjectTexture(int plugin, int instance, int nObject, int nTexture, struct string **pIName);
//bool ReignEngine_GetEffectObjectSize(int plugin, int instance, int nObject, float *pfBeginSize, float *pfEndSize);
//bool ReignEngine_GetEffectObjectSize2(int plugin, int instance, int nObject, int nFrame, float *pfSize);
//bool ReignEngine_GetEffectObjectSizeX(int plugin, int instance, int nObject, int nFrame, float *pfSize);
//bool ReignEngine_GetEffectObjectSizeY(int plugin, int instance, int nObject, int nFrame, float *pfSize);
//bool ReignEngine_GetEffectObjectSizeType(int plugin, int instance, int nObject, int nFrame, int *nType);
//bool ReignEngine_GetEffectObjectSizeXType(int plugin, int instance, int nObject, int nFrame, int *nType);
//bool ReignEngine_GetEffectObjectSizeYType(int plugin, int instance, int nObject, int nFrame, int *nType);
//int ReignEngine_GetEffectNumofObjectSize2(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectSizeX(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectSizeY(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectSizeType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectSizeXType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectNumofObjectSizeYType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectBlendType(int plugin, int instance, int nObject);
//void ReignEngine_GetEffectObjectPolygonName(int plugin, int instance, int nObject, struct string **pIResult);
//int ReignEngine_GetEffectNumofObjectParticle(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectAlphaFadeInTime(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectAlphaFadeOutTime(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectTextureAnimeTime(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectAlphaFadeInFrame(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectAlphaFadeOutFrame(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectTextureAnimeFrame(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectFrameReferenceType(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectFrameReferenceParam(int plugin, int instance, int nObject);
//void ReignEngine_GetEffectObjectXRotationAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectYRotationAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectZRotationAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectXRevolutionAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectXRevolutionDistance(int plugin, int instance, int nObject, float *pfBeginDistance, float *pfEndDistance);
//void ReignEngine_GetEffectObjectYRevolutionAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectYRevolutionDistance(int plugin, int instance, int nObject, float *pfBeginDistance, float *pfEndDistance);
//void ReignEngine_GetEffectObjectZRevolutionAngle(int plugin, int instance, int nObject, float *pfBeginAngle, float *pfEndAngle);
//void ReignEngine_GetEffectObjectZRevolutionDistance(int plugin, int instance, int nObject, float *pfBeginDistance, float *pfEndDistance);
//bool ReignEngine_AddEffectObject(int plugin, int instance);
//bool ReignEngine_DeleteEffectObject(int plugin, int instance, int nObject);
//bool ReignEngine_SetEffectObjectName(int plugin, int instance, int nObject, struct string *pIName);
//bool ReignEngine_SetEffectObjectType(int plugin, int instance, int nObject, int type);
//bool ReignEngine_SetEffectObjectMoveType(int plugin, int instance, int nObject, int type);
//bool ReignEngine_SetEffectObjectUpVecType(int plugin, int instance, int nObject, int type);
//bool ReignEngine_SetEffectObjectMoveCurve(int plugin, int instance, int nObject, float fCurve);
//bool ReignEngine_SetEffectObjectFrame(int plugin, int instance, int nObject, int nBeginFrame, int nEndFrame);
//bool ReignEngine_SetEffectObjectStopFrame(int plugin, int instance, int nObject, int nStopFrame);
//bool ReignEngine_SetEffectObjectPosUnitType(int plugin, int instance, int nObject, int nPos, int nPosUnit, int type);
//bool ReignEngine_SetEffectObjectPosUnitIndex(int plugin, int instance, int nObject, int nPos, int nPosUnit, int index);
//bool ReignEngine_SetEffectObjectPosUnitParam(int plugin, int instance, int nObject, int nPos, int nPosUnit, int nParam, float fData);
//bool ReignEngine_SetEffectObjectPosUnitString(int plugin, int instance, int nObject, int nPos, int nPosUnit, int nParam, struct string *String);
//bool ReignEngine_SetEffectObjectTexture(int plugin, int instance, int nObject, int nTexture, struct string *pIName);
//bool ReignEngine_SetEffectObjectSize(int plugin, int instance, int nObject, float fBeginSize, float fEndSize);
//bool ReignEngine_SetEffectObjectSize2(int plugin, int instance, int nObject, int nFrame, float fSize);
//bool ReignEngine_SetEffectObjectSizeX(int plugin, int instance, int nObject, int nFrame, float fSize);
//bool ReignEngine_SetEffectObjectSizeY(int plugin, int instance, int nObject, int nFrame, float fSize);
//bool ReignEngine_SetEffectObjectSizeType(int plugin, int instance, int nObject, int nFrame, int type);
//bool ReignEngine_SetEffectObjectSizeXType(int plugin, int instance, int nObject, int nFrame, int type);
//bool ReignEngine_SetEffectObjectSizeYType(int plugin, int instance, int nObject, int nFrame, int type);
//bool ReignEngine_SetEffectObjectBlendType(int plugin, int instance, int nObject, int type);
//bool ReignEngine_SetEffectObjectPolygonName(int plugin, int instance, int nObject, struct string *pIName);
//bool ReignEngine_SetEffectNumofObjectParticle(int plugin, int instance, int nObject, int numof);
//bool ReignEngine_SetEffectObjectAlphaFadeInTime(int plugin, int instance, int nObject, int nTime);
//bool ReignEngine_SetEffectObjectAlphaFadeOutTime(int plugin, int instance, int nObject, int nTime);
//bool ReignEngine_SetEffectObjectTextureAnimeTime(int plugin, int instance, int nObject, int nTime);
//bool ReignEngine_SetEffectObjectAlphaFadeInFrame(int plugin, int instance, int nObject, float fFrame);
//bool ReignEngine_SetEffectObjectAlphaFadeOutFrame(int plugin, int instance, int nObject, float fFrame);
//bool ReignEngine_SetEffectObjectTextureAnimeFrame(int plugin, int instance, int nObject, float fFrame);
//bool ReignEngine_SetEffectObjectFrameReferenceType(int plugin, int instance, int nObject, int type);
//bool ReignEngine_SetEffectObjectFrameReferenceParam(int plugin, int instance, int nObject, int nParam);
//bool ReignEngine_SetEffectObjectXRotationAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectYRotationAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectZRotationAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectXRevolutionAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectXRevolutionDistance(int plugin, int instance, int nObject, float fBeginDistance, float fEndDistance);
//bool ReignEngine_SetEffectObjectYRevolutionAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectYRevolutionDistance(int plugin, int instance, int nObject, float fBeginDistance, float fEndDistance);
//bool ReignEngine_SetEffectObjectZRevolutionAngle(int plugin, int instance, int nObject, float fBeginAngle, float fEndAngle);
//bool ReignEngine_SetEffectObjectZRevolutionDistance(int plugin, int instance, int nObject, float fBeginDistance, float fEndDistance);
//bool ReignEngine_GetEffectObjectCurveLength(int plugin, int instance, int nObject, float *pfX, float *pfY, float *pfZ);
//bool ReignEngine_SetEffectObjectCurveLength(int plugin, int instance, int nObject, float x, float y, float z);
//int ReignEngine_GetEffectObjectChildFrame(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectChildLength(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectChildBeginSlope(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectChildEndSlope(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectChildCreateBeginFrame(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectChildCreateEndFrame(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectChildMoveDirType(int plugin, int instance, int nObject);
//bool ReignEngine_SetEffectObjectChildFrame(int plugin, int instance, int nObject, int nFrame);
//bool ReignEngine_SetEffectObjectChildLength(int plugin, int instance, int nObject, float fLength);
//bool ReignEngine_SetEffectObjectChildBeginSlope(int plugin, int instance, int nObject, float fSlope);
//bool ReignEngine_SetEffectObjectChildEndSlope(int plugin, int instance, int nObject, float fSlope);
//bool ReignEngine_SetEffectObjectChildCreateBeginFrame(int plugin, int instance, int nObject, float fFrame);
//bool ReignEngine_SetEffectObjectChildCreateEndFrame(int plugin, int instance, int nObject, float fFrame);
//bool ReignEngine_SetEffectObjectChildMoveDirType(int plugin, int instance, int nObject, int type);
//int ReignEngine_GetEffectObjectDirType(int plugin, int instance, int nObject);
//bool ReignEngine_SetEffectObjectDirType(int plugin, int instance, int nObject, int type);
//int ReignEngine_GetEffectNumofObjectDamage(int plugin, int instance, int nObject);
//int ReignEngine_GetEffectObjectDamage(int plugin, int instance, int nObject, int nFrame);
//bool ReignEngine_SetEffectObjectDamage(int plugin, int instance, int nObject, int nFrame, int nData);
//bool ReignEngine_GetEffectObjectDraw(int plugin, int instance, int nObject);
//bool ReignEngine_SetEffectObjectDraw(int plugin, int instance, int nObject, bool bDraw);
//float ReignEngine_GetEffectObjectOffsetX(int plugin, int instance, int nObject);
//float ReignEngine_GetEffectObjectOffsetY(int plugin, int instance, int nObject);
//bool ReignEngine_SetEffectObjectOffsetX(int plugin, int instance, int nObject, float fOffsetX);
//bool ReignEngine_SetEffectObjectOffsetY(int plugin, int instance, int nObject, float fOffsetY);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, GetCameraQuakeEffectFlag, int plugin, int instance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetCameraQuakeEffectFlag, int plugin, int instance, bool flag);

bool ReignEngine_SetCameraPos(int plugin, float x, float y, float z)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->camera.pos[0] = x;
	p->camera.pos[1] = y;
	p->camera.pos[2] = -z;
	return true;
}

bool ReignEngine_SetCameraAngle(int plugin, float angle)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->camera.yaw = -angle;
	return true;
}

bool ReignEngine_SetCameraAngleP(int plugin, float angle_p)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->camera.pitch = angle_p;
	return true;
}

bool ReignEngine_SetCameraAngleB(int plugin, float angle_b)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->camera.roll = angle_b;
	return true;
}

//float ReignEngine_GetShadowBias(int plugin, int num);
//float ReignEngine_GetShadowTargetDistance(int plugin, int num);

static int ReignEngine_GetShadowMapResolutionLevel(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->shadow_map_resolution_level : 0;
}

//float ReignEngine_GetShadowSplitDepth(int plugin, int num);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetShadowMapType, int plugin, int type);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetShadowMapLightLookPos, int plugin, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetShadowMapLightDir, int plugin, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetShadowBox, int plugin, float x, float y, float z, float size_x, float size_y, float size_z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetShadowBias, int plugin, int num, float shadow_bias);
//bool ReignEngine_SetShadowSlopeBias(int plugin, float fShadowSlopeBias);
//bool ReignEngine_SetShadowFilterMag(int plugin, float fShadowFilterMag);
//bool ReignEngine_SetShadowTargetDistance(int plugin, int num, float fDistance);

static bool ReignEngine_SetShadowMapResolutionLevel(int plugin, int level)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->shadow_map_resolution_level = level;
	return true;
}

//bool ReignEngine_SetShadowSplitDepth(int plugin, int num, float fDepth);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetFogType, int plugin, int type);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetFogNear, int plugin, float near);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetFogFar, int plugin, float far);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetFogColor, int plugin, float r, float g, float b);
//int ReignEngine_GetFogType(int plugin);
//float ReignEngine_GetFogNear(int plugin);
//float ReignEngine_GetFogFar(int plugin);
//void ReignEngine_GetFogColor(int plugin, float *r, float *g, float *b);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, LoadLightScatteringSetting, int plugin, struct string *filename /* e.g. "Data\PolyData\BG\bg001\bg001.ls2" */);
//float ReignEngine_GetLSBetaR(int plugin);
//float ReignEngine_GetLSBetaM(int plugin);
//float ReignEngine_GetLSG(int plugin);
//float ReignEngine_GetLSDistance(int plugin);
//void ReignEngine_GetLSLightDir(int plugin, float *pfX, float *pfY, float *pfZ);
//void ReignEngine_GetLSLightColor(int plugin, float *r, float *g, float *b);
//void ReignEngine_GetLSSunColor(int plugin, float *r, float *g, float *b);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSBetaR, int plugin, float beta_r);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSBetaM, int plugin, float beta_m);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSG, int plugin, float g);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSDistance, int plugin, float distance);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSLightDir, int plugin, float x, float y, float z);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSLightColor, int plugin, float r, float g, float b);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetLSSunColor, int plugin, float r, float g, float b);
//bool ReignEngine_SetDrawTextureFog(int plugin, bool bDraw);
//bool ReignEngine_GetDrawTextureFog(int plugin);

static bool ReignEngine_SetOptionAntiAliasing(int flag)
{
	RE_options.anti_aliasing = flag;
	return true;
}

static bool ReignEngine_SetOptionWaitVSync(int flag)
{
	RE_options.wait_vsync = flag;
	return true;
}

static int ReignEngine_GetOptionAntiAliasing(void)
{
	return RE_options.anti_aliasing;
}

static int ReignEngine_GetOptionWaitVSync(void)
{
	return RE_options.wait_vsync;
}

//bool ReignEngine_SetViewport(int plugin, int nX, int nY, int nWidth, int nHeight);

bool ReignEngine_SetProjection(int plugin, float width, float height, float near, float far, float deg)
{
	return RE_set_projection(get_plugin(plugin), width, height, near, far, deg);
}

//float ReignEngine_GetBrightness(int plugin);
//bool ReignEngine_SetBrightness(int plugin, float fBrightness);

static bool ReignEngine_SetRenderMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->render_mode = mode;
	return true;
}

static bool ReignEngine_SetShadowMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->shadow_mode = mode;
	return true;
}

static bool ReignEngine_SetBumpMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->bump_mode = mode;
	return true;
}

//bool ReignEngine_SetHDRMode(int plugin, int nMode);

static bool ReignEngine_SetVertexBlendMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->vertex_blend_mode = mode;
	return true;
}

static bool ReignEngine_SetFogMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->fog_mode = mode;
	return true;
}

static bool ReignEngine_SetSpecularMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->specular_mode = mode;
	return true;
}

static bool ReignEngine_SetLightMapMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->light_map_mode = mode;
	return true;
}

static bool ReignEngine_SetSoftFogEdgeMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->soft_fog_edge_mode = mode;
	return true;
}

static bool ReignEngine_SetSSAOMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->ssao_mode = mode;
	return true;
}

static bool ReignEngine_SetShaderPrecisionMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->shader_precision_mode = mode;
	return true;
}

//bool ReignEngine_SetShaderDebugMode(int plugin, int nMode);

static int ReignEngine_GetRenderMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->render_mode : 0;
}

static int ReignEngine_GetShadowMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->shadow_mode : 0;
}

static int ReignEngine_GetBumpMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->bump_mode : 0;
}

//int ReignEngine_GetHDRMode(int plugin);

static int ReignEngine_GetVertexBlendMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->vertex_blend_mode : 0;
}

static int ReignEngine_GetFogMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->fog_mode : 0;
}

static int ReignEngine_GetSpecularMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->specular_mode : 0;
}

static int ReignEngine_GetLightMapMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->light_map_mode : 0;
}

static int ReignEngine_GetSoftFogEdgeMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->soft_fog_edge_mode : 0;
}

static int ReignEngine_GetSSAOMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->ssao_mode : 0;
}

static int ReignEngine_GetShaderPrecisionMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->shader_precision_mode : 0;
}

//int ReignEngine_GetShaderDebugMode(int plugin);

static int ReignEngine_GetTextureResolutionLevel(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->texture_resolution_level : 0;
}

static int ReignEngine_GetTextureFilterMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->texture_filter_mode : 0;
}

static bool ReignEngine_SetTextureResolutionLevel(int plugin, int level)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->texture_resolution_level = level;
	return true;
}

static bool ReignEngine_SetTextureFilterMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->texture_filter_mode = mode;
	return true;
}

static bool ReignEngine_GetUsePower2Texture(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->use_power2_texture : false;
}

static bool ReignEngine_SetUsePower2Texture(int plugin, bool use)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->use_power2_texture = use;
	return true;
}

static bool ReignEngine_GetGlobalAmbient(int plugin, float *r, float *g, float *b)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	*r = p->global_ambient_r;
	*g = p->global_ambient_g;
	*b = p->global_ambient_b;
	return true;
}

static bool ReignEngine_SetGlobalAmbient(int plugin, float r, float g, float b)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->global_ambient_r = r;
	p->global_ambient_g = g;
	p->global_ambient_b = b;
	return true;
}

static int ReignEngine_GetBloomMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->bloom_mode : 0;
}

static bool ReignEngine_SetBloomMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->bloom_mode = mode;
	return true;
}

static int ReignEngine_GetGlareMode(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->glare_mode : 0;
}

static bool ReignEngine_SetGlareMode(int plugin, int mode)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->glare_mode = mode;
	return true;
}

static float ReignEngine_GetPostEffectFilterY(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->post_effect_filter_y : 0.0;
}

static float ReignEngine_GetPostEffectFilterCb(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->post_effect_filter_cb : 0.0;
}

static float ReignEngine_GetPostEffectFilterCr(int plugin)
{
	struct RE_plugin *p = get_plugin(plugin);
	return p ? p->post_effect_filter_cr : 0.0;
}

static bool ReignEngine_SetPostEffectFilterY(int plugin, float y)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->post_effect_filter_y = y;
	return true;
}

static bool ReignEngine_SetPostEffectFilterCb(int plugin, float cb)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->post_effect_filter_cb = cb;
	return true;
}

static bool ReignEngine_SetPostEffectFilterCr(int plugin, float cr)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	p->post_effect_filter_cr = cr;
	return true;
}

static bool ReignEngine_GetBackCGName(int plugin, int num, struct string **cg_name)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	if (!bcg)
		return false;
	if (*cg_name)
		free_string(*cg_name);
	*cg_name = string_ref(bcg->name ? bcg->name : &EMPTY_STRING);
	return true;
}

static int ReignEngine_GetBackCGNum(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->no : 0;
}

static float ReignEngine_GetBackCGBlendRate(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->blend_rate : 0.0;
}

static float ReignEngine_GetBackCGDestPosX(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->x : 0.0;
}

static float ReignEngine_GetBackCGDestPosY(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->y : 0.0;
}

static float ReignEngine_GetBackCGMag(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->mag : 0.0;
}

//float ReignEngine_GetBackCGQuakeMag(int plugin, int num);

static bool ReignEngine_GetBackCGShow(int plugin, int num)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	return bcg ? bcg->show : false;
}

static bool ReignEngine_SetBackCGName(int plugin, int num, struct string *cg_name)
{
	struct RE_plugin *p = get_plugin(plugin);
	if (!p)
		return false;
	return RE_back_cg_set_name(get_back_cg(plugin, num), cg_name, p->aar);
}

static bool ReignEngine_SetBackCGNum(int plugin, int num, int cg_num)
{
	return RE_back_cg_set(get_back_cg(plugin, num), cg_num);
}

static bool ReignEngine_SetBackCGBlendRate(int plugin, int num, float blend_rate)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	if (!bcg)
		return false;
	bcg->blend_rate = blend_rate;
	return true;
}

static bool ReignEngine_SetBackCGDestPos(int plugin, int num, float x, float y)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	if (!bcg)
		return false;
	bcg->x = x;
	bcg->y = y;
	return true;
}

static bool ReignEngine_SetBackCGMag(int plugin, int num, float mag)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	if (!bcg)
		return false;
	bcg->mag = mag;
	return true;
}

//bool ReignEngine_SetBackCGQuakeMag(int plugin, int num, float fQuakeMag);

static bool ReignEngine_SetBackCGShow(int plugin, int num, bool show)
{
	struct RE_back_cg* bcg = get_back_cg(plugin, num);
	if (!bcg)
		return false;
	bcg->show = show;
	return true;
}

//float ReignEngine_GetGlareBrightnessParam(int plugin, int index);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetGlareBrightnessParam, int plugin, int index, float param);
//float ReignEngine_GetSSAOParam(int plugin, int type);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetSSAOParam, int plugin, int type, float param);
//bool ReignEngine_CalcIntersectEyeVec(int plugin, int instance, int nMouseX, int nMouseY, float *pfX, float *pfY, float *pfZ);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, IsLoading, int plugin);
//int ReignEngine_GetDebugInfoMode(int plugin);
//bool ReignEngine_SetDebugInfoMode(int plugin, int nMode);
HLL_WARN_UNIMPLEMENTED(30, int, ReignEngine, GetVertexShaderVersion, void);
HLL_WARN_UNIMPLEMENTED(30, int, ReignEngine, GetPixelShaderVersion, void);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceSpecularReflectRate, int plugin, int instance, float rate);
HLL_WARN_UNIMPLEMENTED(false, bool, ReignEngine, SetInstanceFresnelReflectRate, int plugin, int instance, float rate);
//float ReignEngine_GetInstanceSpecularReflectRate(int plugin, int instance);
//float ReignEngine_GetInstanceFresnelReflectRate(int plugin, int instance);
//float ReignEngine_StringToFloat(struct string *pIText);
//bool ReignEngine_DrawToMainSurface(int plugin);

HLL_LIBRARY(ReignEngine,
	    HLL_EXPORT(CreatePlugin, ReignEngine_CreatePlugin),
	    HLL_EXPORT(ReleasePlugin, ReignEngine_ReleasePlugin),
	    HLL_EXPORT(BindPlugin, ReignEngine_BindPlugin),
	    HLL_EXPORT(UnbindPlugin, ReignEngine_UnbindPlugin),
	    HLL_EXPORT(BuildModel, ReignEngine_BuildModel),
	    HLL_EXPORT(CreateInstance, ReignEngine_CreateInstance),
	    HLL_EXPORT(ReleaseInstance, ReignEngine_ReleaseInstance),
	    HLL_EXPORT(LoadInstance, ReignEngine_LoadInstance),
	    HLL_EXPORT(SetInstanceType, ReignEngine_SetInstanceType),
	    HLL_EXPORT(SetInstancePos, ReignEngine_SetInstancePos),
	    HLL_EXPORT(SetInstanceVector, ReignEngine_SetInstanceVector),
	    HLL_EXPORT(SetInstanceAngle, ReignEngine_SetInstanceAngle),
	    HLL_EXPORT(SetInstanceAngleP, ReignEngine_SetInstanceAngleP),
	    HLL_EXPORT(SetInstanceAngleB, ReignEngine_SetInstanceAngleB),
	    HLL_EXPORT(GetInstanceScaleX, ReignEngine_GetInstanceScaleX),
	    HLL_EXPORT(GetInstanceScaleY, ReignEngine_GetInstanceScaleY),
	    HLL_EXPORT(GetInstanceScaleZ, ReignEngine_GetInstanceScaleZ),
	    HLL_EXPORT(SetInstanceScaleX, ReignEngine_SetInstanceScaleX),
	    HLL_EXPORT(SetInstanceScaleY, ReignEngine_SetInstanceScaleY),
	    HLL_EXPORT(SetInstanceScaleZ, ReignEngine_SetInstanceScaleZ),
	    HLL_TODO_EXPORT(SetInstanceZBias, ReignEngine_SetInstanceZBias),
	    HLL_TODO_EXPORT(GetInstanceZBias, ReignEngine_GetInstanceZBias),
	    HLL_EXPORT(SetInstanceVertexPos, ReignEngine_SetInstanceVertexPos),
	    HLL_EXPORT(SetInstanceDiffuse, ReignEngine_SetInstanceDiffuse),
	    HLL_EXPORT(SetInstanceGlobeDiffuse, ReignEngine_SetInstanceGlobeDiffuse),
	    HLL_EXPORT(SetInstanceAmbient, ReignEngine_SetInstanceAmbient),
	    HLL_TODO_EXPORT(SetInstanceSpecular, ReignEngine_SetInstanceSpecular),
	    HLL_EXPORT(SetInstanceAlpha, ReignEngine_SetInstanceAlpha),
	    HLL_TODO_EXPORT(SetInstanceAttenuationNear, ReignEngine_SetInstanceAttenuationNear),
	    HLL_TODO_EXPORT(SetInstanceAttenuationFar, ReignEngine_SetInstanceAttenuationFar),
	    HLL_EXPORT(SetInstanceDraw, ReignEngine_SetInstanceDraw),
	    HLL_EXPORT(SetInstanceDrawShadow, ReignEngine_SetInstanceDrawShadow),
	    HLL_EXPORT(SetInstanceDrawBackShadow, ReignEngine_SetInstanceDrawBackShadow),
	    HLL_EXPORT(SetInstanceDrawMakeShadow, ReignEngine_SetInstanceDrawMakeShadow),
	    HLL_EXPORT(SetInstanceDrawBump, ReignEngine_SetInstanceDrawBump),
	    HLL_EXPORT(GetInstanceDraw, ReignEngine_GetInstanceDraw),
	    HLL_TODO_EXPORT(GetInstanceDrawShadow, ReignEngine_GetInstanceDrawShadow),
	    HLL_TODO_EXPORT(GetInstanceDrawBackShadow, ReignEngine_GetInstanceDrawBackShadow),
	    HLL_TODO_EXPORT(GetInstanceDrawMakeShadow, ReignEngine_GetInstanceDrawMakeShadow),
	    HLL_TODO_EXPORT(GetInstanceDrawBump, ReignEngine_GetInstanceDrawBump),
	    HLL_TODO_EXPORT(GetInstanceDrawType, ReignEngine_GetInstanceDrawType),
	    HLL_EXPORT(SetInstanceDrawType, ReignEngine_SetInstanceDrawType),
	    HLL_EXPORT(LoadInstanceMotion, ReignEngine_LoadInstanceMotion),
	    HLL_EXPORT(GetInstanceMotionState, ReignEngine_GetInstanceMotionState),
	    HLL_EXPORT(GetInstanceMotionFrame, ReignEngine_GetInstanceMotionFrame),
	    HLL_EXPORT(SetInstanceMotionState, ReignEngine_SetInstanceMotionState),
	    HLL_EXPORT(SetInstanceMotionFrame, ReignEngine_SetInstanceMotionFrame),
	    HLL_EXPORT(SetInstanceMotionFrameRange, ReignEngine_SetInstanceMotionFrameRange),
	    HLL_EXPORT(SetInstanceMotionLoopFrameRange, ReignEngine_SetInstanceMotionLoopFrameRange),
	    HLL_EXPORT(LoadInstanceNextMotion, ReignEngine_LoadInstanceNextMotion),
	    HLL_EXPORT(SetInstanceNextMotionState, ReignEngine_SetInstanceNextMotionState),
	    HLL_EXPORT(SetInstanceNextMotionFrame, ReignEngine_SetInstanceNextMotionFrame),
	    HLL_EXPORT(SetInstanceNextMotionFrameRange, ReignEngine_SetInstanceNextMotionFrameRange),
	    HLL_EXPORT(SetInstanceNextMotionLoopFrameRange, ReignEngine_SetInstanceNextMotionLoopFrameRange),
	    HLL_EXPORT(SetInstanceMotionBlendRate, ReignEngine_SetInstanceMotionBlendRate),
	    HLL_EXPORT(SetInstanceMotionBlend, ReignEngine_SetInstanceMotionBlend),
	    HLL_EXPORT(IsInstanceMotionBlend, ReignEngine_IsInstanceMotionBlend),
	    HLL_TODO_EXPORT(GetInstanceMotionBlendPutTime, ReignEngine_GetInstanceMotionBlendPutTime),
	    HLL_EXPORT(SetInstanceMotionBlendPutTime, ReignEngine_SetInstanceMotionBlendPutTime),
	    HLL_EXPORT(SwapInstanceMotion, ReignEngine_SwapInstanceMotion),
	    HLL_EXPORT(FreeInstanceNextMotion, ReignEngine_FreeInstanceNextMotion),
	    HLL_TODO_EXPORT(GetInstanceNumofMaterial, ReignEngine_GetInstanceNumofMaterial),
	    HLL_TODO_EXPORT(GetInstanceMaterialName, ReignEngine_GetInstanceMaterialName),
	    HLL_TODO_EXPORT(GetInstanceMaterialParam, ReignEngine_GetInstanceMaterialParam),
	    HLL_TODO_EXPORT(SetInstanceMaterialParam, ReignEngine_SetInstanceMaterialParam),
	    HLL_TODO_EXPORT(SaveInstanceAddMaterialData, ReignEngine_SaveInstanceAddMaterialData),
	    HLL_TODO_EXPORT(SetInstancePointPos, ReignEngine_SetInstancePointPos),
	    HLL_TODO_EXPORT(GetInstancePointPos, ReignEngine_GetInstancePointPos),
	    HLL_TODO_EXPORT(GetInstanceColumnPos, ReignEngine_GetInstanceColumnPos),
	    HLL_TODO_EXPORT(GetInstanceColumnHeight, ReignEngine_GetInstanceColumnHeight),
	    HLL_TODO_EXPORT(GetInstanceColumnRadius, ReignEngine_GetInstanceColumnRadius),
	    HLL_TODO_EXPORT(GetInstanceColumnAngle, ReignEngine_GetInstanceColumnAngle),
	    HLL_TODO_EXPORT(GetInstanceColumnAngleP, ReignEngine_GetInstanceColumnAngleP),
	    HLL_TODO_EXPORT(GetInstanceColumnAngleB, ReignEngine_GetInstanceColumnAngleB),
	    HLL_EXPORT(SetInstanceColumnPos, ReignEngine_SetInstanceColumnPos),
	    HLL_EXPORT(SetInstanceColumnHeight, ReignEngine_SetInstanceColumnHeight),
	    HLL_EXPORT(SetInstanceColumnRadius, ReignEngine_SetInstanceColumnRadius),
	    HLL_EXPORT(SetInstanceColumnAngle, ReignEngine_SetInstanceColumnAngle),
	    HLL_TODO_EXPORT(SetInstanceColumnAngleP, ReignEngine_SetInstanceColumnAngleP),
	    HLL_TODO_EXPORT(SetInstanceColumnAngleB, ReignEngine_SetInstanceColumnAngleB),
	    HLL_TODO_EXPORT(GetInstanceDrawColumn, ReignEngine_GetInstanceDrawColumn),
	    HLL_TODO_EXPORT(SetInstanceDrawColumn, ReignEngine_SetInstanceDrawColumn),
	    HLL_EXPORT(SetInstanceTarget, ReignEngine_SetInstanceTarget),
	    HLL_TODO_EXPORT(GetInstanceTarget, ReignEngine_GetInstanceTarget),
	    HLL_EXPORT(GetInstanceFPS, ReignEngine_GetInstanceFPS),
	    HLL_EXPORT(SetInstanceFPS, ReignEngine_SetInstanceFPS),
	    HLL_EXPORT(GetInstanceBoneIndex, ReignEngine_GetInstanceBoneIndex),
	    HLL_EXPORT(TransInstanceLocalPosToWorldPosByBone, ReignEngine_TransInstanceLocalPosToWorldPosByBone),
	    HLL_TODO_EXPORT(GetInstanceNumofPolygon, ReignEngine_GetInstanceNumofPolygon),
	    HLL_TODO_EXPORT(GetInstanceTextureMemorySize, ReignEngine_GetInstanceTextureMemorySize),
	    HLL_TODO_EXPORT(GetInstanceInfoText, ReignEngine_GetInstanceInfoText),
	    HLL_TODO_EXPORT(GetInstanceMaterialInfoText, ReignEngine_GetInstanceMaterialInfoText),
	    HLL_EXPORT(CalcInstanceHeightDetection, ReignEngine_CalcInstanceHeightDetection),
	    HLL_TODO_EXPORT(GetInstanceSoftFogEdgeLength, ReignEngine_GetInstanceSoftFogEdgeLength),
	    HLL_EXPORT(SetInstanceSoftFogEdgeLength, ReignEngine_SetInstanceSoftFogEdgeLength),
	    HLL_TODO_EXPORT(GetInstanceShadowVolumeBoneRadius, ReignEngine_GetInstanceShadowVolumeBoneRadius),
	    HLL_EXPORT(SetInstanceShadowVolumeBoneRadius, ReignEngine_SetInstanceShadowVolumeBoneRadius),
	    HLL_TODO_EXPORT(GetInstanceDebugDrawShadowVolume, ReignEngine_GetInstanceDebugDrawShadowVolume),
	    HLL_TODO_EXPORT(SetInstanceDebugDrawShadowVolume, ReignEngine_SetInstanceDebugDrawShadowVolume),
	    HLL_TODO_EXPORT(SaveEffect, ReignEngine_SaveEffect),
	    HLL_EXPORT(GetEffectFrameRange, ReignEngine_GetEffectFrameRange),
	    HLL_TODO_EXPORT(GetEffectObjectType, ReignEngine_GetEffectObjectType),
	    HLL_TODO_EXPORT(GetEffectObjectMoveType, ReignEngine_GetEffectObjectMoveType),
	    HLL_TODO_EXPORT(GetEffectObjectUpVecType, ReignEngine_GetEffectObjectUpVecType),
	    HLL_TODO_EXPORT(GetEffectObjectMoveCurve, ReignEngine_GetEffectObjectMoveCurve),
	    HLL_TODO_EXPORT(GetEffectObjectFrame, ReignEngine_GetEffectObjectFrame),
	    HLL_TODO_EXPORT(GetEffectObjectStopFrame, ReignEngine_GetEffectObjectStopFrame),
	    HLL_EXPORT(GetEffectNumofObject, ReignEngine_GetEffectNumofObject),
	    HLL_TODO_EXPORT(GetEffectObjectName, ReignEngine_GetEffectObjectName),
	    HLL_TODO_EXPORT(GetEffectNumofObjectPos, ReignEngine_GetEffectNumofObjectPos),
	    HLL_TODO_EXPORT(GetEffectNumofObjectPosUnit, ReignEngine_GetEffectNumofObjectPosUnit),
	    HLL_TODO_EXPORT(GetEffectObjectPosUnitType, ReignEngine_GetEffectObjectPosUnitType),
	    HLL_TODO_EXPORT(GetEffectObjectPosUnitIndex, ReignEngine_GetEffectObjectPosUnitIndex),
	    HLL_TODO_EXPORT(GetEffectNumofObjectPosUnitParam, ReignEngine_GetEffectNumofObjectPosUnitParam),
	    HLL_TODO_EXPORT(GetEffectObjectPosUnitParam, ReignEngine_GetEffectObjectPosUnitParam),
	    HLL_TODO_EXPORT(GetEffectNumofObjectPosUnitString, ReignEngine_GetEffectNumofObjectPosUnitString),
	    HLL_TODO_EXPORT(GetEffectObjectPosUnitString, ReignEngine_GetEffectObjectPosUnitString),
	    HLL_TODO_EXPORT(GetEffectNumofObjectTexture, ReignEngine_GetEffectNumofObjectTexture),
	    HLL_TODO_EXPORT(GetEffectObjectTexture, ReignEngine_GetEffectObjectTexture),
	    HLL_TODO_EXPORT(GetEffectObjectSize, ReignEngine_GetEffectObjectSize),
	    HLL_TODO_EXPORT(GetEffectObjectSize2, ReignEngine_GetEffectObjectSize2),
	    HLL_TODO_EXPORT(GetEffectObjectSizeX, ReignEngine_GetEffectObjectSizeX),
	    HLL_TODO_EXPORT(GetEffectObjectSizeY, ReignEngine_GetEffectObjectSizeY),
	    HLL_TODO_EXPORT(GetEffectObjectSizeType, ReignEngine_GetEffectObjectSizeType),
	    HLL_TODO_EXPORT(GetEffectObjectSizeXType, ReignEngine_GetEffectObjectSizeXType),
	    HLL_TODO_EXPORT(GetEffectObjectSizeYType, ReignEngine_GetEffectObjectSizeYType),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSize2, ReignEngine_GetEffectNumofObjectSize2),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSizeX, ReignEngine_GetEffectNumofObjectSizeX),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSizeY, ReignEngine_GetEffectNumofObjectSizeY),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSizeType, ReignEngine_GetEffectNumofObjectSizeType),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSizeXType, ReignEngine_GetEffectNumofObjectSizeXType),
	    HLL_TODO_EXPORT(GetEffectNumofObjectSizeYType, ReignEngine_GetEffectNumofObjectSizeYType),
	    HLL_TODO_EXPORT(GetEffectObjectBlendType, ReignEngine_GetEffectObjectBlendType),
	    HLL_TODO_EXPORT(GetEffectObjectPolygonName, ReignEngine_GetEffectObjectPolygonName),
	    HLL_TODO_EXPORT(GetEffectNumofObjectParticle, ReignEngine_GetEffectNumofObjectParticle),
	    HLL_TODO_EXPORT(GetEffectObjectAlphaFadeInTime, ReignEngine_GetEffectObjectAlphaFadeInTime),
	    HLL_TODO_EXPORT(GetEffectObjectAlphaFadeOutTime, ReignEngine_GetEffectObjectAlphaFadeOutTime),
	    HLL_TODO_EXPORT(GetEffectObjectTextureAnimeTime, ReignEngine_GetEffectObjectTextureAnimeTime),
	    HLL_TODO_EXPORT(GetEffectObjectAlphaFadeInFrame, ReignEngine_GetEffectObjectAlphaFadeInFrame),
	    HLL_TODO_EXPORT(GetEffectObjectAlphaFadeOutFrame, ReignEngine_GetEffectObjectAlphaFadeOutFrame),
	    HLL_TODO_EXPORT(GetEffectObjectTextureAnimeFrame, ReignEngine_GetEffectObjectTextureAnimeFrame),
	    HLL_TODO_EXPORT(GetEffectObjectFrameReferenceType, ReignEngine_GetEffectObjectFrameReferenceType),
	    HLL_TODO_EXPORT(GetEffectObjectFrameReferenceParam, ReignEngine_GetEffectObjectFrameReferenceParam),
	    HLL_TODO_EXPORT(GetEffectObjectXRotationAngle, ReignEngine_GetEffectObjectXRotationAngle),
	    HLL_TODO_EXPORT(GetEffectObjectYRotationAngle, ReignEngine_GetEffectObjectYRotationAngle),
	    HLL_TODO_EXPORT(GetEffectObjectZRotationAngle, ReignEngine_GetEffectObjectZRotationAngle),
	    HLL_TODO_EXPORT(GetEffectObjectXRevolutionAngle, ReignEngine_GetEffectObjectXRevolutionAngle),
	    HLL_TODO_EXPORT(GetEffectObjectXRevolutionDistance, ReignEngine_GetEffectObjectXRevolutionDistance),
	    HLL_TODO_EXPORT(GetEffectObjectYRevolutionAngle, ReignEngine_GetEffectObjectYRevolutionAngle),
	    HLL_TODO_EXPORT(GetEffectObjectYRevolutionDistance, ReignEngine_GetEffectObjectYRevolutionDistance),
	    HLL_TODO_EXPORT(GetEffectObjectZRevolutionAngle, ReignEngine_GetEffectObjectZRevolutionAngle),
	    HLL_TODO_EXPORT(GetEffectObjectZRevolutionDistance, ReignEngine_GetEffectObjectZRevolutionDistance),
	    HLL_TODO_EXPORT(AddEffectObject, ReignEngine_AddEffectObject),
	    HLL_TODO_EXPORT(DeleteEffectObject, ReignEngine_DeleteEffectObject),
	    HLL_TODO_EXPORT(SetEffectObjectName, ReignEngine_SetEffectObjectName),
	    HLL_TODO_EXPORT(SetEffectObjectType, ReignEngine_SetEffectObjectType),
	    HLL_TODO_EXPORT(SetEffectObjectMoveType, ReignEngine_SetEffectObjectMoveType),
	    HLL_TODO_EXPORT(SetEffectObjectUpVecType, ReignEngine_SetEffectObjectUpVecType),
	    HLL_TODO_EXPORT(SetEffectObjectMoveCurve, ReignEngine_SetEffectObjectMoveCurve),
	    HLL_TODO_EXPORT(SetEffectObjectFrame, ReignEngine_SetEffectObjectFrame),
	    HLL_TODO_EXPORT(SetEffectObjectStopFrame, ReignEngine_SetEffectObjectStopFrame),
	    HLL_TODO_EXPORT(SetEffectObjectPosUnitType, ReignEngine_SetEffectObjectPosUnitType),
	    HLL_TODO_EXPORT(SetEffectObjectPosUnitIndex, ReignEngine_SetEffectObjectPosUnitIndex),
	    HLL_TODO_EXPORT(SetEffectObjectPosUnitParam, ReignEngine_SetEffectObjectPosUnitParam),
	    HLL_TODO_EXPORT(SetEffectObjectPosUnitString, ReignEngine_SetEffectObjectPosUnitString),
	    HLL_TODO_EXPORT(SetEffectObjectTexture, ReignEngine_SetEffectObjectTexture),
	    HLL_TODO_EXPORT(SetEffectObjectSize, ReignEngine_SetEffectObjectSize),
	    HLL_TODO_EXPORT(SetEffectObjectSize2, ReignEngine_SetEffectObjectSize2),
	    HLL_TODO_EXPORT(SetEffectObjectSizeX, ReignEngine_SetEffectObjectSizeX),
	    HLL_TODO_EXPORT(SetEffectObjectSizeY, ReignEngine_SetEffectObjectSizeY),
	    HLL_TODO_EXPORT(SetEffectObjectSizeType, ReignEngine_SetEffectObjectSizeType),
	    HLL_TODO_EXPORT(SetEffectObjectSizeXType, ReignEngine_SetEffectObjectSizeXType),
	    HLL_TODO_EXPORT(SetEffectObjectSizeYType, ReignEngine_SetEffectObjectSizeYType),
	    HLL_TODO_EXPORT(SetEffectObjectBlendType, ReignEngine_SetEffectObjectBlendType),
	    HLL_TODO_EXPORT(SetEffectObjectPolygonName, ReignEngine_SetEffectObjectPolygonName),
	    HLL_TODO_EXPORT(SetEffectNumofObjectParticle, ReignEngine_SetEffectNumofObjectParticle),
	    HLL_TODO_EXPORT(SetEffectObjectAlphaFadeInTime, ReignEngine_SetEffectObjectAlphaFadeInTime),
	    HLL_TODO_EXPORT(SetEffectObjectAlphaFadeOutTime, ReignEngine_SetEffectObjectAlphaFadeOutTime),
	    HLL_TODO_EXPORT(SetEffectObjectTextureAnimeTime, ReignEngine_SetEffectObjectTextureAnimeTime),
	    HLL_TODO_EXPORT(SetEffectObjectAlphaFadeInFrame, ReignEngine_SetEffectObjectAlphaFadeInFrame),
	    HLL_TODO_EXPORT(SetEffectObjectAlphaFadeOutFrame, ReignEngine_SetEffectObjectAlphaFadeOutFrame),
	    HLL_TODO_EXPORT(SetEffectObjectTextureAnimeFrame, ReignEngine_SetEffectObjectTextureAnimeFrame),
	    HLL_TODO_EXPORT(SetEffectObjectFrameReferenceType, ReignEngine_SetEffectObjectFrameReferenceType),
	    HLL_TODO_EXPORT(SetEffectObjectFrameReferenceParam, ReignEngine_SetEffectObjectFrameReferenceParam),
	    HLL_TODO_EXPORT(SetEffectObjectXRotationAngle, ReignEngine_SetEffectObjectXRotationAngle),
	    HLL_TODO_EXPORT(SetEffectObjectYRotationAngle, ReignEngine_SetEffectObjectYRotationAngle),
	    HLL_TODO_EXPORT(SetEffectObjectZRotationAngle, ReignEngine_SetEffectObjectZRotationAngle),
	    HLL_TODO_EXPORT(SetEffectObjectXRevolutionAngle, ReignEngine_SetEffectObjectXRevolutionAngle),
	    HLL_TODO_EXPORT(SetEffectObjectXRevolutionDistance, ReignEngine_SetEffectObjectXRevolutionDistance),
	    HLL_TODO_EXPORT(SetEffectObjectYRevolutionAngle, ReignEngine_SetEffectObjectYRevolutionAngle),
	    HLL_TODO_EXPORT(SetEffectObjectYRevolutionDistance, ReignEngine_SetEffectObjectYRevolutionDistance),
	    HLL_TODO_EXPORT(SetEffectObjectZRevolutionAngle, ReignEngine_SetEffectObjectZRevolutionAngle),
	    HLL_TODO_EXPORT(SetEffectObjectZRevolutionDistance, ReignEngine_SetEffectObjectZRevolutionDistance),
	    HLL_TODO_EXPORT(GetEffectObjectCurveLength, ReignEngine_GetEffectObjectCurveLength),
	    HLL_TODO_EXPORT(SetEffectObjectCurveLength, ReignEngine_SetEffectObjectCurveLength),
	    HLL_TODO_EXPORT(GetEffectObjectChildFrame, ReignEngine_GetEffectObjectChildFrame),
	    HLL_TODO_EXPORT(GetEffectObjectChildLength, ReignEngine_GetEffectObjectChildLength),
	    HLL_TODO_EXPORT(GetEffectObjectChildBeginSlope, ReignEngine_GetEffectObjectChildBeginSlope),
	    HLL_TODO_EXPORT(GetEffectObjectChildEndSlope, ReignEngine_GetEffectObjectChildEndSlope),
	    HLL_TODO_EXPORT(GetEffectObjectChildCreateBeginFrame, ReignEngine_GetEffectObjectChildCreateBeginFrame),
	    HLL_TODO_EXPORT(GetEffectObjectChildCreateEndFrame, ReignEngine_GetEffectObjectChildCreateEndFrame),
	    HLL_TODO_EXPORT(GetEffectObjectChildMoveDirType, ReignEngine_GetEffectObjectChildMoveDirType),
	    HLL_TODO_EXPORT(SetEffectObjectChildFrame, ReignEngine_SetEffectObjectChildFrame),
	    HLL_TODO_EXPORT(SetEffectObjectChildLength, ReignEngine_SetEffectObjectChildLength),
	    HLL_TODO_EXPORT(SetEffectObjectChildBeginSlope, ReignEngine_SetEffectObjectChildBeginSlope),
	    HLL_TODO_EXPORT(SetEffectObjectChildEndSlope, ReignEngine_SetEffectObjectChildEndSlope),
	    HLL_TODO_EXPORT(SetEffectObjectChildCreateBeginFrame, ReignEngine_SetEffectObjectChildCreateBeginFrame),
	    HLL_TODO_EXPORT(SetEffectObjectChildCreateEndFrame, ReignEngine_SetEffectObjectChildCreateEndFrame),
	    HLL_TODO_EXPORT(SetEffectObjectChildMoveDirType, ReignEngine_SetEffectObjectChildMoveDirType),
	    HLL_TODO_EXPORT(GetEffectObjectDirType, ReignEngine_GetEffectObjectDirType),
	    HLL_TODO_EXPORT(SetEffectObjectDirType, ReignEngine_SetEffectObjectDirType),
	    HLL_TODO_EXPORT(GetEffectNumofObjectDamage, ReignEngine_GetEffectNumofObjectDamage),
	    HLL_TODO_EXPORT(GetEffectObjectDamage, ReignEngine_GetEffectObjectDamage),
	    HLL_TODO_EXPORT(SetEffectObjectDamage, ReignEngine_SetEffectObjectDamage),
	    HLL_TODO_EXPORT(GetEffectObjectDraw, ReignEngine_GetEffectObjectDraw),
	    HLL_TODO_EXPORT(SetEffectObjectDraw, ReignEngine_SetEffectObjectDraw),
	    HLL_TODO_EXPORT(GetEffectObjectOffsetX, ReignEngine_GetEffectObjectOffsetX),
	    HLL_TODO_EXPORT(GetEffectObjectOffsetY, ReignEngine_GetEffectObjectOffsetY),
	    HLL_TODO_EXPORT(SetEffectObjectOffsetX, ReignEngine_SetEffectObjectOffsetX),
	    HLL_TODO_EXPORT(SetEffectObjectOffsetY, ReignEngine_SetEffectObjectOffsetY),
	    HLL_EXPORT(GetCameraQuakeEffectFlag, ReignEngine_GetCameraQuakeEffectFlag),
	    HLL_EXPORT(SetCameraQuakeEffectFlag, ReignEngine_SetCameraQuakeEffectFlag),
	    HLL_EXPORT(SetCameraPos, ReignEngine_SetCameraPos),
	    HLL_EXPORT(SetCameraAngle, ReignEngine_SetCameraAngle),
	    HLL_EXPORT(SetCameraAngleP, ReignEngine_SetCameraAngleP),
	    HLL_EXPORT(SetCameraAngleB, ReignEngine_SetCameraAngleB),
	    HLL_TODO_EXPORT(GetShadowBias, ReignEngine_GetShadowBias),
	    HLL_TODO_EXPORT(GetShadowTargetDistance, ReignEngine_GetShadowTargetDistance),
	    HLL_EXPORT(GetShadowMapResolutionLevel, ReignEngine_GetShadowMapResolutionLevel),
	    HLL_TODO_EXPORT(GetShadowSplitDepth, ReignEngine_GetShadowSplitDepth),
	    HLL_EXPORT(SetShadowMapType, ReignEngine_SetShadowMapType),
	    HLL_EXPORT(SetShadowMapLightLookPos, ReignEngine_SetShadowMapLightLookPos),
	    HLL_EXPORT(SetShadowMapLightDir, ReignEngine_SetShadowMapLightDir),
	    HLL_EXPORT(SetShadowBox, ReignEngine_SetShadowBox),
	    HLL_EXPORT(SetShadowBias, ReignEngine_SetShadowBias),
	    HLL_TODO_EXPORT(SetShadowSlopeBias, ReignEngine_SetShadowSlopeBias),
	    HLL_TODO_EXPORT(SetShadowFilterMag, ReignEngine_SetShadowFilterMag),
	    HLL_TODO_EXPORT(SetShadowTargetDistance, ReignEngine_SetShadowTargetDistance),
	    HLL_EXPORT(SetShadowMapResolutionLevel, ReignEngine_SetShadowMapResolutionLevel),
	    HLL_TODO_EXPORT(SetShadowSplitDepth, ReignEngine_SetShadowSplitDepth),
	    HLL_EXPORT(SetFogType, ReignEngine_SetFogType),
	    HLL_EXPORT(SetFogNear, ReignEngine_SetFogNear),
	    HLL_EXPORT(SetFogFar, ReignEngine_SetFogFar),
	    HLL_EXPORT(SetFogColor, ReignEngine_SetFogColor),
	    HLL_TODO_EXPORT(GetFogType, ReignEngine_GetFogType),
	    HLL_TODO_EXPORT(GetFogNear, ReignEngine_GetFogNear),
	    HLL_TODO_EXPORT(GetFogFar, ReignEngine_GetFogFar),
	    HLL_TODO_EXPORT(GetFogColor, ReignEngine_GetFogColor),
	    HLL_EXPORT(LoadLightScatteringSetting, ReignEngine_LoadLightScatteringSetting),
	    HLL_TODO_EXPORT(GetLSBetaR, ReignEngine_GetLSBetaR),
	    HLL_TODO_EXPORT(GetLSBetaM, ReignEngine_GetLSBetaM),
	    HLL_TODO_EXPORT(GetLSG, ReignEngine_GetLSG),
	    HLL_TODO_EXPORT(GetLSDistance, ReignEngine_GetLSDistance),
	    HLL_TODO_EXPORT(GetLSLightDir, ReignEngine_GetLSLightDir),
	    HLL_TODO_EXPORT(GetLSLightColor, ReignEngine_GetLSLightColor),
	    HLL_TODO_EXPORT(GetLSSunColor, ReignEngine_GetLSSunColor),
	    HLL_EXPORT(SetLSBetaR, ReignEngine_SetLSBetaR),
	    HLL_EXPORT(SetLSBetaM, ReignEngine_SetLSBetaM),
	    HLL_EXPORT(SetLSG, ReignEngine_SetLSG),
	    HLL_EXPORT(SetLSDistance, ReignEngine_SetLSDistance),
	    HLL_EXPORT(SetLSLightDir, ReignEngine_SetLSLightDir),
	    HLL_EXPORT(SetLSLightColor, ReignEngine_SetLSLightColor),
	    HLL_EXPORT(SetLSSunColor, ReignEngine_SetLSSunColor),
	    HLL_TODO_EXPORT(SetDrawTextureFog, ReignEngine_SetDrawTextureFog),
	    HLL_TODO_EXPORT(GetDrawTextureFog, ReignEngine_GetDrawTextureFog),
	    HLL_EXPORT(SetOptionAntiAliasing, ReignEngine_SetOptionAntiAliasing),
	    HLL_EXPORT(SetOptionWaitVSync, ReignEngine_SetOptionWaitVSync),
	    HLL_EXPORT(GetOptionAntiAliasing, ReignEngine_GetOptionAntiAliasing),
	    HLL_EXPORT(GetOptionWaitVSync, ReignEngine_GetOptionWaitVSync),
	    HLL_TODO_EXPORT(SetViewport, ReignEngine_SetViewport),
	    HLL_EXPORT(SetProjection, ReignEngine_SetProjection),
	    HLL_TODO_EXPORT(GetBrightness, ReignEngine_GetBrightness),
	    HLL_TODO_EXPORT(SetBrightness, ReignEngine_SetBrightness),
	    HLL_EXPORT(SetRenderMode, ReignEngine_SetRenderMode),
	    HLL_EXPORT(SetShadowMode, ReignEngine_SetShadowMode),
	    HLL_EXPORT(SetBumpMode, ReignEngine_SetBumpMode),
	    HLL_TODO_EXPORT(SetHDRMode, ReignEngine_SetHDRMode),
	    HLL_EXPORT(SetVertexBlendMode, ReignEngine_SetVertexBlendMode),
	    HLL_EXPORT(SetFogMode, ReignEngine_SetFogMode),
	    HLL_EXPORT(SetSpecularMode, ReignEngine_SetSpecularMode),
	    HLL_EXPORT(SetLightMapMode, ReignEngine_SetLightMapMode),
	    HLL_EXPORT(SetSoftFogEdgeMode, ReignEngine_SetSoftFogEdgeMode),
	    HLL_EXPORT(SetSSAOMode, ReignEngine_SetSSAOMode),
	    HLL_EXPORT(SetShaderPrecisionMode, ReignEngine_SetShaderPrecisionMode),
	    HLL_TODO_EXPORT(SetShaderDebugMode, ReignEngine_SetShaderDebugMode),
	    HLL_EXPORT(GetRenderMode, ReignEngine_GetRenderMode),
	    HLL_EXPORT(GetShadowMode, ReignEngine_GetShadowMode),
	    HLL_EXPORT(GetBumpMode, ReignEngine_GetBumpMode),
	    HLL_TODO_EXPORT(GetHDRMode, ReignEngine_GetHDRMode),
	    HLL_EXPORT(GetVertexBlendMode, ReignEngine_GetVertexBlendMode),
	    HLL_EXPORT(GetFogMode, ReignEngine_GetFogMode),
	    HLL_EXPORT(GetSpecularMode, ReignEngine_GetSpecularMode),
	    HLL_EXPORT(GetLightMapMode, ReignEngine_GetLightMapMode),
	    HLL_EXPORT(GetSoftFogEdgeMode, ReignEngine_GetSoftFogEdgeMode),
	    HLL_EXPORT(GetSSAOMode, ReignEngine_GetSSAOMode),
	    HLL_EXPORT(GetShaderPrecisionMode, ReignEngine_GetShaderPrecisionMode),
	    HLL_TODO_EXPORT(GetShaderDebugMode, ReignEngine_GetShaderDebugMode),
	    HLL_EXPORT(GetTextureResolutionLevel, ReignEngine_GetTextureResolutionLevel),
	    HLL_EXPORT(GetTextureFilterMode, ReignEngine_GetTextureFilterMode),
	    HLL_EXPORT(SetTextureResolutionLevel, ReignEngine_SetTextureResolutionLevel),
	    HLL_EXPORT(SetTextureFilterMode, ReignEngine_SetTextureFilterMode),
	    HLL_EXPORT(GetUsePower2Texture, ReignEngine_GetUsePower2Texture),
	    HLL_EXPORT(SetUsePower2Texture, ReignEngine_SetUsePower2Texture),
	    HLL_EXPORT(GetGlobalAmbient, ReignEngine_GetGlobalAmbient),
	    HLL_EXPORT(SetGlobalAmbient, ReignEngine_SetGlobalAmbient),
	    HLL_EXPORT(GetBloomMode, ReignEngine_GetBloomMode),
	    HLL_EXPORT(SetBloomMode, ReignEngine_SetBloomMode),
	    HLL_EXPORT(GetGlareMode, ReignEngine_GetGlareMode),
	    HLL_EXPORT(SetGlareMode, ReignEngine_SetGlareMode),
	    HLL_EXPORT(GetPostEffectFilterY, ReignEngine_GetPostEffectFilterY),
	    HLL_EXPORT(GetPostEffectFilterCb, ReignEngine_GetPostEffectFilterCb),
	    HLL_EXPORT(GetPostEffectFilterCr, ReignEngine_GetPostEffectFilterCr),
	    HLL_EXPORT(SetPostEffectFilterY, ReignEngine_SetPostEffectFilterY),
	    HLL_EXPORT(SetPostEffectFilterCb, ReignEngine_SetPostEffectFilterCb),
	    HLL_EXPORT(SetPostEffectFilterCr, ReignEngine_SetPostEffectFilterCr),
	    HLL_EXPORT(GetBackCGName, ReignEngine_GetBackCGName),
	    HLL_EXPORT(GetBackCGNum, ReignEngine_GetBackCGNum),
	    HLL_EXPORT(GetBackCGBlendRate, ReignEngine_GetBackCGBlendRate),
	    HLL_EXPORT(GetBackCGDestPosX, ReignEngine_GetBackCGDestPosX),
	    HLL_EXPORT(GetBackCGDestPosY, ReignEngine_GetBackCGDestPosY),
	    HLL_EXPORT(GetBackCGMag, ReignEngine_GetBackCGMag),
	    HLL_TODO_EXPORT(GetBackCGQuakeMag, ReignEngine_GetBackCGQuakeMag),
	    HLL_EXPORT(GetBackCGShow, ReignEngine_GetBackCGShow),
	    HLL_EXPORT(SetBackCGName, ReignEngine_SetBackCGName),
	    HLL_EXPORT(SetBackCGNum, ReignEngine_SetBackCGNum),
	    HLL_EXPORT(SetBackCGBlendRate, ReignEngine_SetBackCGBlendRate),
	    HLL_EXPORT(SetBackCGDestPos, ReignEngine_SetBackCGDestPos),
	    HLL_EXPORT(SetBackCGMag, ReignEngine_SetBackCGMag),
	    HLL_TODO_EXPORT(SetBackCGQuakeMag, ReignEngine_SetBackCGQuakeMag),
	    HLL_EXPORT(SetBackCGShow, ReignEngine_SetBackCGShow),
	    HLL_TODO_EXPORT(GetGlareBrightnessParam, ReignEngine_GetGlareBrightnessParam),
	    HLL_EXPORT(SetGlareBrightnessParam, ReignEngine_SetGlareBrightnessParam),
	    HLL_TODO_EXPORT(GetSSAOParam, ReignEngine_GetSSAOParam),
	    HLL_EXPORT(SetSSAOParam, ReignEngine_SetSSAOParam),
	    HLL_TODO_EXPORT(CalcIntersectEyeVec, ReignEngine_CalcIntersectEyeVec),
	    HLL_EXPORT(IsLoading, ReignEngine_IsLoading),
	    HLL_TODO_EXPORT(GetDebugInfoMode, ReignEngine_GetDebugInfoMode),
	    HLL_TODO_EXPORT(SetDebugInfoMode, ReignEngine_SetDebugInfoMode),
	    HLL_EXPORT(GetVertexShaderVersion, ReignEngine_GetVertexShaderVersion),
	    HLL_EXPORT(GetPixelShaderVersion, ReignEngine_GetPixelShaderVersion),
	    HLL_EXPORT(SetInstanceSpecularReflectRate, ReignEngine_SetInstanceSpecularReflectRate),
	    HLL_EXPORT(SetInstanceFresnelReflectRate, ReignEngine_SetInstanceFresnelReflectRate),
	    HLL_TODO_EXPORT(GetInstanceSpecularReflectRate, ReignEngine_GetInstanceSpecularReflectRate),
	    HLL_TODO_EXPORT(GetInstanceFresnelReflectRate, ReignEngine_GetInstanceFresnelReflectRate),
	    HLL_TODO_EXPORT(StringToFloat, ReignEngine_StringToFloat),
	    HLL_TODO_EXPORT(DrawToMainSurface, ReignEngine_DrawToMainSurface)
	    );
