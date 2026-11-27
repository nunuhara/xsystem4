/* AIN v12 sound channels, GPL-2.0-or-later. */
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "kiwi.h"
#include "mixer.h"
#include "xsystem4.h"
#include "hll.h"

struct sound_meta {
	struct ex *ex;
	struct ex_table *info;
};

struct sound_row_entry {
	struct ex_value *row;
	unsigned nr_columns;
};

static struct id_pool sounds, sound_names, sound_file_names, effects;
static struct sound_meta *sound_metas;
static size_t nr_sound_metas;
static struct sound_row_entry *sound_row_entries;
static size_t nr_sound_row_entries;
static struct hash_table *sound_row_index;

static int parse_sound_num_prefix(const char *name)
{
	if (!name || !isdigit((unsigned char)name[0]))
		return -1;
	char *endp = NULL;
	long val = strtol(name, &endp, 10);
	if (endp == name || val < 0 || val > INT_MAX)
		return -1;
	if (*endp != '_' && *endp != '\0')
		return -1;
	return (int)val;
}

static void register_archive_sound_name(struct archive_data *data, void *user)
{
	(void)user;
	if (!data || !data->name)
		return;
	char *base = archive_basename(data->name);
	int num = parse_sound_num_prefix(base);
	if (num >= 0 && !id_pool_get(&sound_file_names, num))
		id_pool_set(&sound_file_names, num, cstr_to_string(base));
	free(base);
}

struct effect_voice { struct channel *channel; struct effect_voice *next; };
struct effect {
	int span;
	bool multi, started;
	uint32_t last_play;
	struct effect_voice *voices;
};

static struct effect system_effect = { .multi = true };

static void set_default_mixers(const char *const *names, const int *volumes, unsigned count)
{
	config.mixer_nr_channels = count;
	config.mixer_channels = xcalloc(count, sizeof(char *));
	config.mixer_volumes = xcalloc(count, sizeof(int));
	for (unsigned i = 0; i < count; i++) {
		config.mixer_channels[i] = strdup(names[i]);
		config.mixer_volumes[i] = volumes ? volumes[i] : 80;
	}
}

static void add_ex_mixers(void)
{
	if (!config.ex_path)
		return;
	struct ex *ex = ex_read_file(config.ex_path);
	if (!ex)
		return;
	// Ｅ＿サウンドグループ設定
	struct ex_table *table = ex_get_table(ex,
		"\x82\x64\x81\x51\x83\x54\x83\x45\x83\x93\x83\x68\x83\x4f\x83\x8b\x81\x5b\x83\x76\x90\xdd\x92\xe8");
	if (!table || table->nr_columns < 3) {
		ex_free(ex);
		return;
	}
	unsigned old_count = config.mixer_nr_channels;
	unsigned count = old_count;
	unsigned capacity = mixer_get_stream_capacity();
	for (unsigned r = 0; r < table->nr_rows; r++) {
		struct ex_value *row = table->rows[r];
		if (row[0].type != EX_INT || row[0].i < 0) {
			WARNING("Invalid EX sound group at row %u", r);
			continue;
		}
		if ((unsigned)row[0].i >= capacity) {
			WARNING("EX sound group %d exceeds maximum %u; skipping", row[0].i, capacity - 1);
			continue;
		}
		count = max(count, (unsigned)row[0].i + 1);
	}
	if (count > old_count) {
		config.mixer_channels = xrealloc_array(config.mixer_channels, old_count, count, sizeof(char *));
		config.mixer_volumes = xrealloc_array(config.mixer_volumes, old_count, count, sizeof(int));
		for (unsigned i = old_count; i < count; i++) {
			static const char *const names[] = { "Master", "BGM", "SE", "Voice", "Ambient", "BackVoice" };
			char name[32];
			snprintf(name, sizeof(name), "Group%u", i);
			config.mixer_channels[i] = strdup(i < 6 ? names[i] : name);
			config.mixer_volumes[i] = config.default_volume;
		}
		for (unsigned r = 0; r < table->nr_rows; r++) {
			struct ex_value *row = table->rows[r];
			if (row[0].type != EX_INT || row[0].i < (int)old_count || (unsigned)row[0].i >= count)
				continue;
			if (row[2].type == EX_FLOAT && isfinite(row[2].f))
				config.mixer_volumes[row[0].i] = lroundf(fminf(1.0f, fmaxf(0.0f, row[2].f)) * 100);
		}
		config.mixer_nr_channels = count;
	}
	ex_free(ex);
}

