/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <string.h>
#include <assert.h>

#include "system4/ain.h"

#include "gfx/gfx.h"
#include "parts.h"
#include "hll.h"

static void GUIEngine_PreLink(void);

static void GUIEngine_ModuleInit(void)
{
	gfx_init();
	PE_Init();
}

static void GUIEngine_ModuleFini(void)
{
	PE_Reset();
}

bool GUIEngine_Save(struct page **buffer)
{
	return true;
}

bool GUIEngine_Load(struct page **buffer)
{
	return true;
}

//static void GUIEngine_Release(int PartsNumber);

static void GUIEngine_ReleaseAll(possibly_unused struct page **erase_number_list)
{
	PE_ReleaseAllParts();
}

static void GUIEngine_ReleaseAllWithoutSystem(possibly_unused struct page **erase_number_list)
{
	PE_ReleaseAllPartsWithoutSystem();
}

//static void GUIEngine_SetDelegateIndex(int PartsNumber, int DelegateIndex);
//static int GUIEngine_GetFreeNumber(void)
//static bool GUIEngine_IsExist(int PartsNumber);
//static void GUIEngine_PushController(void);
//static void GUIEngine_PopController(ref array<int> EraseNumberList);
//static void GUIEngine_UpdateComponent(possibly_unused int passed_time)
//static void GUIEngine_BeginInput(void);
//static void GUIEngine_EndInput(void);
//static void GUIEngine_UpdateInputState(int PassedTime);
//static void GUIEngine_UpdateParts(int PassedTime, bool IsSkip, bool MessageWindowShow);
//static void GUIEngine_SetFocus(int PartsNumber);
//static bool GUIEngine_IsFocus(int PartsNumber);
//static void GUIEngine_StopSoundWithoutSystemSound(void);
HLL_WARN_UNIMPLEMENTED(, void, GUIEngine, StopSoundWithoutSystemSound);
//static void GUIEngine_ReleaseMessage(void);
HLL_QUIET_UNIMPLEMENTED(, void, GUIEngine, ReleaseMessage);
//static void GUIEngine_PopMessage(void);
//static int GUIEngine_GetMessagePartsNumber(void);
//static int GUIEngine_GetMessageDelegateIndex(void);
//static int GUIEngine_GetDelegateIndex(int PartsNumber);
//static int GUIEngine_GetMessageType(void);
HLL_QUIET_UNIMPLEMENTED(0, int, GUIEngine, GetMessageType);
//static int GUIEngine_GetMessageVariableCount(void);
//static int GUIEngine_GetMessageVariableType(int MessageVariableIndex);
//static int GUIEngine_GetMessageVariableInt(int MessageVariableIndex);
//static float GUIEngine_GetMessageVariableFloat(int MessageVariableIndex);
//static bool GUIEngine_GetMessageVariableBool(int MessageVariableIndex);
//static void GUIEngine_GetMessageVariableString(int MessageVariableIndex, ref string String);
//static void GUIEngine_SetButtonPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetButtonZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetButtonX(int PartsNumber);
//static int GUIEngine_GetButtonY(int PartsNumber);
//static int GUIEngine_GetButtonZ(int PartsNumber);
//static void GUIEngine_SetButtonWidth(int PartsNumber, int Width);
//static void GUIEngine_SetButtonHeight(int PartsNumber, int Height);
//static int GUIEngine_GetButtonWidth(int PartsNumber);
//static int GUIEngine_GetButtonHeight(int PartsNumber);
//static void GUIEngine_SetButtonShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsButtonShow(int PartsNumber);
//static void GUIEngine_SetButtonDrag(int PartsNumber, bool Drag);
//static bool GUIEngine_IsButtonDrag(int PartsNumber);
//static void GUIEngine_SetButtonEnable(int PartsNumber, bool Enable);
//static bool GUIEngine_IsButtonEnable(int PartsNumber);
//static void GUIEngine_SetButtonPixelDecide(int PartsNumber, bool PixelDecide);
//static bool GUIEngine_IsButtonPixelDecide(int PartsNumber);
//static void GUIEngine_SetButtonR(int PartsNumber, int Red);
//static void GUIEngine_SetButtonG(int PartsNumber, int Green);
//static void GUIEngine_SetButtonB(int PartsNumber, int Blue);
//static int GUIEngine_GetButtonR(int PartsNumber);
//static int GUIEngine_GetButtonG(int PartsNumber);
//static int GUIEngine_GetButtonB(int PartsNumber);
//static void GUIEngine_SetButtonFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetButtonFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetButtonOnCursorSoundNumber(int PartsNumber, int SoundNumber);
//static void GUIEngine_SetButtonClickSoundNumber(int PartsNumber, int SoundNumber);
//static int GUIEngine_GetButtonOnCursorSoundNumber(int PartsNumber);
//static int GUIEngine_GetButtonClickSoundNumber(int PartsNumber);
//static void GUIEngine_SetButtonCGName(int PartsNumber, string Name);
//static void GUIEngine_GetButtonCGName(int PartsNumber, ref string Name);
//static void GUIEngine_SetButtonText(int PartsNumber, string pIText);
//static void GUIEngine_GetButtonText(int PartsNumber, ref string pIText);
//static void GUIEngine_SetCGBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetCGBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetCGBoxX(int PartsNumber);
//static int GUIEngine_GetCGBoxY(int PartsNumber);
//static int GUIEngine_GetCGBoxZ(int PartsNumber);
//static void GUIEngine_SetCGBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetCGBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetCGBoxWidth(int PartsNumber);
//static int GUIEngine_GetCGBoxHeight(int PartsNumber);
//static void GUIEngine_SetCGBoxAlpha(int PartsNumber, int Alpha);
//static int GUIEngine_GetCGBoxAlpha(int PartsNumber);
//static void GUIEngine_SetCGBoxViewMode(int PartsNumber, int ViewMode);
//static int GUIEngine_GetCGBoxViewMode(int PartsNumber);
//static void GUIEngine_SetCGBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsCGBoxShow(int PartsNumber);
//static void GUIEngine_SetCGBoxDrag(int PartsNumber, bool Drag);
//static bool GUIEngine_IsCGBoxDrag(int PartsNumber);
//static void GUIEngine_SetCGBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetCGBoxCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetCGBoxAddR(int PartsNumber, int Red);
//static void GUIEngine_SetCGBoxAddG(int PartsNumber, int Green);
//static void GUIEngine_SetCGBoxAddB(int PartsNumber, int Blue);
//static int GUIEngine_GetCGBoxAddR(int PartsNumber);
//static int GUIEngine_GetCGBoxAddG(int PartsNumber);
//static int GUIEngine_GetCGBoxAddB(int PartsNumber);
//static void GUIEngine_SetCGBoxMulR(int PartsNumber, int Red);
//static void GUIEngine_SetCGBoxMulG(int PartsNumber, int Green);
//static void GUIEngine_SetCGBoxMulB(int PartsNumber, int Blue);
//static int GUIEngine_GetCGBoxMulR(int PartsNumber);
//static int GUIEngine_GetCGBoxMulG(int PartsNumber);
//static int GUIEngine_GetCGBoxMulB(int PartsNumber);
//static void GUIEngine_SetCheckBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetCheckBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetCheckBoxX(int PartsNumber);
//static int GUIEngine_GetCheckBoxY(int PartsNumber);
//static int GUIEngine_GetCheckBoxZ(int PartsNumber);
//static void GUIEngine_SetCheckBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetCheckBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetCheckBoxWidth(int PartsNumber);
//static int GUIEngine_GetCheckBoxHeight(int PartsNumber);
//static void GUIEngine_SetCheckBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsCheckBoxShow(int PartsNumber);
//static void GUIEngine_SetCheckBoxDrag(int PartsNumber, bool Drag);
//static bool GUIEngine_IsCheckBoxDrag(int PartsNumber);
//static void GUIEngine_CheckBoxChecked(int PartsNumber, bool Check);
//static bool GUIEngine_IsCheckBoxChecked(int PartsNumber);
//static void GUIEngine_SetCheckBoxR(int PartsNumber, int Red);
//static void GUIEngine_SetCheckBoxG(int PartsNumber, int Green);
//static void GUIEngine_SetCheckBoxB(int PartsNumber, int Blue);
//static int GUIEngine_GetCheckBoxR(int PartsNumber);
//static int GUIEngine_GetCheckBoxG(int PartsNumber);
//static int GUIEngine_GetCheckBoxB(int PartsNumber);
//static void GUIEngine_SetCheckBoxFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetCheckBoxFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetCheckBoxOnCursorSoundNumber(int PartsNumber, int SoundNumber);
//static void GUIEngine_SetCheckBoxClickSoundNumber(int PartsNumber, int SoundNumber);
//static int GUIEngine_GetCheckBoxOnCursorSoundNumber(int PartsNumber);
//static int GUIEngine_GetCheckBoxClickSoundNumber(int PartsNumber);
//static void GUIEngine_SetCheckBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetCheckBoxCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetCheckBoxText(int PartsNumber, string pIName);
//static void GUIEngine_GetCheckBoxText(int PartsNumber, ref string pIName);
//static void GUIEngine_SetLabelPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetLabelZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetLabelX(int PartsNumber);
//static int GUIEngine_GetLabelY(int PartsNumber);
//static int GUIEngine_GetLabelZ(int PartsNumber);
//static void GUIEngine_SetLabelWidth(int PartsNumber, int Width);
//static void GUIEngine_SetLabelHeight(int PartsNumber, int Height);
//static int GUIEngine_GetLabelWidth(int PartsNumber);
//static int GUIEngine_GetLabelHeight(int PartsNumber);
//static void GUIEngine_SetLabelAlpha(int PartsNumber, int Alpha);
//static int GUIEngine_GetLabelAlpha(int PartsNumber);
//static void GUIEngine_SetLabelShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsLabelShow(int PartsNumber);
//static void GUIEngine_SetLabelDrag(int PartsNumber, bool Drag);
//static bool GUIEngine_IsLabelDrag(int PartsNumber);
//static void GUIEngine_SetLabelClip(int PartsNumber, bool Clip);
//static bool GUIEngine_IsLabelClip(int PartsNumber);
//static void GUIEngine_SetLabelText(int PartsNumber, string pIText);
//static void GUIEngine_GetLabelText(int PartsNumber, ref string pIText);
//static void GUIEngine_SetLabelFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetLabelFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetVScrollbarPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetVScrollbarZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetVScrollbarX(int PartsNumber);
//static int GUIEngine_GetVScrollbarY(int PartsNumber);
//static int GUIEngine_GetVScrollbarZ(int PartsNumber);
//static void GUIEngine_SetVScrollbarShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsVScrollbarShow(int PartsNumber);
//static void GUIEngine_SetVScrollbarOnCursorSoundNumber(int PartsNumber, int SoundNumber);
//static void GUIEngine_SetVScrollbarClickSoundNumber(int PartsNumber, int SoundNumber);
//static int GUIEngine_GetVScrollbarOnCursorSoundNumber(int PartsNumber);
//static int GUIEngine_GetVScrollbarClickSoundNumber(int PartsNumber);
//static void GUIEngine_SetVScrollbarWidth(int PartsNumber, int Width);
//static void GUIEngine_SetVScrollbarHeight(int PartsNumber, int Height);
//static void GUIEngine_SetVScrollbarUpHeight(int PartsNumber, int Height);
//static void GUIEngine_SetVScrollbarDownHeight(int PartsNumber, int Height);
//static int GUIEngine_GetVScrollbarWidth(int PartsNumber);
//static int GUIEngine_GetVScrollbarHeight(int PartsNumber);
//static int GUIEngine_GetVScrollbarUpHeight(int PartsNumber);
//static int GUIEngine_GetVScrollbarDownHeight(int PartsNumber);
//static void GUIEngine_SetVScrollbarTotalSize(int PartsNumber, int Size);
//static void GUIEngine_SetVScrollbarViewSize(int PartsNumber, int Size);
//static void GUIEngine_SetVScrollbarScrollPos(int PartsNumber, int Pos);
//static void GUIEngine_SetVScrollbarMoveSizeByButton(int PartsNumber, int Size);
//static int GUIEngine_GetVScrollbarTotalSize(int PartsNumber);
//static int GUIEngine_GetVScrollbarViewSize(int PartsNumber);
//static int GUIEngine_GetVScrollbarScrollPos(int PartsNumber);
//static int GUIEngine_GetVScrollbarMoveSizeByButton(int PartsNumber);
//static void GUIEngine_SetVScrollbarCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetVScrollbarCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetHScrollbarPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetHScrollbarZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetHScrollbarX(int PartsNumber);
//static int GUIEngine_GetHScrollbarY(int PartsNumber);
//static int GUIEngine_GetHScrollbarZ(int PartsNumber);
//static void GUIEngine_SetHScrollbarShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsHScrollbarShow(int PartsNumber);
//static void GUIEngine_SetHScrollbarOnCursorSoundNumber(int PartsNumber, int SoundNumber);
//static void GUIEngine_SetHScrollbarClickSoundNumber(int PartsNumber, int SoundNumber);
//static int GUIEngine_GetHScrollbarOnCursorSoundNumber(int PartsNumber);
//static int GUIEngine_GetHScrollbarClickSoundNumber(int PartsNumber);
//static void GUIEngine_SetHScrollbarWidth(int PartsNumber, int Width);
//static void GUIEngine_SetHScrollbarHeight(int PartsNumber, int Height);
//static void GUIEngine_SetHScrollbarLeftWidth(int PartsNumber, int Width);
//static void GUIEngine_SetHScrollbarRightWidth(int PartsNumber, int Width);
//static int GUIEngine_GetHScrollbarWidth(int PartsNumber);
//static int GUIEngine_GetHScrollbarHeight(int PartsNumber);
//static int GUIEngine_GetHScrollbarLeftWidth(int PartsNumber);
//static int GUIEngine_GetHScrollbarRightWidth(int PartsNumber);
//static void GUIEngine_SetHScrollbarTotalSize(int PartsNumber, int Size);
//static void GUIEngine_SetHScrollbarViewSize(int PartsNumber, int Size);
//static void GUIEngine_SetHScrollbarScrollPos(int PartsNumber, int Pos);
//static void GUIEngine_SetHScrollbarMoveSizeByButton(int PartsNumber, int Size);
//static int GUIEngine_GetHScrollbarTotalSize(int PartsNumber);
//static int GUIEngine_GetHScrollbarViewSize(int PartsNumber);
//static int GUIEngine_GetHScrollbarScrollPos(int PartsNumber);
//static int GUIEngine_GetHScrollbarMoveSizeByButton(int PartsNumber);
//static void GUIEngine_SetHScrollbarCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetHScrollbarCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetTextBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetTextBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetTextBoxX(int PartsNumber);
//static int GUIEngine_GetTextBoxY(int PartsNumber);
//static int GUIEngine_GetTextBoxZ(int PartsNumber);
//static void GUIEngine_SetTextBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetTextBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetTextBoxWidth(int PartsNumber);
//static int GUIEngine_GetTextBoxHeight(int PartsNumber);
//static void GUIEngine_SetTextBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsTextBoxShow(int PartsNumber);
//static void GUIEngine_SetTextBoxFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetTextBoxFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetTextBoxText(int PartsNumber, string pIText);
//static void GUIEngine_GetTextBoxText(int PartsNumber, ref string pIText);
//static void GUIEngine_SetTextBoxMaxTextLength(int PartsNumber, int Length);
//static int GUIEngine_GetTextBoxMaxTextLength(int PartsNumber);
//static void GUIEngine_SetTextBoxSelectR(int PartsNumber, int Red);
//static void GUIEngine_SetTextBoxSelectG(int PartsNumber, int Green);
//static void GUIEngine_SetTextBoxSelectB(int PartsNumber, int Blue);
//static int GUIEngine_GetTextBoxSelectR(int PartsNumber);
//static int GUIEngine_GetTextBoxSelectG(int PartsNumber);
//static int GUIEngine_GetTextBoxSelectB(int PartsNumber);
//static void GUIEngine_SetTextBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetTextBoxCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetListBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetListBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetListBoxX(int PartsNumber);
//static int GUIEngine_GetListBoxY(int PartsNumber);
//static int GUIEngine_GetListBoxZ(int PartsNumber);
//static void GUIEngine_SetListBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetListBoxLineHeight(int PartsNumber, int Height);
//static void GUIEngine_SetListBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetListBoxWidth(int PartsNumber);
//static int GUIEngine_GetListBoxLineHeight(int PartsNumber);
//static int GUIEngine_GetListBoxHeight(int PartsNumber);
//static void GUIEngine_SetListBoxWidthMargin(int PartsNumber, int Margin);
//static void GUIEngine_SetListBoxHeightMargin(int PartsNumber, int Margin);
//static int GUIEngine_GetListBoxWidthMargin(int PartsNumber);
//static int GUIEngine_GetListBoxHeightMargin(int PartsNumber);
//static void GUIEngine_SetListBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsListBoxShow(int PartsNumber);
//static void GUIEngine_SetListBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetListBoxCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetListBoxScrollPos(int PartsNumber, int Pos);
//static int GUIEngine_GetListBoxScrollPos(int PartsNumber);
//static void GUIEngine_AddListBoxItem(int PartsNumber, string pIItem);
//static void GUIEngine_SetListBoxItem(int PartsNumber, int Index, string pIItem);
//static int GUIEngine_GetListBoxItemCount(int PartsNumber);
//static void GUIEngine_GetListBoxItem(int PartsNumber, int Index, ref string pIItem);
//static void GUIEngine_EraseListBoxItem(int PartsNumber, int Index);
//static void GUIEngine_ClearListBoxItem(int PartsNumber);
//static void GUIEngine_SetListBoxFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetListBoxFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetListBoxSelectIndex(int PartsNumber, int Index);
//static int GUIEngine_GetListBoxSelectIndex(int PartsNumber);
//static void GUIEngine_SetComboBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetComboBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetComboBoxX(int PartsNumber);
//static int GUIEngine_GetComboBoxY(int PartsNumber);
//static int GUIEngine_GetComboBoxZ(int PartsNumber);
//static void GUIEngine_SetComboBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetComboBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetComboBoxWidth(int PartsNumber);
//static int GUIEngine_GetComboBoxHeight(int PartsNumber);
//static void GUIEngine_SetComboBoxTextWidthMargin(int PartsNumber, int Margin);
//static void GUIEngine_SetComboBoxTextHeightMargin(int PartsNumber, int Margin);
//static int GUIEngine_GetComboBoxTextWidthMargin(int PartsNumber);
//static int GUIEngine_GetComboBoxTextHeightMargin(int PartsNumber);
//static void GUIEngine_SetComboBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsComboBoxShow(int PartsNumber);
//static void GUIEngine_SetComboBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetComboBoxCGName(int PartsNumber, ref string pIName);
//static void GUIEngine_SetComboBoxText(int PartsNumber, string pIItem);
//static void GUIEngine_GetComboBoxText(int PartsNumber, ref string pIItem);
//static void GUIEngine_SetComboBoxFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetComboBoxFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetMultiTextBoxPos(int PartsNumber, int PosX, int PosY);
//static void GUIEngine_SetMultiTextBoxZ(int PartsNumber, int PosZ);
//static int GUIEngine_GetMultiTextBoxX(int PartsNumber);
//static int GUIEngine_GetMultiTextBoxY(int PartsNumber);
//static int GUIEngine_GetMultiTextBoxZ(int PartsNumber);
//static void GUIEngine_SetMultiTextBoxWidth(int PartsNumber, int Width);
//static void GUIEngine_SetMultiTextBoxHeight(int PartsNumber, int Height);
//static int GUIEngine_GetMultiTextBoxWidth(int PartsNumber);
//static int GUIEngine_GetMultiTextBoxHeight(int PartsNumber);
//static void GUIEngine_SetMultiTextBoxShow(int PartsNumber, bool Show);
//static bool GUIEngine_IsMultiTextBoxShow(int PartsNumber);
//static void GUIEngine_SetMultiTextBoxFontProperty(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight);
//static void GUIEngine_GetMultiTextBoxFontProperty(int PartsNumber, ref int Type, ref int Size, ref int R, ref int G, ref int B, ref float BoldWeight, ref int EdgeR, ref int EdgeG, ref int EdgeB, ref float EdgeWeight);
//static void GUIEngine_SetMultiTextBoxText(int PartsNumber, string Text);
//static void GUIEngine_GetMultiTextBoxText(int PartsNumber, ref string Text);
//static void GUIEngine_SetMultiTextBoxMaxTextLength(int PartsNumber, int Length);
//static int GUIEngine_GetMultiTextBoxMaxTextLength(int PartsNumber);
//static void GUIEngine_SetMultiTextBoxSelectR(int PartsNumber, int Red);
//static void GUIEngine_SetMultiTextBoxSelectG(int PartsNumber, int Green);
//static void GUIEngine_SetMultiTextBoxSelectB(int PartsNumber, int Blue);
//static int GUIEngine_GetMultiTextBoxSelectR(int PartsNumber);
//static int GUIEngine_GetMultiTextBoxSelectG(int PartsNumber);
//static int GUIEngine_GetMultiTextBoxSelectB(int PartsNumber);
//static void GUIEngine_SetMultiTextBoxCGName(int PartsNumber, string pIName);
//static void GUIEngine_GetMultiTextBoxCGName(int PartsNumber, ref string pIName);
//static bool GUIEngine_Parts_SetPartsCG(int PartsNumber, string pCGName, int SpriteDeform, int State);
//static void GUIEngine_Parts_GetPartsCGName(int PartsNumber, ref string pCGName, int State);
//static bool GUIEngine_Parts_SetPartsCGSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetLoopCG(int PartsNumber, string pCGName, int StartNo, int NumofCG, int TimePerCG, int State);
//static bool GUIEngine_Parts_SetLoopCGSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetText(int PartsNumber, string pIText, int State);
//static bool GUIEngine_Parts_AddPartsText(int PartsNumber, string pIText, int State);
//static bool GUIEngine_Parts_DeletePartsTopTextLine(int PartsNumber, int State);
//static bool GUIEngine_Parts_SetPartsTextSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static void GUIEngine_Parts_SetPartsTextHighlight(int PartsNumber, int Start, int Length, int nR, int nG, int nB, int State);
//static void GUIEngine_Parts_AddPartsTextHighlight(int PartsNumber, int Start, int Length, int nR, int nG, int nB, int State);
//static void GUIEngine_Parts_ClearPartsTextHighlight(int PartsNumber, int State);
//static void GUIEngine_Parts_SetPartsTextCountReturn(int PartsNumber, bool Count, int State);
//static bool GUIEngine_Parts_GetPartsTextCountReturn(int PartsNumber, int State);
//static bool GUIEngine_Parts_SetFont(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight, int State);
//static bool GUIEngine_Parts_SetPartsFontType(int PartsNumber, int Type, int State);
//static bool GUIEngine_Parts_SetPartsFontSize(int PartsNumber, int Size, int State);
//static bool GUIEngine_Parts_SetPartsFontColor(int PartsNumber, int R, int G, int B, int State);
//static bool GUIEngine_Parts_SetPartsFontBoldWeight(int PartsNumber, float BoldWeight, int State);
//static bool GUIEngine_Parts_SetPartsFontEdgeColor(int PartsNumber, int R, int G, int B, int State);
//static bool GUIEngine_Parts_SetPartsFontEdgeWeight(int PartsNumber, float EdgeWeight, int State);
//static bool GUIEngine_Parts_SetTextCharSpace(int PartsNumber, int CharSpace, int State);
//static bool GUIEngine_Parts_SetTextLineSpace(int PartsNumber, int LineSpace, int State);
//static bool GUIEngine_Parts_SetHGaugeCG(int PartsNumber, string pCGName, int State);
//static bool GUIEngine_Parts_SetHGaugeRate(int PartsNumber, float Numerator, float Denominator, int State);
//static bool GUIEngine_Parts_SetVGaugeCG(int PartsNumber, string pCGName, int State);
//static bool GUIEngine_Parts_SetVGaugeRate(int PartsNumber, float Numerator, float Denominator, int State);
//static bool GUIEngine_Parts_SetHGaugeSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetVGaugeSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetNumeralCG(int PartsNumber, string pBaseCGName, int State);
//static bool GUIEngine_Parts_SetNumeralLinkedCGNumberWidthWidthList(int PartsNumber, string pCGName, int Width0, int Width1, int Width2, int Width3, int Width4, int Width5, int Width6, int Width7, int Width8, int Width9, int WidthMinus, int WidthComma, int State);
//static bool GUIEngine_Parts_SetNumeralFont(int PartsNumber, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight, bool FullPitch, int State);
//static bool GUIEngine_Parts_SetNumeralNumber(int PartsNumber, int Number, int State);
//static bool GUIEngine_Parts_SetNumeralShowComma(int PartsNumber, bool ShowComma, int State);
//static bool GUIEngine_Parts_SetNumeralSpace(int PartsNumber, int NumeralSpace, int State);
//static bool GUIEngine_Parts_SetNumeralLength(int PartsNumber, int Length, int State);
//static bool GUIEngine_Parts_SetNumeralSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetPartsRectangleDetectionSize(int PartsNumber, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetPartsRectangleDetectionSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetPartsCGDetectionSize(int PartsNumber, string pCGName, int State);
//static bool GUIEngine_Parts_SetPartsCGDetectionSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetPartsFlat(int PartsNumber, string pIFileName, int State);
//static bool GUIEngine_Parts_IsPartsFlatEnd(int PartsNumber, int State);
//static int GUIEngine_Parts_GetPartsFlatCurrentFrameNumber(int PartsNumber, int State);
//static bool GUIEngine_Parts_BackPartsFlatBeginFrame(int PartsNumber, int State);
//static bool GUIEngine_Parts_StepPartsFlatFinalFrame(int PartsNumber, int State);
//static bool GUIEngine_Parts_SetPartsFlatSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_SetPartsFlatAndStop(int PartsNumber, string pIFileName, int State);
//static bool GUIEngine_Parts_StopPartsFlat(int PartsNumber, int State);
//static bool GUIEngine_Parts_StartPartsFlat(int PartsNumber, int State);
//static bool GUIEngine_Parts_GoFramePartsFlat(int PartsNumber, int CurrentFrame, int State);
//static int GUIEngine_Parts_GetPartsFlatEndFrame(int PartsNumber, int State);
//static bool GUIEngine_Parts_ExistsFlatFile(string pIFileName);
//static bool GUIEngine_Parts_ClearPartsConstructionProcess(int PartsNumber, int State);
//static bool GUIEngine_Parts_AddCreateToPartsConstructionProcess(int PartsNumber, int nWidth, int nHeight, int State);
//static bool GUIEngine_Parts_AddCreatePixelOnlyToPartsConstructionProcess(int PartsNumber, int nWidth, int nHeight, int State);
//static bool GUIEngine_Parts_AddCreateCGToProcess(int PartsNumber, string pICGName, int State);
//static bool GUIEngine_Parts_AddFillToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, int State);
//static bool GUIEngine_Parts_AddFillAlphaColorToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, int nA, int State);
//static bool GUIEngine_Parts_AddFillAMapToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nAlpha, int State);
//static bool GUIEngine_Parts_AddFillWithAlphaToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, int nA, int State);
//static bool GUIEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nTopR, int nTopG, int nTopB, int nBottomR, int nBottomG, int nBottomB, int State);
//static bool GUIEngine_Parts_AddDrawRectToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, int State);
//static bool GUIEngine_Parts_AddDrawCutCGToPartsConstructionProcess(int PartsNumber, string pICGName, int DestX, int DestY, int DestWidth, int DestHeight, int SrcX, int SrcY, int SrcWidth, int SrcHeight, int InterpolationType, int State);
//static bool GUIEngine_Parts_AddCopyCutCGToPartsConstructionProcess(int PartsNumber, string pICGName, int DestX, int DestY, int DestWidth, int DestHeight, int SrcX, int SrcY, int SrcWidth, int SrcHeight, int InterpolationType, int State);
//static bool GUIEngine_Parts_AddGrayFilterToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, bool FullSize, int State);
//static bool GUIEngine_Parts_AddAddFilterToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, bool FullSize, int State);
//static bool GUIEngine_Parts_AddMulFilterToPartsConstructionProcess(int PartsNumber, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, bool FullSize, int State);
//static bool GUIEngine_Parts_AddDrawLineToPartsConstructionProcess(int PartsNumber, int nX1, int nY1, int nX2, int nY2, int nR, int nG, int nB, int nA, int State);
//static bool GUIEngine_Parts_BuildPartsConstructionProcess(int PartsNumber, int State);
//static bool GUIEngine_Parts_AddDrawTextToPartsConstructionProcess(int PartsNumber, int nX, int nY, string pIText, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight, int CharSpace, int LineSpace, int State);
//static bool GUIEngine_Parts_AddCopyTextToPartsConstructionProcess(int PartsNumber, int nX, int nY, string pIText, int Type, int Size, int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB, float EdgeWeight, int CharSpace, int LineSpace, int State);
//static bool GUIEngine_Parts_SetPartsConstructionSurfaceArea(int PartsNumber, int X, int Y, int Width, int Height, int State);
//static bool GUIEngine_Parts_CreateParts3DLayerPluginID(int PartsNumber, int State);
//static int GUIEngine_Parts_GetParts3DLayerPluginID(int PartsNumber, int State);
//static bool GUIEngine_Parts_ReleaseParts3DLayerPluginID(int PartsNumber, int State);
//static void GUIEngine_Parts_SetPos(int PartsNumber, int X, int Y);
//static void GUIEngine_Parts_SetZ(int PartsNumber, int Z);
//static void GUIEngine_Parts_SetShow(int PartsNumber, bool Show);
//static void GUIEngine_Parts_SetAlpha(int PartsNumber, int Alpha);
//static void GUIEngine_Parts_SetPartsDrawFilter(int PartsNumber, int DrawFilter);
//static void GUIEngine_Parts_SetPassCursor(int PartsNumber, bool Pass);
//static void GUIEngine_Parts_SetClickable(int PartsNumber, bool Clickable);
//static void GUIEngine_Parts_SetSpeedupRateByMessageSkip(int PartsNumber, int Rate);
//static void GUIEngine_Parts_SetResetTimerByChangeInputStatus(int PartsNumber, bool Reset, int State);
//static void GUIEngine_Parts_SetAddColor(int PartsNumber, int nR, int nG, int nB);
//static void GUIEngine_Parts_SetMultiplyColor(int PartsNumber, int nR, int nG, int nB);
//static void GUIEngine_Parts_SetDrag(int PartsNumber, bool Drag);
//static void GUIEngine_Parts_SetPartsOriginPosMode(int PartsNumber, int OriginPosMode);
//static void GUIEngine_Parts_SetParentPartsNumber(int PartsNumber, int ParentPartsNumber);
//static void GUIEngine_Parts_SetInputState(int PartsNumber, int State);
//static void GUIEngine_Parts_SetOnCursorShowLinkPartsNumber(int PartsNumber, int LinkPartsNumber);
//static void GUIEngine_Parts_SetPartsMessageWindowShowLink(int PartsNumber, bool MessageWindowShowLink);
//static void GUIEngine_Parts_SetSoundNumber(int PartsNumber, int SoundNumber, int State);

