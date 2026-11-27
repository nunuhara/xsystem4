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

#ifndef SYSTEM4_SAVEDATA_H
#define SYSTEM4_SAVEDATA_H

#include <stdbool.h>
#include <stdio.h>
#include "cJSON.h"
#include "vm.h"

FILE *atomic_save_open(const char *path, const char *mode, char **tmp_path_out);
bool atomic_save_commit(FILE *fp, const char *path, char *tmp_path, bool keep_backup);
void atomic_save_abort(FILE *fp, char *tmp_path);

void json_load_page(struct page *page, cJSON *vars, bool call_dtors);

int save_json(const char *filename, cJSON *json);
cJSON *load_json(const char *filename);
int save_globals(const char *keyname, const char *filename, const char *group_name, int *n);
int load_globals(const char *keyname, const char *filename, const char *group_name, int *n);
int write_group_save_comment(const char *keyname, const char *filename, const struct string *log);
int read_group_save_comment(const char *keyname, const char *filename, struct string **log_out);
int serialize_structs(const char *filename, struct page *struct_page_list, bool save_folder);
int deserialize_structs(const char *filename, struct page *struct_page_list, bool save_folder);
int write_serialize_struct_comment(const char *filename, const struct string *comment, bool save_folder);
int read_serialize_struct_comment(const char *filename, struct string **comment_out, bool save_folder);
bool x4ss_is_container_file(const char *path);
bool x4ss_convert_to_gdss_file(const char *src_path, const char *dst_path);
bool savedata_export_pc_file(const char *src_path, const char *dst_path);
bool savedata_export_pc_file_with_ain(const char *src_path, const char *dst_path, const char *ain_path);
bool savedata_file_has_xsystem4_only_state(const char *src_path);
int delete_save_file(const char *filename);
void savedata_set_background_flush_hook(void (*hook)(void));
void savedata_flush_on_background(void);
void savedata_reset(void);

bool save_names_equal(const char *a, const char *b);
bool save_symbols_equal(const char *a, const char *b);
void save_cjk_canon_forms(const char *s, char **sjis_out, char **gbk_out);
void save_cjk_canon_forms_buf(const char *s, char *sjis_buf, size_t sjis_cap, char **sjis_out,
		char *gbk_buf, size_t gbk_cap, char **gbk_out);
bool save_symbol_prefix_equal(const char *full_sym, const char *prefix_buf, size_t prefix_len);
int save_symbol_similarity(const char *a, const char *b);
int save_text_gbk_bias(const char *s);
char *save_transcode_cstr(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len);
char *save_transcode_cstr_strict(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len);
struct string *save_decode_vm_string(const char *s, size_t len, bool src_is_gbk);
int save_find_struct(const char *name);
int save_find_enum(const char *name);
int save_find_delegate(const char *name);
int save_find_functype(const char *name);

#endif /* SYSTEM4_SAVEDATA_H */