static void set_game_default_mixers(void)
{
	if (config.mixer_nr_channels)
		return;
	if (game_rance10) {
		static const char *const names[] = { "Master", "BGM", "SE", "Voice", "Ambient", "BackVoice" };
		static const int vols[] = { 100, 100, 100, 100, 100, 100 };
		set_default_mixers(names, vols, 6);
	} else if (game_dohnadohna) {
		static const char *const names[] = { "Master", "BGM", "SE", "Voice", "SubVoice", "Ambient", "System", "Movie", "Extra" };
		static const int vols[] = { 80, 80, 80, 100, 80, 80, 80, 80, 80 };
		set_default_mixers(names, vols, 9);
	} else if (game_evenicle2) {
		static const char *const names[] = {
			"Master", "BGM", "SE", "Voice", "Movie", "BackVoice",
			"Group6", "Group7", "Group8", "Group9",
			"VoiceChara1", "VoiceChara2", "BackVoiceChara1", "BackVoiceChara2"
		};
		static const int vols[] = { 80, 80, 80, 100, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80 };
		set_default_mixers(names, vols, 14);
	} else if (game_ixseal) {
		static const char *const names[] = {
			"Master", "BGM", "SE", "Voice", "Ambient", "BackVoice",
			"VoiceChara1", "VoiceChara2", "VoiceChara3", "VoiceChara4", "VoiceChara5", "VoiceChara6",
			"BackVoiceChara1", "BackVoiceChara2", "BackVoiceChara3", "BackVoiceChara4", "BackVoiceChara5", "BackVoiceChara6"
		};
		static const int vols[] = {
			80, 80, 80, 100, 80, 18,
			80, 80, 80, 80, 80, 80,
			80, 80, 80, 80, 80, 80
		};
		set_default_mixers(names, vols, 18);
	} else if (game_rance03) {
		static const char *const names[] = {
			"Master", "BGM", "SE", "Voice", "Ambient", "BackVoice",
			"VoiceMale", "VoiceFemale"
		};
		static const int vols[] = { 80, 80, 80, 100, 80, 80, 80, 80 };
		set_default_mixers(names, vols, 8);
	} else if (game_okusama || game_haharanman) {
		static const char *const names[] = {
			"Master", "BGM", "SE", "Voice", "Ambient", "BackVoice",
			"SystemVoice", "Extra"
		};
		static const int vols[] = { 80, 80, 80, 100, 80, 80, 100, 80 };
		set_default_mixers(names, vols, 8);
	} else if (game_night_remaster) {
		static const char *const names[] = {
			"Master", "BGM", "SE", "Voice", "Ambient", "BackVoice"
		};
		static const int vols[] = { 80, 80, 80, 80, 80, 80 };
		set_default_mixers(names, vols, 6);
	}
}

static void kiwi_prelink(void)
{
	set_game_default_mixers();
	add_ex_mixers();
}

static bool sound_file_names_loaded = false;

static char *sound_key_normalize(const char *src)
{
	size_t len = strlen(src);
	char *out = xmalloc(len + 1);
	size_t i = 0;
	while (src[i]) {
		unsigned char c = (unsigned char)src[i];
		if (SJIS_2BYTE(c) && src[i + 1]) {
			out[i] = src[i];
			out[i + 1] = src[i + 1];
			i += 2;
		} else {
			if (c == '\\')
				out[i] = '/';
			else if (c >= 'A' && c <= 'Z')
				out[i] = (char)(c - 'A' + 'a');
			else
				out[i] = (char)c;
			i++;
		}
	}
	out[i] = '\0';
	return out;
}