static void GUIEngine_Parts_SetClickMissSoundNumber(int sound_number)
{
	PE_SetClickMissSoundNumber(sound_number);
}

//static void GUIEngine_Parts_SetPartsMagX(int PartsNumber, float MagX);
//static void GUIEngine_Parts_SetPartsMagY(int PartsNumber, float MagY);
//static void GUIEngine_Parts_SetPartsRotateX(int PartsNumber, float RotateX);
//static void GUIEngine_Parts_SetPartsRotateY(int PartsNumber, float RotateY);
//static void GUIEngine_Parts_SetPartsRotateZ(int PartsNumber, float RotateZ);
//static void GUIEngine_Parts_SetPartsAlphaClipperPartsNumber(int PartsNumber, int AlphaClipperPartsNumber);
//static void GUIEngine_Parts_SetPartsPixelDecide(int PartsNumber, bool PixelDecide);
//static int GUIEngine_Parts_GetPartsX(int PartsNumber);
//static int GUIEngine_Parts_GetPartsY(int PartsNumber);
//static int GUIEngine_Parts_GetPartsZ(int PartsNumber);
//static bool GUIEngine_Parts_GetPartsShow(int PartsNumber);
//static int GUIEngine_Parts_GetPartsAlpha(int PartsNumber);
//static int GUIEngine_Parts_GetPartsDrawFilter(int PartsNumber);
//static bool GUIEngine_Parts_GetPartsPassCursor(int PartsNumber);
//static bool GUIEngine_Parts_GetPartsClickable(int PartsNumber);
//static int GUIEngine_Parts_GetPartsSpeedupRateByMessageSkip(int PartsNumber);
//static bool GUIEngine_Parts_GetResetTimerByChangeInputStatus(int PartsNumber, int State);
//static void GUIEngine_Parts_GetAddColor(int PartsNumber, ref int nR, ref int nG, ref int nB);
//static void GUIEngine_Parts_GetMultiplyColor(int PartsNumber, ref int nR, ref int nG, ref int nB);
//static bool GUIEngine_Parts_GetPartsDrag(int PartsNumber);
//static int GUIEngine_Parts_GetPartsOriginPosMode(int PartsNumber);
//static int GUIEngine_Parts_GetParentPartsNumber(int PartsNumber);
//static int GUIEngine_Parts_GetInputState(int PartsNumber);
//static int GUIEngine_Parts_GetOnCursorShowLinkPartsNumber(int PartsNumber);
//static bool GUIEngine_Parts_GetPartsMessageWindowShowLink(int PartsNumber);
//static int GUIEngine_Parts_GetSoundNumber(int PartsNumber, int State);
//static int GUIEngine_Parts_GetClickMissSoundNumber(void);
//static float GUIEngine_Parts_GetPartsMagX(int PartsNumber);
//static float GUIEngine_Parts_GetPartsMagY(int PartsNumber);
//static float GUIEngine_Parts_GetPartsRotateX(int PartsNumber);
//static float GUIEngine_Parts_GetPartsRotateY(int PartsNumber);
//static float GUIEngine_Parts_GetPartsRotateZ(int PartsNumber);
//static int GUIEngine_Parts_GetPartsAlphaClipperPartsNumber(int PartsNumber);
//static bool GUIEngine_Parts_IsPartsPixelDecide(int PartsNumber);
//static int GUIEngine_Parts_GetPartsUpperLeftPosX(int PartsNumber, int State);
//static int GUIEngine_Parts_GetPartsUpperLeftPosY(int PartsNumber, int State);
//static int GUIEngine_Parts_GetPartsWidth(int PartsNumber, int State);
//static int GUIEngine_Parts_GetPartsHeight(int PartsNumber, int State);
//static void GUIEngine_Parts_AddMotionPos(int PartsNumber, int BeginX, int BeginY, int EndX, int EndY, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionAlpha(int PartsNumber, int BeginAlpha, int EndAlpha, int BeginTime, int EndTime, string pICurveName);
//static bool GUIEngine_Parts_AddMotionCG(int PartsNumber, string BaseCGName, int BeginCGNumber, int NumofCG, int BeginTime, int EndTime, string pICurveName);
//static bool GUIEngine_Parts_AddMotionCGTermination(int PartsNumber, int EndTime);
//static void GUIEngine_Parts_AddMotionHGaugeRate(int PartsNumber, float BeginNumerator, float BeginDenominator, float EndNumerator, float EndDenominator, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionVGaugeRate(int PartsNumber, float BeginNumerator, float BeginDenominator, float EndNumerator, float EndDenominator, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionNumeralNumber(int PartsNumber, int BeginNumber, int EndNumber, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionMagX(int PartsNumber, float BeginMagX, float EndMagX, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionMagY(int PartsNumber, float BeginMagY, float EndMagY, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionRotateX(int PartsNumber, float BeginRotateX, float EndRotateX, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionRotateY(int PartsNumber, float BeginRotateY, float EndRotateY, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionRotateZ(int PartsNumber, float BeginRotateZ, float EndRotateZ, int BeginTime, int EndTime, string pICurveName);
//static void GUIEngine_Parts_AddMotionVibrationSize(int PartsNumber, int BeginWidth, int BeginHeight, int BeginTime, int EndTime);
//static void GUIEngine_Parts_AddWholeMotionVibrationSize(int BeginWidth, int BeginHeight, int BeginTime, int EndTime);
//static void GUIEngine_Parts_AddMotionSound(int SoundNumber, int BeginTime);
//static bool GUIEngine_Parts_IsCursorIn(int PartsNumber, int MouseX, int MouseY, int State);
//static bool GUIEngine_Parts_SetThumbnailReductionSize(int ReductionSize);
//static bool GUIEngine_Parts_SetThumbnailMode(bool Mode);
//static int GUIEngine_GetClickNumber(void);
//static void GUIEngine_Parts_BeginMotion(void);
//static void GUIEngine_Parts_EndMotion(void);
//static bool GUIEngine_Parts_IsMotion(void);
//static void GUIEngine_Parts_SeekEndMotion(void);
//static void GUIEngine_Parts_UpdateMotionTime(int Time, bool Skip);
//static bool GUIEngine_Save(ref array<int> SaveDataBuffer);
//static bool GUIEngine_SaveWithoutHideParts(ref array<int> SaveDataBuffer);
//static bool GUIEngine_Load(ref array<int> SaveDataBuffer);

