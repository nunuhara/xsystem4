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

#ifndef ANTEATERADVENGINE_H
#define ANTEATERADVENGINE_H

#include <stdbool.h>

struct string;
struct page;

void ADVLogList_Clear(void);
void ADVLogList_AddText(struct string **text);
void ADVLogList_AddNewLine(void);
void ADVLogList_AddNewPage(void);
void ADVLogList_AddVoice(int nVoiceNumber);
void ADVLogList_SetEnable(bool bEnable);
bool ADVLogList_IsEnable(void);
int ADVLogList_GetNumofADVLog(void);
int ADVLogList_GetNumofADVLogText(int nADVLog);
void ADVLogList_GetADVLogText(int nADVLog, int nText, struct string **text);
int ADVLogList_GetNumofADVLogVoice(int nADVLog);
int ADVLogList_GetADVLogVoice(int nADVLog, int Index);
int ADVLogList_GetADVLogVoiceLast(int log_no); // for StoatSpriteEngine
bool ADVLogList_Save(struct page **iarray);
bool ADVLogList_Load(struct page **iarray);
bool ADVSceneKeeper_AddADVScene(struct page **page);
int ADVSceneKeeper_GetNumofADVScene(void);
bool ADVSceneKeeper_GetADVScene(int nIndex, struct page **page);
void ADVSceneKeeper_Clear(void);
bool ADVSceneKeeper_Save(struct page **iarray);
bool ADVSceneKeeper_Load(struct page **iarray);

#endif /* ANTEATERADVENGINE_H */