static void kiwi_clear_timemaps(void)
{
	if (sound_row_index) {
		ht_free(sound_row_index);
		sound_row_index = NULL;
	}
	free(sound_row_entries);
	sound_row_entries = NULL;
	nr_sound_row_entries = 0;
	for (size_t i = 0; i < nr_sound_metas; i++)
		ex_free(sound_metas[i].ex);
	free(sound_metas);
	sound_metas = NULL;
	nr_sound_metas = 0;
}

static void kiwi_add_timemap_data(struct archive_data *data)
{
	if (!data)
		return;
	if (!data->data || !data->size) {
		archive_free_data(data);
		return;
	}
	struct ex *ex = ex_read(data->data, data->size);
	archive_free_data(data);
	if (!ex)
		return;
	struct ex_table *info = ex_get_table(ex, "SoundInfo");
	if (!info) {
		ex_free(ex);
		return;
	}
	sound_metas = xrealloc_array(sound_metas, nr_sound_metas, nr_sound_metas + 1, sizeof(*sound_metas));
	sound_metas[nr_sound_metas].ex = ex;
	sound_metas[nr_sound_metas].info = info;
	nr_sound_metas++;
}

static bool ascii_starts_with_icase(const char *s, const char *prefix)
{
	while (*prefix) {
		unsigned char a = (unsigned char)*s++;
		unsigned char b = (unsigned char)*prefix++;
		if (!a)
			return false;
		if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
		if (a != b)
			return false;
	}
	return true;
}

static bool ascii_ends_with_icase(const char *s, const char *suffix)
{
	size_t slen = strlen(s);
	size_t flen = strlen(suffix);
	if (slen < flen)
		return false;
	return ascii_starts_with_icase(s + (slen - flen), suffix);
}

struct timemap_collect_ctx {
	enum asset_type type;
	struct hash_table *seen;
};

static void kiwi_collect_timemap_cb(struct archive_data *data, void *user)
{
	if (!data || !data->name)
		return;
	const char *base = data->name;
	for (const char *p = data->name; *p;) {
		if (SJIS_2BYTE((unsigned char)*p) && p[1]) {
			p += 2;
			continue;
		}
		if (*p == '/' || *p == '\\')
			base = p + 1;
		p++;
	}
	if (!ascii_starts_with_icase(base, "timemap_") || !ascii_ends_with_icase(base, ".ex"))
		return;
	struct timemap_collect_ctx *ctx = user;
	char *norm = sound_key_normalize(data->name);
	bool already_seen = ctx->seen && ht_get(ctx->seen, norm, NULL) != NULL;
	if (!already_seen) {
		if (ctx->seen)
			ht_put(ctx->seen, norm, (void *)1);
		kiwi_add_timemap_data(asset_get_by_name(ctx->type, data->name, NULL));
	}
	free(norm);
}

static void kiwi_build_sound_row_index(void)
{
	if (sound_row_index) {
		ht_free(sound_row_index);
		sound_row_index = NULL;
	}
	free(sound_row_entries);
	sound_row_entries = NULL;
	nr_sound_row_entries = 0;

	size_t total_rows = 0;
	for (size_t m = 0; m < nr_sound_metas; m++) {
		struct ex_table *info = sound_metas[m].info;
		if (info && info->nr_columns >= 1)
			total_rows += info->nr_rows;
	}
	if (!total_rows)
		return;

	sound_row_entries = xcalloc(total_rows, sizeof(*sound_row_entries));
	sound_row_index = ht_create(total_rows * 2);
	for (size_t m = 0; m < nr_sound_metas; m++) {
		struct ex_table *info = sound_metas[m].info;
		if (!info || info->nr_columns < 1)
			continue;
		for (unsigned r = 0; r < info->nr_rows; r++) {
			if (info->rows[r][0].type != EX_STRING || !info->rows[r][0].s)
				continue;
			struct sound_row_entry *entry = &sound_row_entries[nr_sound_row_entries++];
			entry->row = info->rows[r];
			entry->nr_columns = info->nr_columns;
			char *key = sound_key_normalize(info->rows[r][0].s->text);
			ht_put(sound_row_index, key, entry);
			free(key);
		}
	}
}

