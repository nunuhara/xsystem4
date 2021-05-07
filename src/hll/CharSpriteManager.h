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

#ifndef SYSTEM4_HLL_CHARSPRITE_MANAGER_H
#define SYSTEM4_HLL_CHARSPRITE_MANAGER_H

#include <stdbool.h>

struct page;
struct string;

int CharSpriteManager_CreateHandle(void);
bool CharSpriteManager_ReleaseHandle(int handle);
void CharSpriteManager_Clear(void);
void CharSpriteManager_Rebuild(void);
bool CharSpriteManager_Save(struct page **iarray);
bool CharSpriteManager_Load(struct page **iarray);
void CharSprite_Release(int nHandle);
void CharSprite_SetChar(int handle, struct string **s, struct page **property);
void CharSprite_SetPos(int handle, int x, int y);
void CharSprite_SetZ(int handle, int z);
void CharSprite_SetAlphaRate(int handle, int rate);
void CharSprite_SetShow(int handle, bool show);
void CharSprite_GetChar(int handle, struct string **ch);
int CharSprite_GetAlphaRate(int handle);

#endif /* SYSTEM4_HLL_CHARSPRITE_MANAGER_H */
