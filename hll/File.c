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

#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "hll.h"
#include "../system4.h"

//int Open(string szName, int nType)
hll_unimplemented(File, Open);
//int Close()
hll_unimplemented(File, Close);
//int Read(ref struct pIVMStruct)
hll_unimplemented(File, Read);
//int Write(struct pIVMStruct)
hll_unimplemented(File, Write);
//int Copy(string szSrcName, string szDstName)
hll_unimplemented(File, Copy);
//int Delete(string szName)
hll_unimplemented(File, Delete);

/*

  struct tagSaveDate {
      int m_nYear;
      int m_nMonth;
      int m_nDay;
      int m_nHour;
      int m_nMinute;
      int m_nSecond;
      int m_nDayOfWeek;
  };

 */

//int GetTime(string szName, ref struct pIVMStruct)
hll_defun(GetTime, args)
{
	struct stat s;
	char *path = unix_path(hll_string_ref(args[0].i)->text);
	union vm_value *date = hll_struct_ref(args[1].i);

	if (stat(path, &s) < 0) {
		WARNING("stat failed: %s", strerror(errno));
		hll_return(0);
	}

	struct tm *tm = localtime(&s.st_mtime);
	if (!tm) {
		WARNING("localtime failed: %s", strerror(errno));
		hll_return(0);
	}

	date[0].i = tm->tm_year;
	date[1].i = tm->tm_mon;
	date[2].i = tm->tm_mday;
	date[3].i = tm->tm_hour;
	date[4].i = tm->tm_min;
	date[5].i = tm->tm_sec;
	date[6].i = tm->tm_wday;

	hll_return(1);
}

//int SetFind(string szName)
hll_unimplemented(File, SetFind);
//int GetFind(ref string szName)
hll_unimplemented(File, GetFind);
//int MakeDirectory(string szName)
hll_unimplemented(File, MakeDirectory);

hll_deflib(File) {
	hll_export(Open),
	hll_export(Close),
	hll_export(Read),
	hll_export(Write),
	hll_export(Copy),
	hll_export(Delete),
	hll_export(GetTime),
	hll_export(SetFind),
	hll_export(GetFind),
	hll_export(MakeDirectory),
	NULL
};