static void ensure_sound_file_names(void)
{
	if (sound_file_names_loaded)
		return;
	sound_file_names_loaded = true;
	for (size_t m = 0; m < nr_sound_metas; m++) {
		struct ex_table *info = sound_metas[m].info;
		if (!info || info->nr_columns < 1)
			continue;
		for (unsigned r = 0; r < info->nr_rows; r++) {
			if (info->rows[r][0].type != EX_STRING)
				continue;
			struct string *s = info->rows[r][0].s;
			int num = parse_sound_num_prefix(s->text);
			if (num >= 0 && !id_pool_get(&sound_file_names, num))
				id_pool_set(&sound_file_names, num, string_dup(s));
		}
	}
	asset_foreach_file(ASSET_SOUND, register_archive_sound_name, NULL);
	asset_foreach_file(ASSET_BGM, register_archive_sound_name, NULL);
	asset_foreach_file(ASSET_VOICE, register_archive_sound_name, NULL);
}

static void kiwi_init(void)
{
	id_pool_init(&sounds, 0);
	id_pool_init(&sound_names, 0);
	id_pool_init(&sound_file_names, 0);
	id_pool_init(&effects, 0);
	sound_file_names_loaded = false;
	kiwi_clear_timemaps();
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	audio_init();

	const enum asset_type types[] = { ASSET_SOUND, ASSET_BGM, ASSET_VOICE };
	for (unsigned i = 0; i < sizeof(types) / sizeof(*types); i++) {
		struct timemap_collect_ctx ctx = {
			.type = types[i],
			.seen = ht_create(16),
		};
		asset_foreach_file(types[i], kiwi_collect_timemap_cb, &ctx);
		ht_free(ctx.seen);
	}
	if (!nr_sound_metas) {
		kiwi_add_timemap_data(asset_get_by_name(ASSET_SOUND, "timemap_sound.ex", NULL));
	}
	kiwi_build_sound_row_index();
}

static void stop_effect(struct effect *effect)
{
	while (effect->voices) {
		struct effect_voice *voice = effect->voices;
		effect->voices = voice->next;
		channel_close(voice->channel);
		free(voice);
	}
}

static void kiwi_fini(void)
{
	stop_effect(&system_effect);
	for (int i = id_pool_get_first(&sounds); i >= 0; i = id_pool_get_next(&sounds, i))
		channel_close(id_pool_get(&sounds, i));
	for (int i = id_pool_get_first(&sound_names); i >= 0; i = id_pool_get_next(&sound_names, i))
		free_string(id_pool_get(&sound_names, i));
	for (int i = id_pool_get_first(&sound_file_names); i >= 0; i = id_pool_get_next(&sound_file_names, i))
		free_string(id_pool_get(&sound_file_names, i));
	for (int i = id_pool_get_first(&effects); i >= 0; i = id_pool_get_next(&effects, i)) {
		struct effect *effect = id_pool_get(&effects, i);
		stop_effect(effect);
		free(effect);
	}
	id_pool_delete(&sounds);
	id_pool_delete(&sound_names);
	id_pool_delete(&sound_file_names);
	id_pool_delete(&effects);
	sound_file_names_loaded = false;
	kiwi_clear_timemaps();
}