HLL_LIBRARY(GUIEngine,
	    HLL_EXPORT(_PreLink, GUIEngine_PreLink),
	    HLL_EXPORT(_ModuleInit, GUIEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, GUIEngine_ModuleFini),
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAll, GUIEngine_ReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, GUIEngine_ReleaseAllWithoutSystem),
	    HLL_EXPORT(SetDelegateIndex, PE_SetDelegateIndex),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_TODO_EXPORT(PushController, GUIEngine_PushController),
	    HLL_TODO_EXPORT(PopController, GUIEngine_PopController),
	    HLL_EXPORT(UpdateComponent, PE_UpdateComponent),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(UpdateInputState, PE_UpdateInputState),
	    HLL_EXPORT(UpdateParts, PE_UpdateParts),
	    HLL_TODO_EXPORT(SetFocus, GUIEngine_SetFocus),
	    HLL_TODO_EXPORT(IsFocus, GUIEngine_IsFocus),
	    HLL_EXPORT(StopSoundWithoutSystemSound, GUIEngine_StopSoundWithoutSystemSound),
	    HLL_EXPORT(ReleaseMessage, GUIEngine_ReleaseMessage),
	    HLL_TODO_EXPORT(PopMessage, GUIEngine_PopMessage),
	    HLL_TODO_EXPORT(GetMessagePartsNumber, GUIEngine_GetMessagePartsNumber),
	    HLL_TODO_EXPORT(GetMessageDelegateIndex, GUIEngine_GetMessageDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PE_GetDelegateIndex),
	    HLL_EXPORT(GetMessageType, GUIEngine_GetMessageType),
	    HLL_TODO_EXPORT(GetMessageVariableCount, GUIEngine_GetMessageVariableCount),
	    HLL_TODO_EXPORT(GetMessageVariableType, GUIEngine_GetMessageVariableType),
	    HLL_TODO_EXPORT(GetMessageVariableInt, GUIEngine_GetMessageVariableInt),
	    HLL_TODO_EXPORT(GetMessageVariableFloat, GUIEngine_GetMessageVariableFloat),
	    HLL_TODO_EXPORT(GetMessageVariableBool, GUIEngine_GetMessageVariableBool),
	    HLL_TODO_EXPORT(GetMessageVariableString, GUIEngine_GetMessageVariableString),
	    HLL_TODO_EXPORT(SetButtonPos, GUIEngine_SetButtonPos),
	    HLL_TODO_EXPORT(SetButtonZ, GUIEngine_SetButtonZ),
	    HLL_TODO_EXPORT(GetButtonX, GUIEngine_GetButtonX),
	    HLL_TODO_EXPORT(GetButtonY, GUIEngine_GetButtonY),
	    HLL_TODO_EXPORT(GetButtonZ, GUIEngine_GetButtonZ),
	    HLL_TODO_EXPORT(SetButtonWidth, GUIEngine_SetButtonWidth),
	    HLL_TODO_EXPORT(SetButtonHeight, GUIEngine_SetButtonHeight),
	    HLL_TODO_EXPORT(GetButtonWidth, GUIEngine_GetButtonWidth),
	    HLL_TODO_EXPORT(GetButtonHeight, GUIEngine_GetButtonHeight),
	    HLL_TODO_EXPORT(SetButtonShow, GUIEngine_SetButtonShow),
	    HLL_TODO_EXPORT(IsButtonShow, GUIEngine_IsButtonShow),
	    HLL_TODO_EXPORT(SetButtonDrag, GUIEngine_SetButtonDrag),
	    HLL_TODO_EXPORT(IsButtonDrag, GUIEngine_IsButtonDrag),
	    HLL_TODO_EXPORT(SetButtonEnable, GUIEngine_SetButtonEnable),
	    HLL_TODO_EXPORT(IsButtonEnable, GUIEngine_IsButtonEnable),
	    HLL_TODO_EXPORT(SetButtonPixelDecide, GUIEngine_SetButtonPixelDecide),
	    HLL_TODO_EXPORT(IsButtonPixelDecide, GUIEngine_IsButtonPixelDecide),
	    HLL_TODO_EXPORT(SetButtonR, GUIEngine_SetButtonR),
	    HLL_TODO_EXPORT(SetButtonG, GUIEngine_SetButtonG),
	    HLL_TODO_EXPORT(SetButtonB, GUIEngine_SetButtonB),
	    HLL_TODO_EXPORT(GetButtonR, GUIEngine_GetButtonR),
	    HLL_TODO_EXPORT(GetButtonG, GUIEngine_GetButtonG),
	    HLL_TODO_EXPORT(GetButtonB, GUIEngine_GetButtonB),
	    HLL_TODO_EXPORT(SetButtonFontProperty, GUIEngine_SetButtonFontProperty),
	    HLL_TODO_EXPORT(GetButtonFontProperty, GUIEngine_GetButtonFontProperty),
	    HLL_TODO_EXPORT(SetButtonOnCursorSoundNumber, GUIEngine_SetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetButtonClickSoundNumber, GUIEngine_SetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(GetButtonOnCursorSoundNumber, GUIEngine_GetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetButtonClickSoundNumber, GUIEngine_GetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(SetButtonCGName, GUIEngine_SetButtonCGName),
	    HLL_TODO_EXPORT(GetButtonCGName, GUIEngine_GetButtonCGName),
	    HLL_TODO_EXPORT(SetButtonText, GUIEngine_SetButtonText),
	    HLL_TODO_EXPORT(GetButtonText, GUIEngine_GetButtonText),
	    HLL_TODO_EXPORT(SetCGBoxPos, GUIEngine_SetCGBoxPos),
	    HLL_TODO_EXPORT(SetCGBoxZ, GUIEngine_SetCGBoxZ),
	    HLL_TODO_EXPORT(GetCGBoxX, GUIEngine_GetCGBoxX),
	    HLL_TODO_EXPORT(GetCGBoxY, GUIEngine_GetCGBoxY),
	    HLL_TODO_EXPORT(GetCGBoxZ, GUIEngine_GetCGBoxZ),
	    HLL_TODO_EXPORT(SetCGBoxWidth, GUIEngine_SetCGBoxWidth),
	    HLL_TODO_EXPORT(SetCGBoxHeight, GUIEngine_SetCGBoxHeight),
	    HLL_TODO_EXPORT(GetCGBoxWidth, GUIEngine_GetCGBoxWidth),
	    HLL_TODO_EXPORT(GetCGBoxHeight, GUIEngine_GetCGBoxHeight),
	    HLL_TODO_EXPORT(SetCGBoxAlpha, GUIEngine_SetCGBoxAlpha),
	    HLL_TODO_EXPORT(GetCGBoxAlpha, GUIEngine_GetCGBoxAlpha),
	    HLL_TODO_EXPORT(SetCGBoxViewMode, GUIEngine_SetCGBoxViewMode),
	    HLL_TODO_EXPORT(GetCGBoxViewMode, GUIEngine_GetCGBoxViewMode),
	    HLL_TODO_EXPORT(SetCGBoxShow, GUIEngine_SetCGBoxShow),
	    HLL_TODO_EXPORT(IsCGBoxShow, GUIEngine_IsCGBoxShow),
	    HLL_TODO_EXPORT(SetCGBoxDrag, GUIEngine_SetCGBoxDrag),
	    HLL_TODO_EXPORT(IsCGBoxDrag, GUIEngine_IsCGBoxDrag),
	    HLL_TODO_EXPORT(SetCGBoxCGName, GUIEngine_SetCGBoxCGName),
	    HLL_TODO_EXPORT(GetCGBoxCGName, GUIEngine_GetCGBoxCGName),
	    HLL_TODO_EXPORT(SetCGBoxAddR, GUIEngine_SetCGBoxAddR),
	    HLL_TODO_EXPORT(SetCGBoxAddG, GUIEngine_SetCGBoxAddG),
	    HLL_TODO_EXPORT(SetCGBoxAddB, GUIEngine_SetCGBoxAddB),
	    HLL_TODO_EXPORT(GetCGBoxAddR, GUIEngine_GetCGBoxAddR),
	    HLL_TODO_EXPORT(GetCGBoxAddG, GUIEngine_GetCGBoxAddG),
	    HLL_TODO_EXPORT(GetCGBoxAddB, GUIEngine_GetCGBoxAddB),
	    HLL_TODO_EXPORT(SetCGBoxMulR, GUIEngine_SetCGBoxMulR),
	    HLL_TODO_EXPORT(SetCGBoxMulG, GUIEngine_SetCGBoxMulG),
	    HLL_TODO_EXPORT(SetCGBoxMulB, GUIEngine_SetCGBoxMulB),
	    HLL_TODO_EXPORT(GetCGBoxMulR, GUIEngine_GetCGBoxMulR),
	    HLL_TODO_EXPORT(GetCGBoxMulG, GUIEngine_GetCGBoxMulG),
	    HLL_TODO_EXPORT(GetCGBoxMulB, GUIEngine_GetCGBoxMulB),
	    HLL_TODO_EXPORT(SetCheckBoxPos, GUIEngine_SetCheckBoxPos),
	    HLL_TODO_EXPORT(SetCheckBoxZ, GUIEngine_SetCheckBoxZ),
	    HLL_TODO_EXPORT(GetCheckBoxX, GUIEngine_GetCheckBoxX),
	    HLL_TODO_EXPORT(GetCheckBoxY, GUIEngine_GetCheckBoxY),
	    HLL_TODO_EXPORT(GetCheckBoxZ, GUIEngine_GetCheckBoxZ),
	    HLL_TODO_EXPORT(SetCheckBoxWidth, GUIEngine_SetCheckBoxWidth),
	    HLL_TODO_EXPORT(SetCheckBoxHeight, GUIEngine_SetCheckBoxHeight),
	    HLL_TODO_EXPORT(GetCheckBoxWidth, GUIEngine_GetCheckBoxWidth),
	    HLL_TODO_EXPORT(GetCheckBoxHeight, GUIEngine_GetCheckBoxHeight),
	    HLL_TODO_EXPORT(SetCheckBoxShow, GUIEngine_SetCheckBoxShow),
	    HLL_TODO_EXPORT(IsCheckBoxShow, GUIEngine_IsCheckBoxShow),
	    HLL_TODO_EXPORT(SetCheckBoxDrag, GUIEngine_SetCheckBoxDrag),
	    HLL_TODO_EXPORT(IsCheckBoxDrag, GUIEngine_IsCheckBoxDrag),
	    HLL_TODO_EXPORT(CheckBoxChecked, GUIEngine_CheckBoxChecked),
	    HLL_TODO_EXPORT(IsCheckBoxChecked, GUIEngine_IsCheckBoxChecked),
	    HLL_TODO_EXPORT(SetCheckBoxR, GUIEngine_SetCheckBoxR),
	    HLL_TODO_EXPORT(SetCheckBoxG, GUIEngine_SetCheckBoxG),
	    HLL_TODO_EXPORT(SetCheckBoxB, GUIEngine_SetCheckBoxB),
	    HLL_TODO_EXPORT(GetCheckBoxR, GUIEngine_GetCheckBoxR),
	    HLL_TODO_EXPORT(GetCheckBoxG, GUIEngine_GetCheckBoxG),
	    HLL_TODO_EXPORT(GetCheckBoxB, GUIEngine_GetCheckBoxB),
	    HLL_TODO_EXPORT(SetCheckBoxFontProperty, GUIEngine_SetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(GetCheckBoxFontProperty, GUIEngine_GetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(SetCheckBoxOnCursorSoundNumber, GUIEngine_SetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxClickSoundNumber, GUIEngine_SetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxOnCursorSoundNumber, GUIEngine_GetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxClickSoundNumber, GUIEngine_GetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxCGName, GUIEngine_SetCheckBoxCGName),
	    HLL_TODO_EXPORT(GetCheckBoxCGName, GUIEngine_GetCheckBoxCGName),
	    HLL_TODO_EXPORT(SetCheckBoxText, GUIEngine_SetCheckBoxText),
	    HLL_TODO_EXPORT(GetCheckBoxText, GUIEngine_GetCheckBoxText),
	    HLL_TODO_EXPORT(SetLabelPos, GUIEngine_SetLabelPos),
	    HLL_TODO_EXPORT(SetLabelZ, GUIEngine_SetLabelZ),
	    HLL_TODO_EXPORT(GetLabelX, GUIEngine_GetLabelX),
	    HLL_TODO_EXPORT(GetLabelY, GUIEngine_GetLabelY),
	    HLL_TODO_EXPORT(GetLabelZ, GUIEngine_GetLabelZ),
	    HLL_TODO_EXPORT(SetLabelWidth, GUIEngine_SetLabelWidth),
	    HLL_TODO_EXPORT(SetLabelHeight, GUIEngine_SetLabelHeight),
	    HLL_TODO_EXPORT(GetLabelWidth, GUIEngine_GetLabelWidth),
	    HLL_TODO_EXPORT(GetLabelHeight, GUIEngine_GetLabelHeight),
	    HLL_TODO_EXPORT(SetLabelAlpha, GUIEngine_SetLabelAlpha),
	    HLL_TODO_EXPORT(GetLabelAlpha, GUIEngine_GetLabelAlpha),
	    HLL_TODO_EXPORT(SetLabelShow, GUIEngine_SetLabelShow),
	    HLL_TODO_EXPORT(IsLabelShow, GUIEngine_IsLabelShow),
	    HLL_TODO_EXPORT(SetLabelDrag, GUIEngine_SetLabelDrag),
	    HLL_TODO_EXPORT(IsLabelDrag, GUIEngine_IsLabelDrag),
	    HLL_TODO_EXPORT(SetLabelClip, GUIEngine_SetLabelClip),
	    HLL_TODO_EXPORT(IsLabelClip, GUIEngine_IsLabelClip),
	    HLL_TODO_EXPORT(SetLabelText, GUIEngine_SetLabelText),
	    HLL_TODO_EXPORT(GetLabelText, GUIEngine_GetLabelText),
	    HLL_TODO_EXPORT(SetLabelFontProperty, GUIEngine_SetLabelFontProperty),
	    HLL_TODO_EXPORT(GetLabelFontProperty, GUIEngine_GetLabelFontProperty),
	    HLL_TODO_EXPORT(SetVScrollbarPos, GUIEngine_SetVScrollbarPos),
	    HLL_TODO_EXPORT(SetVScrollbarZ, GUIEngine_SetVScrollbarZ),
	    HLL_TODO_EXPORT(GetVScrollbarX, GUIEngine_GetVScrollbarX),
	    HLL_TODO_EXPORT(GetVScrollbarY, GUIEngine_GetVScrollbarY),
	    HLL_TODO_EXPORT(GetVScrollbarZ, GUIEngine_GetVScrollbarZ),
	    HLL_TODO_EXPORT(SetVScrollbarShow, GUIEngine_SetVScrollbarShow),
	    HLL_TODO_EXPORT(IsVScrollbarShow, GUIEngine_IsVScrollbarShow),
	    HLL_TODO_EXPORT(SetVScrollbarOnCursorSoundNumber, GUIEngine_SetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarClickSoundNumber, GUIEngine_SetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarOnCursorSoundNumber, GUIEngine_GetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarClickSoundNumber, GUIEngine_GetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarWidth, GUIEngine_SetVScrollbarWidth),
	    HLL_TODO_EXPORT(SetVScrollbarHeight, GUIEngine_SetVScrollbarHeight),
	    HLL_TODO_EXPORT(SetVScrollbarUpHeight, GUIEngine_SetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(SetVScrollbarDownHeight, GUIEngine_SetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(GetVScrollbarWidth, GUIEngine_GetVScrollbarWidth),
	    HLL_TODO_EXPORT(GetVScrollbarHeight, GUIEngine_GetVScrollbarHeight),
	    HLL_TODO_EXPORT(GetVScrollbarUpHeight, GUIEngine_GetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(GetVScrollbarDownHeight, GUIEngine_GetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(SetVScrollbarTotalSize, GUIEngine_SetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetVScrollbarViewSize, GUIEngine_SetVScrollbarViewSize),
	    HLL_TODO_EXPORT(SetVScrollbarScrollPos, GUIEngine_SetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetVScrollbarMoveSizeByButton, GUIEngine_SetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetVScrollbarTotalSize, GUIEngine_GetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetVScrollbarViewSize, GUIEngine_GetVScrollbarViewSize),
	    HLL_TODO_EXPORT(GetVScrollbarScrollPos, GUIEngine_GetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetVScrollbarMoveSizeByButton, GUIEngine_GetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetVScrollbarCGName, GUIEngine_SetVScrollbarCGName),
	    HLL_TODO_EXPORT(GetVScrollbarCGName, GUIEngine_GetVScrollbarCGName),
	    HLL_TODO_EXPORT(SetHScrollbarPos, GUIEngine_SetHScrollbarPos),
	    HLL_TODO_EXPORT(SetHScrollbarZ, GUIEngine_SetHScrollbarZ),
	    HLL_TODO_EXPORT(GetHScrollbarX, GUIEngine_GetHScrollbarX),
	    HLL_TODO_EXPORT(GetHScrollbarY, GUIEngine_GetHScrollbarY),
	    HLL_TODO_EXPORT(GetHScrollbarZ, GUIEngine_GetHScrollbarZ),
	    HLL_TODO_EXPORT(SetHScrollbarShow, GUIEngine_SetHScrollbarShow),
	    HLL_TODO_EXPORT(IsHScrollbarShow, GUIEngine_IsHScrollbarShow),
	    HLL_TODO_EXPORT(SetHScrollbarOnCursorSoundNumber, GUIEngine_SetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarClickSoundNumber, GUIEngine_SetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarOnCursorSoundNumber, GUIEngine_GetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarClickSoundNumber, GUIEngine_GetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarWidth, GUIEngine_SetHScrollbarWidth),
	    HLL_TODO_EXPORT(SetHScrollbarHeight, GUIEngine_SetHScrollbarHeight),
	    HLL_TODO_EXPORT(SetHScrollbarLeftWidth, GUIEngine_SetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(SetHScrollbarRightWidth, GUIEngine_SetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(GetHScrollbarWidth, GUIEngine_GetHScrollbarWidth),
	    HLL_TODO_EXPORT(GetHScrollbarHeight, GUIEngine_GetHScrollbarHeight),
	    HLL_TODO_EXPORT(GetHScrollbarLeftWidth, GUIEngine_GetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(GetHScrollbarRightWidth, GUIEngine_GetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(SetHScrollbarTotalSize, GUIEngine_SetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetHScrollbarViewSize, GUIEngine_SetHScrollbarViewSize),
	    HLL_TODO_EXPORT(SetHScrollbarScrollPos, GUIEngine_SetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetHScrollbarMoveSizeByButton, GUIEngine_SetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetHScrollbarTotalSize, GUIEngine_GetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetHScrollbarViewSize, GUIEngine_GetHScrollbarViewSize),
	    HLL_TODO_EXPORT(GetHScrollbarScrollPos, GUIEngine_GetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetHScrollbarMoveSizeByButton, GUIEngine_GetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetHScrollbarCGName, GUIEngine_SetHScrollbarCGName),
	    HLL_TODO_EXPORT(GetHScrollbarCGName, GUIEngine_GetHScrollbarCGName),
	    HLL_TODO_EXPORT(SetTextBoxPos, GUIEngine_SetTextBoxPos),
	    HLL_TODO_EXPORT(SetTextBoxZ, GUIEngine_SetTextBoxZ),
	    HLL_TODO_EXPORT(GetTextBoxX, GUIEngine_GetTextBoxX),
	    HLL_TODO_EXPORT(GetTextBoxY, GUIEngine_GetTextBoxY),
	    HLL_TODO_EXPORT(GetTextBoxZ, GUIEngine_GetTextBoxZ),
	    HLL_TODO_EXPORT(SetTextBoxWidth, GUIEngine_SetTextBoxWidth),
	    HLL_TODO_EXPORT(SetTextBoxHeight, GUIEngine_SetTextBoxHeight),
	    HLL_TODO_EXPORT(GetTextBoxWidth, GUIEngine_GetTextBoxWidth),
	    HLL_TODO_EXPORT(GetTextBoxHeight, GUIEngine_GetTextBoxHeight),
	    HLL_TODO_EXPORT(SetTextBoxShow, GUIEngine_SetTextBoxShow),
	    HLL_TODO_EXPORT(IsTextBoxShow, GUIEngine_IsTextBoxShow),
	    HLL_TODO_EXPORT(SetTextBoxFontProperty, GUIEngine_SetTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetTextBoxFontProperty, GUIEngine_GetTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetTextBoxText, GUIEngine_SetTextBoxText),
	    HLL_TODO_EXPORT(GetTextBoxText, GUIEngine_GetTextBoxText),
	    HLL_TODO_EXPORT(SetTextBoxMaxTextLength, GUIEngine_SetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetTextBoxMaxTextLength, GUIEngine_GetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetTextBoxSelectR, GUIEngine_SetTextBoxSelectR),
	    HLL_TODO_EXPORT(SetTextBoxSelectG, GUIEngine_SetTextBoxSelectG),
	    HLL_TODO_EXPORT(SetTextBoxSelectB, GUIEngine_SetTextBoxSelectB),
	    HLL_TODO_EXPORT(GetTextBoxSelectR, GUIEngine_GetTextBoxSelectR),
	    HLL_TODO_EXPORT(GetTextBoxSelectG, GUIEngine_GetTextBoxSelectG),
	    HLL_TODO_EXPORT(GetTextBoxSelectB, GUIEngine_GetTextBoxSelectB),
	    HLL_TODO_EXPORT(SetTextBoxCGName, GUIEngine_SetTextBoxCGName),
	    HLL_TODO_EXPORT(GetTextBoxCGName, GUIEngine_GetTextBoxCGName),
	    HLL_TODO_EXPORT(SetListBoxPos, GUIEngine_SetListBoxPos),
	    HLL_TODO_EXPORT(SetListBoxZ, GUIEngine_SetListBoxZ),
	    HLL_TODO_EXPORT(GetListBoxX, GUIEngine_GetListBoxX),
	    HLL_TODO_EXPORT(GetListBoxY, GUIEngine_GetListBoxY),
	    HLL_TODO_EXPORT(GetListBoxZ, GUIEngine_GetListBoxZ),
	    HLL_TODO_EXPORT(SetListBoxWidth, GUIEngine_SetListBoxWidth),
	    HLL_TODO_EXPORT(SetListBoxLineHeight, GUIEngine_SetListBoxLineHeight),
	    HLL_TODO_EXPORT(SetListBoxHeight, GUIEngine_SetListBoxHeight),
	    HLL_TODO_EXPORT(GetListBoxWidth, GUIEngine_GetListBoxWidth),
	    HLL_TODO_EXPORT(GetListBoxLineHeight, GUIEngine_GetListBoxLineHeight),
	    HLL_TODO_EXPORT(GetListBoxHeight, GUIEngine_GetListBoxHeight),
	    HLL_TODO_EXPORT(SetListBoxWidthMargin, GUIEngine_SetListBoxWidthMargin),
	    HLL_TODO_EXPORT(SetListBoxHeightMargin, GUIEngine_SetListBoxHeightMargin),
	    HLL_TODO_EXPORT(GetListBoxWidthMargin, GUIEngine_GetListBoxWidthMargin),
	    HLL_TODO_EXPORT(GetListBoxHeightMargin, GUIEngine_GetListBoxHeightMargin),
	    HLL_TODO_EXPORT(SetListBoxShow, GUIEngine_SetListBoxShow),
	    HLL_TODO_EXPORT(IsListBoxShow, GUIEngine_IsListBoxShow),
	    HLL_TODO_EXPORT(SetListBoxCGName, GUIEngine_SetListBoxCGName),
	    HLL_TODO_EXPORT(GetListBoxCGName, GUIEngine_GetListBoxCGName),
	    HLL_TODO_EXPORT(SetListBoxScrollPos, GUIEngine_SetListBoxScrollPos),
	    HLL_TODO_EXPORT(GetListBoxScrollPos, GUIEngine_GetListBoxScrollPos),
	    HLL_TODO_EXPORT(AddListBoxItem, GUIEngine_AddListBoxItem),
	    HLL_TODO_EXPORT(SetListBoxItem, GUIEngine_SetListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxItemCount, GUIEngine_GetListBoxItemCount),
	    HLL_TODO_EXPORT(GetListBoxItem, GUIEngine_GetListBoxItem),
	    HLL_TODO_EXPORT(EraseListBoxItem, GUIEngine_EraseListBoxItem),
	    HLL_TODO_EXPORT(ClearListBoxItem, GUIEngine_ClearListBoxItem),
	    HLL_TODO_EXPORT(SetListBoxFontProperty, GUIEngine_SetListBoxFontProperty),
	    HLL_TODO_EXPORT(GetListBoxFontProperty, GUIEngine_GetListBoxFontProperty),
	    HLL_TODO_EXPORT(SetListBoxSelectIndex, GUIEngine_SetListBoxSelectIndex),
	    HLL_TODO_EXPORT(GetListBoxSelectIndex, GUIEngine_GetListBoxSelectIndex),
	    HLL_TODO_EXPORT(SetComboBoxPos, GUIEngine_SetComboBoxPos),
	    HLL_TODO_EXPORT(SetComboBoxZ, GUIEngine_SetComboBoxZ),
	    HLL_TODO_EXPORT(GetComboBoxX, GUIEngine_GetComboBoxX),
	    HLL_TODO_EXPORT(GetComboBoxY, GUIEngine_GetComboBoxY),
	    HLL_TODO_EXPORT(GetComboBoxZ, GUIEngine_GetComboBoxZ),
	    HLL_TODO_EXPORT(SetComboBoxWidth, GUIEngine_SetComboBoxWidth),
	    HLL_TODO_EXPORT(SetComboBoxHeight, GUIEngine_SetComboBoxHeight),
	    HLL_TODO_EXPORT(GetComboBoxWidth, GUIEngine_GetComboBoxWidth),
	    HLL_TODO_EXPORT(GetComboBoxHeight, GUIEngine_GetComboBoxHeight),
	    HLL_TODO_EXPORT(SetComboBoxTextWidthMargin, GUIEngine_SetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(SetComboBoxTextHeightMargin, GUIEngine_SetComboBoxTextHeightMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextWidthMargin, GUIEngine_GetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextHeightMargin, GUIEngine_GetComboBoxTextHeightMargin),
	    HLL_TODO_EXPORT(SetComboBoxShow, GUIEngine_SetComboBoxShow),
	    HLL_TODO_EXPORT(IsComboBoxShow, GUIEngine_IsComboBoxShow),
	    HLL_TODO_EXPORT(SetComboBoxCGName, GUIEngine_SetComboBoxCGName),
	    HLL_TODO_EXPORT(GetComboBoxCGName, GUIEngine_GetComboBoxCGName),
	    HLL_TODO_EXPORT(SetComboBoxText, GUIEngine_SetComboBoxText),
	    HLL_TODO_EXPORT(GetComboBoxText, GUIEngine_GetComboBoxText),
	    HLL_TODO_EXPORT(SetComboBoxFontProperty, GUIEngine_SetComboBoxFontProperty),
	    HLL_TODO_EXPORT(GetComboBoxFontProperty, GUIEngine_GetComboBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxPos, GUIEngine_SetMultiTextBoxPos),
	    HLL_TODO_EXPORT(SetMultiTextBoxZ, GUIEngine_SetMultiTextBoxZ),
	    HLL_TODO_EXPORT(GetMultiTextBoxX, GUIEngine_GetMultiTextBoxX),
	    HLL_TODO_EXPORT(GetMultiTextBoxY, GUIEngine_GetMultiTextBoxY),
	    HLL_TODO_EXPORT(GetMultiTextBoxZ, GUIEngine_GetMultiTextBoxZ),
	    HLL_TODO_EXPORT(SetMultiTextBoxWidth, GUIEngine_SetMultiTextBoxWidth),
	    HLL_TODO_EXPORT(SetMultiTextBoxHeight, GUIEngine_SetMultiTextBoxHeight),
	    HLL_TODO_EXPORT(GetMultiTextBoxWidth, GUIEngine_GetMultiTextBoxWidth),
	    HLL_TODO_EXPORT(GetMultiTextBoxHeight, GUIEngine_GetMultiTextBoxHeight),
	    HLL_TODO_EXPORT(SetMultiTextBoxShow, GUIEngine_SetMultiTextBoxShow),
	    HLL_TODO_EXPORT(IsMultiTextBoxShow, GUIEngine_IsMultiTextBoxShow),
	    HLL_TODO_EXPORT(SetMultiTextBoxFontProperty, GUIEngine_SetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetMultiTextBoxFontProperty, GUIEngine_GetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxText, GUIEngine_SetMultiTextBoxText),
	    HLL_TODO_EXPORT(GetMultiTextBoxText, GUIEngine_GetMultiTextBoxText),
	    HLL_TODO_EXPORT(SetMultiTextBoxMaxTextLength, GUIEngine_SetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetMultiTextBoxMaxTextLength, GUIEngine_GetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetMultiTextBoxSelectR, GUIEngine_SetMultiTextBoxSelectR),
	    HLL_TODO_EXPORT(SetMultiTextBoxSelectG, GUIEngine_SetMultiTextBoxSelectG),
	    HLL_TODO_EXPORT(SetMultiTextBoxSelectB, GUIEngine_SetMultiTextBoxSelectB),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectR, GUIEngine_GetMultiTextBoxSelectR),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectG, GUIEngine_GetMultiTextBoxSelectG),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectB, GUIEngine_GetMultiTextBoxSelectB),
	    HLL_TODO_EXPORT(SetMultiTextBoxCGName, GUIEngine_SetMultiTextBoxCGName),
	    HLL_TODO_EXPORT(GetMultiTextBoxCGName, GUIEngine_GetMultiTextBoxCGName),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(Parts_DeletePartsTopTextLine, GUIEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsTextHighlight, GUIEngine_Parts_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_AddPartsTextHighlight, GUIEngine_Parts_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_ClearPartsTextHighlight, GUIEngine_Parts_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_SetPartsTextCountReturn, GUIEngine_Parts_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(Parts_GetPartsTextCountReturn, GUIEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetNumeralCG, GUIEngine_Parts_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(Parts_SetNumeralFont, GUIEngine_Parts_SetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSize, GUIEngine_Parts_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, GUIEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSize, GUIEngine_Parts_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, GUIEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsFlat, GUIEngine_Parts_SetPartsFlat),
	    HLL_TODO_EXPORT(Parts_IsPartsFlatEnd, GUIEngine_Parts_IsPartsFlatEnd),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, GUIEngine_Parts_GetPartsFlatCurrentFrameNumber),
	    HLL_TODO_EXPORT(Parts_BackPartsFlatBeginFrame, GUIEngine_Parts_BackPartsFlatBeginFrame),
	    HLL_TODO_EXPORT(Parts_StepPartsFlatFinalFrame, GUIEngine_Parts_StepPartsFlatFinalFrame),
	    HLL_TODO_EXPORT(Parts_SetPartsFlatSurfaceArea, GUIEngine_Parts_SetPartsFlatSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsFlatAndStop, GUIEngine_Parts_SetPartsFlatAndStop),
	    HLL_TODO_EXPORT(Parts_StopPartsFlat, GUIEngine_Parts_StopPartsFlat),
	    HLL_TODO_EXPORT(Parts_StartPartsFlat, GUIEngine_Parts_StartPartsFlat),
	    HLL_TODO_EXPORT(Parts_GoFramePartsFlat, GUIEngine_Parts_GoFramePartsFlat),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatEndFrame, GUIEngine_Parts_GetPartsFlatEndFrame),
	    HLL_TODO_EXPORT(Parts_ExistsFlatFile, GUIEngine_Parts_ExistsFlatFile),
	    HLL_TODO_EXPORT(Parts_ClearPartsConstructionProcess, GUIEngine_Parts_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, GUIEngine_Parts_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, GUIEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, GUIEngine_Parts_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, GUIEngine_Parts_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, GUIEngine_Parts_AddMulFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, GUIEngine_Parts_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_TODO_EXPORT(Parts_CreateParts3DLayerPluginID, GUIEngine_Parts_CreateParts3DLayerPluginID),
	    HLL_TODO_EXPORT(Parts_GetParts3DLayerPluginID, GUIEngine_Parts_GetParts3DLayerPluginID),
	    HLL_TODO_EXPORT(Parts_ReleaseParts3DLayerPluginID, GUIEngine_Parts_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPos, PE_SetPos),
	    HLL_EXPORT(Parts_SetZ, PE_SetZ),
	    HLL_EXPORT(Parts_SetShow, PE_SetShow),
	    HLL_EXPORT(Parts_SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(Parts_SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_TODO_EXPORT(Parts_SetPassCursor, GUIEngine_Parts_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_EXPORT(Parts_SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(Parts_SetResetTimerByChangeInputStatus, GUIEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(Parts_SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_TODO_EXPORT(Parts_SetDrag, GUIEngine_Parts_SetDrag),
	    HLL_EXPORT(Parts_SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_TODO_EXPORT(Parts_SetInputState, GUIEngine_Parts_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(Parts_SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_TODO_EXPORT(Parts_SetSoundNumber, GUIEngine_Parts_SetSoundNumber),
	    HLL_EXPORT(Parts_SetClickMissSoundNumber, GUIEngine_Parts_SetClickMissSoundNumber),
	    HLL_EXPORT(Parts_SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(Parts_SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(Parts_SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(Parts_SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(Parts_SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_TODO_EXPORT(Parts_SetPartsAlphaClipperPartsNumber, GUIEngine_Parts_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(Parts_GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(Parts_GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(Parts_GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(Parts_GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(Parts_GetPartsDrawFilter, PE_GetPartsDrawFilter),
	    HLL_TODO_EXPORT(Parts_GetPartsPassCursor, GUIEngine_Parts_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(Parts_GetPartsSpeedupRateByMessageSkip, GUIEngine_Parts_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(Parts_GetResetTimerByChangeInputStatus, GUIEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_GetAddColor, PE_GetAddColor),
	    HLL_EXPORT(Parts_GetMultiplyColor, PE_GetMultiplyColor),
	    HLL_TODO_EXPORT(Parts_GetPartsDrag, GUIEngine_Parts_GetPartsDrag),
	    HLL_EXPORT(Parts_GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PE_GetParentPartsNumber),
	    HLL_TODO_EXPORT(Parts_GetInputState, GUIEngine_Parts_GetInputState),
	    HLL_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PE_GetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(Parts_GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_TODO_EXPORT(Parts_GetSoundNumber, GUIEngine_Parts_GetSoundNumber),
	    HLL_TODO_EXPORT(Parts_GetClickMissSoundNumber, GUIEngine_Parts_GetClickMissSoundNumber),
	    HLL_EXPORT(Parts_GetPartsMagX, PE_GetPartsMagX),
	    HLL_EXPORT(Parts_GetPartsMagY, PE_GetPartsMagY),
	    HLL_TODO_EXPORT(Parts_GetPartsRotateX, GUIEngine_Parts_GetPartsRotateX),
	    HLL_TODO_EXPORT(Parts_GetPartsRotateY, GUIEngine_Parts_GetPartsRotateY),
	    HLL_EXPORT(Parts_GetPartsRotateZ, PE_GetPartsRotateZ),
	    HLL_TODO_EXPORT(Parts_GetPartsAlphaClipperPartsNumber, GUIEngine_Parts_GetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(Parts_IsPartsPixelDecide, GUIEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(Parts_AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(Parts_AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(Parts_AddMotionCG, GUIEngine_Parts_AddMotionCG),
	    HLL_TODO_EXPORT(Parts_AddMotionCGTermination, GUIEngine_Parts_AddMotionCGTermination),
	    HLL_EXPORT(Parts_AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(Parts_AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(Parts_AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(Parts_AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(Parts_AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(Parts_AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(Parts_AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(Parts_AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(Parts_AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(Parts_AddWholeMotionVibrationSize, GUIEngine_Parts_AddWholeMotionVibrationSize),
	    HLL_EXPORT(Parts_AddMotionSound, PE_AddMotionSound),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_TODO_EXPORT(GetClickNumber, GUIEngine_GetClickNumber),
	    HLL_EXPORT(Parts_BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(Parts_EndMotion, PE_EndMotion),
	    HLL_EXPORT(Parts_IsMotion, PE_IsMotion),
	    HLL_EXPORT(Parts_SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(Parts_UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load));

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void GUIEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "GUIEngine");
	assert(libno >= 0);

	// XXX: if PartsEngine exists, don't call PE_Save/PE_Load for GUIEngine
	//      (e.g. Oyako Rankan)
	int pe_libno = ain_get_library(ain, "PartsEngine");
	if (pe_libno >= 0) {
		if ((fun = get_fun(libno, "Save")))
			static_library_replace(&lib_GUIEngine, "Save", GUIEngine_Save);
		if ((fun = get_fun(libno, "SaveWithoutHideParts")))
			static_library_replace(&lib_GUIEngine, "SaveWithoutHideParts", GUIEngine_Save);
		if ((fun = get_fun(libno, "Load")))
			static_library_replace(&lib_GUIEngine, "Load", GUIEngine_Load);
	}
}
