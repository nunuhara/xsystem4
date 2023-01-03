/* Copyright (C) 2022 nao1215 <n.chika156@gmail.com>
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

#include "hll.h"

//static int DrawField_Init(int nSurface);
//static void DrawField_Release(int nSurface);
//static void DrawField_SetDrawFlag(int nSurface, int nFlag);
//static bool DrawField_GetDrawFlag(int nSurface);
//static int DrawField_LoadFromFile(int nSurface, struct string szFileName, int nNum);
//static int DrawField_LoadTexture(int nSurface, struct string szFileName);
//static int DrawField_LoadEventTexture(int nSurface, string szFileName);
//static bool DrawField_BeginLoad(int nSurface, string szFileName, int nNum);
//static bool DrawField_IsLoadEnd(int nSurface);
//static bool DrawField_StopLoad(int nSurface);
//static float DrawField_GetLoadPercent(int nSurface);
//static bool DrawField_IsLoadSucceeded(int nSurface);
//static bool DrawField_BeginLoadTexture(int nSurface, string szFileName);
//static bool DrawField_IsLoadTextureEnd(int nSurface);
//static bool DrawField_StopLoadTexture(int nSurface);
//static float DrawField_GetLoadTexturePercent(int nSurface);
//static bool DrawField_IsLoadTextureSucceeded(int nSurface);
//static void DrawField_UpdateTexture(int nSurface);
//static void DrawField_SetCamera(int nSurface, float fX, float fY, float fZ, float fAngle, float fAngleP);
//static int DrawField_GetMapX(int nSurface);
//static int DrawField_GetMapY(int nSurface);
//static int DrawField_GetMapZ(int nSurface);
//static int DrawField_IsInMap(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsFloor(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsStair(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsStairN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsStairW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsStairS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsStairE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsWallN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsWallW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsWallS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsWallE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsDoorN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsDoorW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsDoorS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsDoorE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsEnter(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsEnterN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsEnterW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsEnterS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_IsEnterE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvFloor(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvFloor2(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallN2(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallW2(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallS2(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetEvWallE2(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexFloor(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexCeiling(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexWallN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexWallW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexWallS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexWallE(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexDoorN(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexDoorW(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexDoorS(int nSurface, int nX, int nY, int nZ);
//static int DrawField_GetTexDoorE(int nSurface, int nX, int nY, int nZ);
//static bool DrawField_GetDoorNAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//static bool DrawField_GetDoorWAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//static bool DrawField_GetDoorSAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//static bool DrawField_GetDoorEAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//static bool DrawField_SetDoorNAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//static bool DrawField_SetDoorWAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//static bool DrawField_SetDoorSAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//static bool DrawField_SetDoorEAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//static void DrawField_SetEvFloor(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallN(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallW(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallS(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallE(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvFloor2(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallN2(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallW2(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallS2(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvWallE2(int nSurface, int nX, int nY, int nZ, int nEvent);
//static void DrawField_SetEvMag(int nSurface, int nX, int nY, int nZ, float fMag);
//static void DrawField_SetEvRate(int nSurface, int nX, int nY, int nZ, int nRate);
//static void DrawField_SetEnter(int nSurface, int nX, int nY, int nZ, int nFlag);
//static void DrawField_SetEnterN(int nSurface, int nX, int nY, int nZ, int nFlag);
//static void DrawField_SetEnterW(int nSurface, int nX, int nY, int nZ, int nFlag);
//static void DrawField_SetEnterS(int nSurface, int nX, int nY, int nZ, int nFlag);
//static void DrawField_SetEnterE(int nSurface, int nX, int nY, int nZ, int nFlag);
//static bool DrawField_SetDoorNLock(int nSurface, int nX, int nY, int nZ, int nLock);
//static bool DrawField_SetDoorWLock(int nSurface, int nX, int nY, int nZ, int nLock);
//static bool DrawField_SetDoorSLock(int nSurface, int nX, int nY, int nZ, int nLock);
//static bool DrawField_SetDoorELock(int nSurface, int nX, int nY, int nZ, int nLock);
//static bool DrawField_GetDoorNLock(int nSurface, int nX, int nY, int nZ, int *pnLock);
//static bool DrawField_GetDoorWLock(int nSurface, int nX, int nY, int nZ, int *pnLock);
//static bool DrawField_GetDoorSLock(int nSurface, int nX, int nY, int nZ, int *pnLock);
//static bool DrawField_GetDoorELock(int nSurface, int nX, int nY, int nZ, int *pnLock);
//static void DrawField_SetTexFloor(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexCeiling(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexWallN(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexWallW(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexWallS(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexWallE(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexDoorN(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexDoorW(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexDoorS(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexDoorE(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetTexStair(int nSurface, int nX, int nY, int nZ, int nTexture, int nType);
//static void DrawField_SetShadowTexFloor(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexCeiling(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexWallN(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexWallW(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexWallS(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexWallE(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexDoorN(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexDoorW(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexDoorS(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_SetShadowTexDoorE(int nSurface, int nX, int nY, int nZ, int nTexture);
//static void DrawField_CalcShadowMap(int nSurface);
//static void DrawField_DrawMap(int nSurface, int nSprite);
//static void DrawField_SetMapAllViewFlag(int nSurfacce, int nFlag);
//static void DrawField_SetDrawMapFloor(int nSurface, int nFloor);
//static void DrawField_DrawLMap(int nSurface, int nSprite);
//static void DrawField_SetMapCG(int nSurface, int nIndex, int nSprite);
//static void DrawField_SetDrawLMapFloor(int nSurface, int nFloor);
//static void DrawField_SetPlayerPos(int nSurface, int nX, int nY, int nZ);
//static void DrawField_SetWalked(int nSurface, int nX, int nY, int nZ, int nFlag);
//static void DrawField_SetLooked(int nSurface, int nX, int nY, int nZ, bool bFlag);
//static bool DrawField_SetHideDrawMapFloor(int nSurface, int nFloor, bool bHide);
//static bool DrawField_SetHideDrawMapWall(int nSurface, int nWall, bool bHide);
//static void DrawField_SetDrawHalfFlag(int nSurface, int nFlag);
//static int DrawField_GetDrawHalfFlag(int nSurface);
//static void DrawField_SetInterlaceMode(int nSurface, int nFlag);
//static bool DrawField_SetDirect3DMode(int nSurface, int nFlag);
//static bool DrawField_GetDirect3DMode(int nSurface);
//static void DrawField_SaveDrawSettingFlag(int nDirect3D, int nInterlace, int nHalf);
//static void DrawField_SetPerspective(int nSurface, int nWidth, int nHeight, float fNear, float fFar, float fDeg);
//static void DrawField_SetDrawShadowMap(int nSurface, bool bDrawShadowMap);
//static int DrawField_CalcNumofFloor(int nSurface);
//static int DrawField_CalcNumofWalk(int nSurface);
//static int DrawField_CalcNumofWalk2(int nSurface);
//static bool DrawField_IsPVSData(int nSurface);
//static float DrawField_CosDeg(float fDeg);
//static float DrawField_SinDeg(float fDeg);
//static float DrawField_TanDeg(float fDeg);
//static float DrawField_Sqrt(float f);
//static float DrawField_Atan2(float fY, float fX);
//static bool DrawField_TransPos2DToPos3DOnPlane(int nSurface, int nScreenX, int nScreenY, float fPlaneY, float *pfX, float *pfY, float *pfZ);
//static bool DrawField_TransPos3DToPos2D(int nSurace, float fX, float fY, float fZ, int *pnScreenX, int *pnScreenY);
//static int DrawField_GetCharaNumMax(int nSurface);
//static void DrawField_SetCharaSprite(int nSurface, int nNum, int nSprite);
//static void DrawField_SetCharaPos(int nSurface, int nNum, float fX, float fY, float fZ);
//static void DrawField_SetCharaCG(int nSurface, int nNum, int nCG);
//static void DrawField_SetCharaCGInfo(int nSurface, int nNum, int nNumofCharaX, int nNumofCharaY);
//static void DrawField_SetCharaZBias(int nSurface, float fZBias0, float fZBias1, float fZBias2, float fZBias3);
//static void DrawField_SetCharaShow(int nSurface, int nNum, bool bShow);
//static void DrawField_SetCenterOffsetY(int nSurface, float fY);
//static void DrawField_SetBuilBoard(int nSurface, int nNum, int nSprite);
//static void DrawField_SetSphereTheta(int nSurface, float fX, float fY, float fZ);
//static void DrawField_SetSphereColor(int nSurface, float fTop, float fBottom);
//static bool DrawField_GetSphereTheta(int nSurface, float *fX, float *fY, float *fZ);
//static bool DrawField_GetSphereColor(int nSurface, float *fTop, float *fBottom);
//static bool DrawField_GetBackColor(int nSurface, int *pnR, int *pnG, int *pnB);
//static void DrawField_SetBackColor(int nSurface, int nR, int nG, int nB);
//static int DrawField_GetPolyObj(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjMag(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjRotateH(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjRotateP(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjRotateB(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjOffsetX(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjOffsetY(int nSurface, int nX, int nY, int nZ);
//static float DrawField_GetPolyObjOffsetZ(int nSurface, int nX, int nY, int nZ);
//static void DrawField_SetPolyObj(int nSurface, int nX, int nY, int nZ, int nPolyObj);
//static void DrawField_SetPolyObjMag(int nSurface, int nX, int nY, int nZ, float fMag);
//static void DrawField_SetPolyObjRotateH(int nSurface, int nX, int nY, int nZ, float fRotateH);
//static void DrawField_SetPolyObjRotateP(int nSurface, int nX, int nY, int nZ, float fRotateP);
//static void DrawField_SetPolyObjRotateB(int nSurface, int nX, int nY, int nZ, float fRotateB);
//static void DrawField_SetPolyObjOffsetX(int nSurface, int nX, int nY, int nZ, float fOffsetX);
//static void DrawField_SetPolyObjOffsetY(int nSurface, int nX, int nY, int nZ, float fOffsetY);
//static void DrawField_SetPolyObjOffsetZ(int nSurface, int nX, int nY, int nZ, float fOffsetZ);
//static void DrawField_SetDrawObjFlag(int nSurface, int nType, bool bFlag);
//static bool DrawField_GetDrawObjFlag(int nSurface, int nType);
//static int DrawField_GetTexSound(int nSurface, int nType, int nNum);
//static int DrawField_GetNumofTexSound(int nSurface, int nType);


HLL_WARN_UNIMPLEMENTED(0, int, DrawField, Init, int nSurface);
HLL_WARN_UNIMPLEMENTED(, void, DrawField, Release, int nSurface);
HLL_WARN_UNIMPLEMENTED(, void, DrawField, SetDrawFlag, int nSurface, int nFlag);
HLL_WARN_UNIMPLEMENTED(true, bool, DrawField, GetDrawFlag, int nSurface);
HLL_WARN_UNIMPLEMENTED(, void, DrawField, Sqrt, void);

HLL_LIBRARY(DrawField,
		HLL_EXPORT(Sqrt, DrawField_Init),
		HLL_EXPORT(Sqrt, DrawField_Release),
		HLL_EXPORT(Sqrt, DrawField_SetDrawFlag),
		HLL_EXPORT(Sqrt, DrawField_GetDrawFlag),
		HLL_EXPORT(Sqrt, DrawField_Sqrt)
        );