static struct ex_value *sound_row(const char *name, unsigned *nr_columns_out)
{
	if (!name || !nr_sound_metas)
		return NULL;
	char *base = archive_basename(name);
	if (sound_row_index) {
		char *key = sound_key_normalize(base);
		struct sound_row_entry *entry = ht_get(sound_row_index, key, NULL);
		free(key);
		if (entry) {
			free(base);
			if (nr_columns_out)
				*nr_columns_out = entry->nr_columns;
			return entry->row;
		}
	}
	for (size_t m = 0; m < nr_sound_metas; m++) {
		struct ex_table *info = sound_metas[m].info;
		if (!info)
			continue;
		int row = ex_row_at_string_key(info, base);
		if (row >= 0) {
			free(base);
			if (nr_columns_out)
				*nr_columns_out = info->nr_columns;
			return info->rows[row];
		}
	}
	free(base);
	return NULL;
}

static int sound_group(struct string *name)
{
	if (!name) return 0;
	unsigned nr_columns = 0;
	struct ex_value *row = sound_row(name->text, &nr_columns);
	return row && nr_columns >= 2 && row[1].type == EX_INT ? row[1].i : 0;
}

static struct channel *open_sound(struct string *name)
{
	if (!name) return NULL;
	struct channel *channel = NULL;
	const enum asset_type types[] = { ASSET_SOUND, ASSET_BGM, ASSET_VOICE };
	for (unsigned i = 0; i < sizeof(types) / sizeof(*types); i++) {
		struct archive_data *data = asset_get_by_name(types[i], name->text, NULL);
		if (!data) continue;
		channel = channel_open_archive_data(data, types[i], AUDIO_NO_METADATA);
		break;
	}
	if (!channel) {
		char *path = gamedir_path_icase(name->text);
		if (path && file_exists(path)) channel = channel_open_file(path);
		free(path);
	}
	if (!channel) return NULL;
	unsigned nr_columns = 0;
	struct ex_value *row = sound_row(name->text, &nr_columns);
	if (row && nr_columns >= 2) {
		if (row[1].type == EX_INT && !channel_set_group(channel, row[1].i)) {
			WARNING("Failed to assign sound '%s' to group %d", display_sjis0(name->text), row[1].i);
			channel_close(channel);
			return NULL;
		}
		if (nr_columns >= 4 && row[2].type == EX_INT && row[3].type == EX_INT &&
				row[2].i >= 0 && row[3].i > row[2].i &&
				row[3].i <= channel_get_sample_length(channel)) {
			channel_set_loop_start_pos(channel, row[2].i);
			channel_set_loop_end_pos(channel, row[3].i);
		}
	}
	return channel;
}

static int free_id(struct id_pool *pool, int low, int high)
{
	if (low < 0 || high < low) return -1;
	for (int id = low; ; id++) {
		if (!id_pool_get(pool, id)) return id;
		if (id == high) return -1;
	}
}

static int kiwi_free(int low, int high) { return free_id(&sounds, low, high); }
static int kiwi_free_se(int low, int high) { return free_id(&effects, low, high); }
static bool kiwi_exists(int id) { return id_pool_get(&sounds, id); }
static bool kiwi_exists_se(int id) { return id_pool_get(&effects, id); }

static bool kiwi_prepare(int id, struct string *name, struct string *filter, bool streaming)
{
	if (id < 0 || !name) return false;
	if (filter && filter->size) WARNING("Unimplemented sound filter: %s", display_sjis0(filter->text));
	// libsndfile streams both archive and filesystem data on demand.
	(void)streaming;
	struct channel *channel = open_sound(name);
	if (!channel) return false;
	struct channel *old = id_pool_set(&sounds, id, channel);
	if (old) channel_close(old);
	struct string *old_name = id_pool_set(&sound_names, id, string_dup(name));
	if (old_name) free_string(old_name);
	return true;
}

static bool kiwi_prepare_3(int id, struct string *name, bool streaming)
{
	return kiwi_prepare(id, name, NULL, streaming);
}

