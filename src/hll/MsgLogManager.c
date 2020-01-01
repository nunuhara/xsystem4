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

#include "hll.h"

hll_warn_unimplemented(MsgLogManager, Numof, 0);
hll_warn_unimplemented(MsgLogManager, AddInt, 0);
hll_warn_unimplemented(MsgLogManager, AddString, 0);
hll_warn_unimplemented(MsgLogManager, GetType, 0);
hll_warn_unimplemented(MsgLogManager, GetInt, 0);
hll_warn_unimplemented(MsgLogManager, GetString, 0);
hll_warn_unimplemented(MsgLogManager, Save, 0);
hll_warn_unimplemented(MsgLogManager, Load, 0);
hll_warn_unimplemented(MsgLogManager, SetLineMax, 0);
hll_warn_unimplemented(MsgLogManager, GetInterface, 0);

hll_deflib(MsgLogManager) {
	hll_export(Numof),
	hll_export(AddInt),
	hll_export(AddString),
	hll_export(GetType),
	hll_export(GetInt),
	hll_export(GetString),
	hll_export(Save),
	hll_export(Load),
	hll_export(SetLineMax),
	hll_export(GetInterface),
	NULL
};