static void *kiwi_resolve_prepare(const struct ain_hll_function *f)
{
	return f->nr_arguments == 3 ? (void *)kiwi_prepare_3 : (void *)kiwi_prepare;
}

static int kiwi_get_sound_file_name(int id, struct string **out)
{
	if (id < 0)
		return 0;
	ensure_sound_file_names();
	struct string *s = id_pool_get(&sound_file_names, id);
	if (!s)
		s = id_pool_get(&sound_names, id);
	if (!s)
		return 0;
	if (out) {
		if (*out)
			free_string(*out);
		*out = string_dup(s);
	}
	return 1;
}

static bool kiwi_unprepare(int id)
{
	struct string *old_name = id_pool_release(&sound_names, id);
	if (old_name) free_string(old_name);
	struct channel *channel = id_pool_release(&sounds, id);
	if (!channel) return false;
	channel_close(channel);
	return true;
}

#define CHANNEL_CALL(name, result, function) \
	static result name(int id) { \
		struct channel *channel = id_pool_get(&sounds, id); \
		return channel ? function(channel) : 0; \
	}
CHANNEL_CALL(kiwi_play, bool, channel_play)
CHANNEL_CALL(kiwi_stop, bool, channel_stop)
CHANNEL_CALL(kiwi_playing, bool, channel_is_playing)
CHANNEL_CALL(kiwi_get_loop, int, channel_get_loop_count)
CHANNEL_CALL(kiwi_pause, bool, channel_pause)
CHANNEL_CALL(kiwi_restart, bool, channel_restart)
CHANNEL_CALL(kiwi_paused, bool, channel_is_paused)
CHANNEL_CALL(kiwi_stop_fade, bool, channel_stop_fade)
CHANNEL_CALL(kiwi_fading, bool, channel_is_fading)
CHANNEL_CALL(kiwi_position, int, channel_get_pos)
CHANNEL_CALL(kiwi_length, int, channel_get_length)
CHANNEL_CALL(kiwi_group, int, channel_get_group)
#undef CHANNEL_CALL

static bool kiwi_loop(int id, int count)
{
	struct channel *channel = id_pool_get(&sounds, id);
	return channel && channel_set_loop_count(channel, count);
}

static bool kiwi_seek(int id, int milliseconds)
{
	struct channel *channel = id_pool_get(&sounds, id);
	return channel && channel_seek(channel, milliseconds);
}

static void memory_archive_free_data(struct archive_data *data)
{
	free(data->data);
	free(data);
}

static struct archive_ops memory_archive_ops = {
	.free_data = memory_archive_free_data,
};

static struct archive memory_archive = {
	.ops = &memory_archive_ops,
};

bool kiwi_prepare_from_memory(int id, uint8_t *buf, size_t size, int group)
{
	if (id < 0 || !buf || !size) {
		free(buf);
		return false;
	}
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	audio_init();
	struct archive_data *dfile = xcalloc(1, sizeof(*dfile));
	dfile->data = buf;
	dfile->size = size;
	dfile->archive = &memory_archive;
	struct channel *channel = channel_open_archive_data(dfile, ASSET_SOUND, AUDIO_NO_METADATA);
	if (!channel) return false;
	if (group >= 0) channel_set_group(channel, group);
	struct string *old_name = id_pool_release(&sound_names, id);
	if (old_name) free_string(old_name);
	struct channel *old = id_pool_set(&sounds, id, channel);
	if (old) channel_close(old);
	return true;
}

bool kiwi_play_sound(int id) { return kiwi_play(id); }
bool kiwi_stop_sound(int id) { return kiwi_stop(id); }
bool kiwi_unprepare_sound(int id) { return kiwi_unprepare(id); }
bool kiwi_is_playing_sound(int id) { return kiwi_playing(id); }
int kiwi_get_sound_pos(int id) { return kiwi_position(id); }
bool kiwi_seek_sound(int id, int milliseconds) { return kiwi_seek(id, milliseconds); }

static bool kiwi_fade(int id, int milliseconds, float volume, bool stop, int type)
{
	struct channel *channel = id_pool_get(&sounds, id);
	if (!channel) return false;
	// Out-of-range types use linear interpolation in the reference engine.
	if (type < 0 || type >= 8) type = 0;
	return channel_fade_with_curve(channel, max(0, milliseconds), lroundf(volume * 100), stop, type);
}

static bool kiwi_set_se(int id, int span, bool multi)
{
	if (id < 0 || span < 0) return false;
	struct effect *effect = id_pool_get(&effects, id);
	if (!effect) {
		effect = xcalloc(1, sizeof(*effect));
		id_pool_set(&effects, id, effect);
	}
	effect->span = span;
	effect->multi = multi;
	return true;
}

static bool play_effect(struct effect *effect, struct string *name)
{
	if (!effect || !name) return false;
	uint32_t now = SDL_GetTicks();
	if (effect->started && now - effect->last_play < (uint32_t)effect->span) return true;
	for (struct effect_voice **p = &effect->voices; *p;) {
		struct effect_voice *voice = *p;
		if (!channel_is_playing(voice->channel)) {
			*p = voice->next;
			channel_close(voice->channel);
			free(voice);
		} else p = &voice->next;
	}
	struct channel *channel = open_sound(name);
	if (!channel) return false;
	if (!effect->multi) stop_effect(effect);
	struct effect_voice *voice = xcalloc(1, sizeof(*voice));
	voice->channel = channel;
	voice->next = effect->voices;
	effect->voices = voice;
	effect->started = true;
	effect->last_play = now;
	return channel_play(channel);
}

static bool kiwi_play_se(int id, struct string *name)
{
	if (id < 0) return false;
	if (!kiwi_exists_se(id)) kiwi_set_se(id, 0, true);
	return play_effect(id_pool_get(&effects, id), name);
}

static bool kiwi_play_se_v14(int id, struct string *name, struct string *filter)
{
	if (filter && filter->size)
		WARNING("Unimplemented SE filter: %s", display_sjis0(filter->text));
	return kiwi_play_se(id, name);
}

static void *kiwi_resolve_play_se(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return kiwi_play_se_v14;
	return kiwi_play_se;
}

bool kiwi_play_system_sound(struct string *name)
{
	return !name || !name->size || play_effect(&system_effect, name);
}

void kiwi_stop_non_system_sound(void)
{
	for (int i = id_pool_get_first(&sounds); i >= 0; i = id_pool_get_next(&sounds, i))
		channel_stop(id_pool_get(&sounds, i));
	for (int i = id_pool_get_first(&effects); i >= 0; i = id_pool_get_next(&effects, i))
		stop_effect(id_pool_get(&effects, i));
}

static bool kiwi_stop_se(int id)
{
	struct effect *effect = id_pool_get(&effects, id);
	if (!effect) return false;
	stop_effect(effect);
	return true;
}

static bool kiwi_playing_se(int id)
{
	struct effect *effect = id_pool_get(&effects, id);
	if (!effect) return false;
	for (struct effect_voice *v = effect->voices; v; v = v->next)
		if (channel_is_playing(v->channel)) return true;
	return false;
}

static bool kiwi_file_exists(struct string *name)
{
	if (!name) return false;
	if (asset_exists_by_name(ASSET_SOUND, name->text, NULL)
			|| asset_exists_by_name(ASSET_BGM, name->text, NULL)
			|| asset_exists_by_name(ASSET_VOICE, name->text, NULL)) return true;
	char *path = gamedir_path_icase(name->text);
	bool exists = path && file_exists(path);
	free(path);
	return exists;
}

static int kiwi_file_length(struct string *name)
{
	struct channel *channel = open_sound(name);
	if (!channel) return 0;
	int length = channel_get_length(channel);
	channel_close(channel);
	return length;
}

static float kiwi_group_volume(int group)
{
	int volume = 0;
	mixer_get_volume(group, &volume);
	return volume / 100.0f;
}

static int kiwi_samples(int time, int rate) { return (int64_t)time * rate / 1000; }

static bool kiwi_mixer_name(int index, struct string **out)
{
	if (!out) return false;
	const char *name = mixer_get_name(index);
	if (!name) return false;
	if (*out)
		free_string(*out);
	*out = cstr_to_string(name);
	return true;
}

static bool kiwi_mixer_set_name(int index, struct string *name) { return name && mixer_set_name(index, name->text); }
static bool kiwi_mixer_default(int index, int *volume)
{
	if (!volume || index < 0 || (unsigned)index >= config.mixer_nr_channels) return false;
	*volume = config.mixer_volumes[index];
	return true;
}

struct static_library lib_KiwiSoundEngine_v12 = {
	.name = "KiwiSoundEngine",
	.functions = {
		HLL_EXPORT(_PreLink, kiwi_prelink), HLL_EXPORT(_ModuleInit, kiwi_init), HLL_EXPORT(_ModuleFini, kiwi_fini),
		HLL_EXPORT(SetGlobalFocus, mixer_set_global_focus),
		HLL_EXPORT(GetFreeSeID, kiwi_free_se), HLL_EXPORT(IsExistSeID, kiwi_exists_se),
		HLL_EXPORT(SetSeParam, kiwi_set_se), HLL_OVERLOAD(PlaySe, kiwi_resolve_play_se),
		HLL_EXPORT(StopSe, kiwi_stop_se), HLL_EXPORT(IsPlaySe, kiwi_playing_se),
		HLL_EXPORT(GetFreeID, kiwi_free), HLL_EXPORT(IsExistID, kiwi_exists),
		HLL_OVERLOAD(Prepare, kiwi_resolve_prepare), HLL_EXPORT(Unprepare, kiwi_unprepare),
		HLL_EXPORT(GetSoundFileName, kiwi_get_sound_file_name),
		HLL_EXPORT(Play, kiwi_play), HLL_EXPORT(Stop, kiwi_stop), HLL_EXPORT(IsPlay, kiwi_playing),
		HLL_EXPORT(SetLoopCount, kiwi_loop), HLL_EXPORT(GetLoopCount, kiwi_get_loop),
		HLL_EXPORT(Pause, kiwi_pause), HLL_EXPORT(Restart, kiwi_restart), HLL_EXPORT(IsPause, kiwi_paused),
		HLL_EXPORT(Fade, kiwi_fade), HLL_EXPORT(StopFade, kiwi_stop_fade), HLL_EXPORT(IsFade, kiwi_fading),
		HLL_EXPORT(Seek, kiwi_seek), HLL_EXPORT(GetPos, kiwi_position), HLL_EXPORT(GetLength, kiwi_length),
		HLL_EXPORT(GetGroupNum, kiwi_group), HLL_EXPORT(IsExistFile, kiwi_file_exists),
		HLL_EXPORT(GetLengthFromFile, kiwi_file_length), HLL_EXPORT(GetGroupNumFromFile, sound_group),
		HLL_EXPORT(GetGroupVolume, kiwi_group_volume), HLL_EXPORT(MillisecondsToSamples, kiwi_samples),
		HLL_EXPORT(GetMixerNumof, mixer_get_numof), HLL_EXPORT(GetMixerName, kiwi_mixer_name),
		HLL_EXPORT(GetMixerVolume, mixer_get_volume), HLL_EXPORT(GetMixerDefaultVolume, kiwi_mixer_default),
		HLL_EXPORT(GetMixerMute, mixer_get_mute), HLL_EXPORT(SetMixerName, kiwi_mixer_set_name),
		HLL_EXPORT(SetMixerVolume, mixer_set_volume), HLL_EXPORT(SetMixerMute, mixer_set_mute),
		{0}
	}
};
