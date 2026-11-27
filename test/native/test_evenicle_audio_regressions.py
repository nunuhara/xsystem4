#!/usr/bin/env python3
"""EX mixer configuration, native playback lifecycle and BGV volume regressions."""

import struct
import tempfile
import unittest
import wave
from pathlib import Path

from test_rance03_volume_regressions import (
    LIBSYS4_CORE,
    ROOT,
    _ex_block,
    _ex_pstr,
    _ex_soundinfo_table,
    build_extf,
    compile_and_run,
)


def group_table(rows, encoding="cp932"):
    fields = [(b"Group", 1), (b"Voice", 1), (b"Volume", 2)]
    data = bytearray(struct.pack("<I", len(fields)))
    for i, (name, kind) in enumerate(fields):
        data += struct.pack("<I", kind) + _ex_pstr(name)
        data += struct.pack("<II", 0, int(i == 0))
    data += struct.pack("<II", len(fields), len(rows))
    for group, volume in rows:
        data += struct.pack("<IiIiIf", 1, group, 1, -1, 2, volume)
    return _ex_block("Ｅ＿サウンドグループ設定".encode(encoding), 4, data)


HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "xsystem4.h"
#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "mixer.h"
#include "system4/archive.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "hll/hll.h"

struct config config;
struct ain *ain;
bool game_rance10, game_dohnadohna, game_evenicle2, game_rance03, game_ixseal, game_okusama, game_haharanman, game_night_remaster, game_tsumamigui3_jun, game_tsumamigui3_ntr;
bool game_escalayer_reboot, game_tsumamigui3, game_maman;
static const char *wave_path;
static Uint32 ticks;
int SDL_InitSubSystem(Uint32 flags) { return 0; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int cap, const SDL_AudioSpec *want,
                                     SDL_AudioSpec *have, int allowed) {
    *have = *want;
    return 2;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause) {}
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {}
const char *SDL_GetError(void) { return ""; }
Uint32 SDL_GetTicks(void) { return ticks; }
bool asset_exists_by_name(enum asset_type t, const char *name, int *id) { return false; }
bool asset_exists(enum asset_type t, int no) {
    return game_escalayer_reboot && t == ASSET_SOUND && (no == 30690 || no == 11773);
}
struct archive_data *asset_get_by_name(enum asset_type t, const char *name, int *id) { return NULL; }
void asset_foreach_file(enum asset_type t, void (*cb)(struct archive_data*, void*), void *u) {}
const char *display_utf0(const char *s) { return s; }
const char *display_sjis0(const char *s) { return s; }
char *gamedir_path_icase(const char *name) {
    return strcmp(name, "missing") ? strdup(wave_path) : NULL;
}
void static_library_replace(struct static_library *l, const char *name, void *f) {}

#include "audio_mixer.c"
#include "audio.c"
#include "hll/KiwiSoundEngine.c"
#include "hll/KiwiSoundEngine_v12.c"

struct archive_data *asset_get(enum asset_type t, int no) {
    if (!asset_exists(t, no)) return NULL;
    struct archive_data *data = calloc(1, sizeof(*data));
    data->archive = &memory_archive;
    data->data = file_read(wave_path, &data->size);
    assert(data->data);
    return data;
}

static int slots(void) {
    int total = 0;
    for (int i = 0; i < nr_mixers; i++)
        total += sts_mixer_get_active_voices(&mixers[i].mixer);
    return total;
}

static float render(int chunks) {
    float output[CHUNK_SIZE * 2], peak = 0;
    for (int i = 0; i < chunks; i++) {
        audio_callback(NULL, (Uint8 *)output, sizeof(output));
        for (unsigned j = 0; j < CHUNK_SIZE * 2; j++)
            peak = fmaxf(peak, fabsf(output[j]));
        ticks += 24;
    }
    return peak;
}

static void prepare(int id, const char *name, int group) {
    struct string *s = cstr_to_string(name);
    assert(kiwi_prepare_3(id, s, false));
    assert(kiwi_group(id) == group);
    free_string(s);
}

static void start(int id, const char *name, int group, bool loop) {
    prepare(id, name, group);
    assert(kiwi_loop(id, loop ? 0 : 1));
    assert(kiwi_play(id));
}

static void bgv_test(int group, int default_volume) {
    int volume;
    assert(mixer_get_volume(group, &volume) && volume == default_volume);
    assert(mixer_set_volume(group, 100));
    float peak = render(4);
    assert(peak > 0.01f);
    assert(mixer_set_volume(group, 30));
    render(4);
    assert(fabsf(render(4) - 0.3f * peak) < 0.0001f);
    assert(mixer_set_volume(group, 0));
    render(4);
    assert(render(4) == 0);
    assert(mixer_set_volume(group, 100));
    assert(mixer_set_volume(3, 0));
    render(4);
    assert(fabsf(render(4) - peak) < 0.0001f);
    assert(mixer_set_volume(3, 100));
}

static void foreground_test(int bgv_group) {
    assert(mixer_set_volume(bgv_group, 100));
    float peak = render(4);
    assert(peak > 0.01f);
    assert(mixer_set_volume(bgv_group, 0));
    render(4);
    assert(fabsf(render(4) - peak) < 0.0001f);
    assert(mixer_set_volume(3, 0));
    render(4);
    assert(render(4) == 0);
}

static void escalayer_bgv_test(void) {
    int bgv_group = KiwiSoundEngine_GetBackVoiceGroup();
    assert(bgv_group == 5);
    assert(wav_get_group_num_from_data_num(30690) == 12);
    assert(wav_prepare(12, 30690));
    assert(wav_set_loop_count(12, 0));
    assert(wav_play(12));
    bgv_test(bgv_group, 15);
    assert(wav_unprepare(12));
    assert(wav_prepare(3, 11773));
    assert(wav_set_loop_count(3, 0));
    assert(wav_play(3));
    foreground_test(bgv_group);
    assert(wav_unprepare(3));
    printf("Escalayer Reboot: BGV slider controls WAI group 12, independently of voice group 6\n");
}

static void config_test(bool empty) {
    assert(config.mixer_nr_channels == 14);
    assert(nr_mixers == 14);
    assert(master == &mixers[0]);
    assert(config.mixer_volumes[0] == (empty ? 80 : 50));
    assert(config.mixer_volumes[3] == (empty ? 100 : 80));
    assert(config.mixer_volumes[6] == 53);
    assert(config.mixer_volumes[7] == 59);
    assert(config.mixer_volumes[8] == 0);
    assert(config.mixer_volumes[9] == 100);
    assert(mixers[10].parent == &mixers[3]);
    assert(mixers[11].parent == &mixers[3]);
    assert(mixer_set_volume(10, 53));
    int value = -1;
    assert(mixer_get_volume(10, &value) && value == 53);
    assert(kiwi_mixer_default(10, &value) && value == 80);
    prepare(12, "10001", 11);
    assert(kiwi_play(12));
    assert(render(4) > 0.01f);
    kiwi_stop(12);
    assert(mixer_set_mute(11, 1));
    prepare(12, "10001", 11);
    assert(kiwi_play(12));
    render(4);
    assert(render(4) == 0);
    assert(mixer_set_mute(11, 0));
    prepare(12, "10101", 10);
    assert(kiwi_play(12));
    assert(render(4) > 0.01f);
    assert(mixer_set_volume(10, 0));
    render(4);
    assert(render(4) == 0);
    assert(mixer_set_volume(10, 80));
    assert(render(4) > 0.01f);
    struct string *bad = cstr_to_string("badgroup");
    assert(!kiwi_prepare_3(12, bad, false));
    free_string(bad);
    struct string *missing = cstr_to_string("missing");
    assert(!kiwi_prepare_3(13, missing, false));
    free_string(missing);
    assert(!kiwi_exists(13));
    printf("EX mixers=14; INI preserved=%d; groups 10/11 render, mute, volume and invalid routing OK\n", !empty);
}

static void lifecycle_test(void) {
    int baseline = slots(), peak = baseline;
    struct string *se = cstr_to_string("se");
    kiwi_set_se(5, 33, false);
    for (int scene = 0; scene < 40; scene++) {
        int bgm = 1 + scene % 2;
        start(bgm, "bgm", 1, true);
        kiwi_fade(3 - bgm, 50, 0, true, 0);
        for (int i = 0; i < 3; i++)
            start(3 + i * 2 + scene % 2, "se", 2, true);
        for (int id = 20; id <= 24; id++)
            start(id, "ambient", 12, false);
        for (int message = 0; message < 25; message++) {
            int se_id = 10 + message % 2;
            start(se_id, "se", 2, false);
            kiwi_fade(se_id, 0, 0, false, -1);
            kiwi_fade(se_id, 20, 1, false, 0);
            // SYS_PlayVoice: Prepare(12), Play(12); next message can replace live audio.
            prepare(12, message % 2 ? "10001" : "10101", message % 2 ? 11 : 10);
            struct channel *voice = id_pool_get(&sounds, 12);
            assert(!voice->fade.pending && !voice->fade.fading);
            assert(voice->volume == 100 && !voice->paused);
            assert(kiwi_play(12));
            // BeginBGMFade ducks BGM and looping SE channels, never master or voice.
            kiwi_fade(bgm, 100, 0.6f, false, 5);
            for (int i = 0; i < 3; i++)
                kiwi_fade(3 + i * 2 + scene % 2, 100, 0.6f, false, 5);
            assert(kiwi_play_se(5, se));
            peak = max(peak, slots());
            assert(slots() <= baseline + 20);
            assert(render(4) > 0.01f);
            if (message % 5 == 0) {
                assert(kiwi_pause(12));
                assert(kiwi_stop(12));
            } else if (message % 5 == 1) {
                assert(kiwi_fade(12, 30, 0, true, 0));
                render(1);
            } else if (message % 5 == 2) {
                render(30);
                assert(!kiwi_playing(12));
                assert(sts_mixer_get_active_voices(&mixers[voice->mixer_no].mixer) == 0);
            }
            // UpdateSoundShuffle re-prepares finished ambient handles, not new IDs.
            for (int id = 20; id <= 24; id++)
                if (!kiwi_playing(id)) start(id, "ambient", 12, false);
        }
        // SYS_Sound_Stop stops voices, looping SE and background channels.
        for (int id = 1; id <= 24; id++) kiwi_stop(id);
        kiwi_stop_se(5);
        assert(slots() == baseline);
        assert(kiwi_exists(12));
        assert(kiwi_unprepare(12));
        assert(!kiwi_exists(12));
    }
    free_string(se);
    printf("40 scenes / 1000 voices: baseline slots=%d peak=%d; every scene returns to baseline\n", baseline, peak);
}

static void exhaustion_test(void) {
    int baseline = slots();
    for (int i = 0; i < STS_MIXER_VOICES; i++) start(100 + i, "10001", 11, true);
    prepare(12, "10001", 11);
    assert(!kiwi_play(12));
    assert(!kiwi_playing(12));
    assert(sts_mixer_get_active_voices(&mixers[11].mixer) == STS_MIXER_VOICES);
    assert(kiwi_pause(100));
    assert(kiwi_unprepare(100));
    assert(kiwi_play(12));
    assert(kiwi_stop(12));
    for (int i = 1; i < STS_MIXER_VOICES; i++) kiwi_unprepare(100 + i);
    assert(slots() == baseline);
    // Pending and active stop-fades belong to the channel, not its handle.
    for (int i = 0; i < 50; i++) {
        prepare(12, "10001", 11);
        kiwi_fade(12, 0, 0, false, -1);
        kiwi_fade(12, 100, 1, false, 0);
        assert(((struct channel *)id_pool_get(&sounds, 12))->fade.pending);
        prepare(12, "10001", 11);
        assert(((struct channel *)id_pool_get(&sounds, 12))->volume == 100);
        kiwi_play(12);
        kiwi_fade(12, 20, 0, true, 0);
        prepare(12, "10101", 10);
        kiwi_play(12);
        assert(render(6) > 0.01f);
        assert(kiwi_playing(12));
        kiwi_stop(12);
        assert(slots() == baseline);
    }
    start(12, "10001", 11, true);
    kiwi_fade(12, 20, 0, true, 0);
    render(8);
    assert(!kiwi_playing(12));
    assert(slots() == baseline);
    printf("capacity=%d: overflow returns false; paused close, EOF, fade-stop and replacement release slots\n", STS_MIXER_VOICES);
}

static void pressure_test(void) {
    int baseline = slots();
    struct string *name = cstr_to_string("10001");
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int i = 0; i < 40; i++) {
            start(100 + i, "master", 0, true);
            start(140 + i, "se", 2, true);
            start(180 + i, "ambient", 12, true);
            start(220 + i, "10001", 11, true);
        }
        for (int id = 100; id < 260; id++)
            assert(kiwi_fade(id, 0, 0, false, 0));
        assert(sts_mixer_get_active_voices(&master->mixer) == 43);
        assert(sts_mixer_get_active_voices(&mixers[3].mixer) == 10);
        assert(sts_mixer_get_active_voices(&mixers[11].mixer) == 40);
        assert(slots() == baseline + 160);
        assert(kiwi_prepare_3(12, name, false));
        assert(kiwi_loop(12, 0));
        assert(kiwi_play(12));
        assert(sts_mixer_get_active_voices(&mixers[11].mixer) == 41);
        assert(render(6) > 0.01f);
        assert(mixer_set_mute(11, 1));
        render(4);
        assert(render(4) == 0);
        assert(mixer_set_mute(11, 0));
        assert(render(4) > 0.01f);
        assert(kiwi_unprepare(12));
        for (int id = 100; id < 260; id++) assert(kiwi_unprepare(id));
        render(4);
        assert(render(4) == 0);
        assert(slots() == baseline);
        assert(sts_mixer_get_active_voices(&master->mixer) == 3);
        assert(sts_mixer_get_active_voices(&mixers[3].mixer) == 10);
    }
    free_string(name);
    printf("160 concurrent streams across 4 groups + voice 11 audible; master=43, voice11=41; 3 cycles, parent slots unchanged\n");
}

static int complete_stream(sts_mixer_sample_t *sample, void *data) {
    return STS_STREAM_COMPLETE;
}

static void link_limit_test(const char *mode) {
    assert(config.mixer_nr_channels == STS_MIXER_VOICES);
    assert(nr_mixers == STS_MIXER_VOICES + !strcmp(mode, "limit-implicit"));
    for (int i = 0; i < nr_mixers; i++) {
        if (&mixers[i] == master) continue;
        assert(mixers[i].voice >= 0);
        sts_mixer_voice_t *voice = &mixers[i].parent->mixer.voices[mixers[i].voice];
        assert(voice->state == STS_MIXER_VOICE_STREAMING);
        assert(voice->stream == &mixers[i].stream);
    }
    if (!strcmp(mode, "limit-flat") || !strcmp(mode, "limit-implicit")) {
        assert(sts_mixer_get_active_voices(&master->mixer) == nr_mixers - 1);
    } else {
        assert(sts_mixer_get_active_voices(&master->mixer) == 3);
        assert(sts_mixer_get_active_voices(&mixers[3].mixer) == nr_mixers - 4);
    }
    start(12, "last", STS_MIXER_VOICES - 1, true);
    assert(render(6) > 0.01f);
    assert(kiwi_unprepare(12));
    assert(slots() == nr_mixers - 1);
    printf("%s: %d configured groups, all %d parent links active, last group renders audible PCM\n",
        mode, STS_MIXER_VOICES, nr_mixers - 1);
}

static void bounds_test(void) {
    sts_mixer_t mixer;
    memset(&mixer, 0xff, sizeof(mixer));
    sts_mixer_init(&mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
    float value = 0.1f, output[4];
    sts_mixer_sample_t sample = {.length = 1, .frequency = 44100,
        .audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT, .data = &value};
    assert(!mixer.voice_count);
    sts_mixer_mix_audio(&mixer, output, 2);
    for (int i = 0; i < 4; i++) assert(output[i] == 0);
    for (int i = 0; i < 40; i++)
        assert(sts_mixer_play_sample(&mixer, &sample, 1, 1, 0) == i);
    assert(mixer.voice_count == 40);
    sts_mixer_stop_voice(&mixer, 39);
    assert(mixer.voice_count == 39);
    sts_mixer_stop_voice(&mixer, 3);
    assert(mixer.voice_count == 39);
    assert(sts_mixer_play_sample(&mixer, &sample, 1, 1, 0) == 3);
    sts_mixer_stop_sample(&mixer, &sample);
    assert(!mixer.voice_count);
    for (int i = 0; i < 40; i++) sts_mixer_play_sample(&mixer, &sample, 1, 1, 0);
    sts_mixer_mix_audio(&mixer, output, 2);
    assert(output[0] > 0);
    assert(!mixer.voice_count);
    sts_mixer_stream_t stream = {.callback = complete_stream};
    for (int i = 0; i < 40; i++)
        assert(sts_mixer_play_stream(&mixer, &stream, 1) == i);
    sts_mixer_stop_stream(&mixer, &stream);
    assert(!mixer.voice_count);
    for (int i = 0; i < 40; i++) sts_mixer_play_stream(&mixer, &stream, 1);
    sts_mixer_mix_audio(&mixer, output, 2);
    assert(!mixer.voice_count);
    assert(sts_mixer_play_sample(&mixer, &sample, 1, 1, 0) == 0);
    sts_mixer_shutdown(&mixer);
    assert(!mixer.voice_count && !sts_mixer_get_active_voices(&mixer));
    printf("active scan bound: empty, holes, reuse, sample/stream EOF, bulk stop and shutdown OK\n");
    printf("capacity=%d voice_bytes=%zu mixer_bytes=%zu extra_slot_bytes_for_14_mixers=%zu\n",
        STS_MIXER_VOICES, sizeof(sts_mixer_voice_t), sizeof(sts_mixer_t),
        (STS_MIXER_VOICES - 32) * sizeof(sts_mixer_voice_t) * 14);
}

static void authored_groups_test(void) {
    int baseline = slots();
    assert(config.mixer_nr_channels == STS_MIXER_VOICES);
    assert(nr_mixers == STS_MIXER_VOICES + 1);
    assert(master == &mixers[config.mixer_nr_channels]);
    assert(mixers[255].parent == &mixers[3]);
    prepare(12, "last", 255);
    struct channel *ch = id_pool_get(&sounds, 12);
    assert(!channel_set_group(ch, -1));
    assert(!channel_set_group(ch, 256));
    assert(!channel_set_group(ch, 257));
    assert(!channel_set_group(ch, 1023));
    assert(channel_get_group(ch) == 255);
    assert(kiwi_loop(12, 0));
    assert(kiwi_play(12));
    assert(render(6) > 0.01f);
    assert(mixer_set_mute(3, 1));
    render(4);
    assert(render(4) == 0);
    assert(mixer_set_mute(3, 0));
    assert(render(4) > 0.01f);
    assert(kiwi_unprepare(12));
    render(4);
    static const char *const invalid[] = {"skipped", "past-skipped", "after-master", "negative"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        struct string *name = cstr_to_string(invalid[i]);
        assert(!kiwi_prepare_3(12, name, false));
        assert(!kiwi_exists(12));
        assert(!kiwi_play(12));
        assert(slots() == baseline);
        assert(render(4) == 0);
        free_string(name);
    }
    printf("implicit master=256: authored group 255 renders and obeys Voice mute; groups -1/256/257/1023 fail Prepare without audio or slot leaks\n");
}

int main(int argc, char **argv) {
    assert(argc == 5);
    wave_path = argv[3];
    config.default_volume = 80;
    game_escalayer_reboot = !strcmp(argv[4], "escalayer");
    game_tsumamigui3 = !strcmp(argv[4], "tsumamigui3");
    if (game_escalayer_reboot) {
        const char *names[18] = {SJIS_MASTER, "BGM", "SE", SJIS_VOICE, "Ambient", "BackVoice"};
        for (int i = 6; i < 18; i++) names[i] = "Character";
        set_default_mixers(names, NULL, 18);
        config.mixer_volumes[5] = 15;
        config.wai_path = argv[2];
        audio_init();
        escalayer_bgv_test();
        return 0;
    } else if (game_tsumamigui3) {
        sys4_set_text_gbk(true);
    } else if (!strcmp(argv[4], "linkfail")) {
        const char *names[STS_MIXER_VOICES + 2];
        names[0] = "Master";
        for (unsigned i = 1; i < STS_MIXER_VOICES + 2; i++) names[i] = "Group";
        set_default_mixers(names, NULL, STS_MIXER_VOICES + 2);
    } else if (!strcmp(argv[4], "limit-implicit")) {
        static const char *const names[] = {"Music", "Sound", "Effects", "Speech", "Ambient", "BackVoice"};
        set_default_mixers(names, NULL, 6);
    } else if (!strcmp(argv[4], "authored-implicit")) {
        static const char *const names[] = {"Music", "Sound", "SE", "Voice", "Ambient", "BackVoice"};
        set_default_mixers(names, NULL, 6);
    } else if (strcmp(argv[4], "empty") && strcmp(argv[4], "limit-empty") && strcmp(argv[4], "legacy")) {
        static const char *const names[] = {"Master", "BGM", "SE", "Voice", "Ambient", "BackVoice"};
        static const int volumes[] = {50, 80, 80, 80, 80, 80};
        set_default_mixers(names, volumes, 6);
    }
    game_evenicle2 = !strcmp(argv[4], "limit-flat");
    config.ex_path = argv[1];
    kiwi_prelink();
    kiwi_init();
    sound_metas = calloc(1, sizeof(*sound_metas));
    sound_metas[0].ex = ex_read_file(argv[2]);
    assert(sound_metas[0].ex);
    sound_metas[0].info = ex_get_table(sound_metas[0].ex, "SoundInfo");
    nr_sound_metas = 1;
    kiwi_build_sound_row_index();
    if (game_tsumamigui3) {
        assert(config.mixer_nr_channels == 14);
        start(12, "30031", 10, true);
        bgv_test(5, 18);
        assert(kiwi_unprepare(12));
        start(3, "20580", 6, true);
        foreground_test(5);
        assert(kiwi_unprepare(3));
        printf("Tsumamigui 3: BGV slider controls SoundInfo group 10, independently of voice group 6\n");
    } else if (!strcmp(argv[4], "life")) lifecycle_test();
    else if (!strcmp(argv[4], "full")) exhaustion_test();
    else if (!strcmp(argv[4], "pressure")) pressure_test();
    else if (!strcmp(argv[4], "bounds")) bounds_test();
    else if (!strcmp(argv[4], "authored-implicit")) authored_groups_test();
    else if (!strcmp(argv[4], "legacy")) {
        assert(config.mixer_nr_channels == 0 && nr_mixers == 3);
        prepare(12, "master", 0);
        assert(channel_set_group(id_pool_get(&sounds, 12), 2));
        assert(kiwi_unprepare(12));
        printf("legacy defaults: unconfigured Music/Sound/Master groups remain assignable\n");
    }
    else if (!strncmp(argv[4], "limit-", 6)) link_limit_test(argv[4]);
    else if (!strcmp(argv[4], "invalid") || !strcmp(argv[4], "absent")) {
        assert(config.mixer_nr_channels == 6);
        printf("absent/invalid EX groups leave configured mixers unchanged\n");
    } else config_test(!strcmp(argv[4], "empty"));
    kiwi_fini();
    assert(slots() == nr_mixers - 1);
    return 0;
}
'''


class EvenicleAudioTests(unittest.TestCase):
    def run_probe(self, mode, invalid=False):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            groups = directory / "groups.ex"
            metadata = directory / "timemap.ex"
            audio = directory / "audio.wav"
            volumes = [0.8, 0.8, 0.8, 1.0, 0.8, 0.8, 0.53, 0.59, -0.2, 1.5, 0.8, 0.8, 0.8, 0.8]
            rows = [(-1, 0.8), (256, 0.8), (1023, 0.8), (1024, 0.8)] if invalid else list(enumerate(volumes))
            if mode.startswith("limit-") or mode == "authored-implicit":
                rows = [(255, 0.8), (256, 0.8), (1023, 0.8)]
            if mode == "tsumamigui3":
                rows = list(enumerate([0.8, 0.8, 0.8, 1.0, 0.8, 0.177828] + [0.8] * 8))
            encoding = "cp936" if mode == "tsumamigui3" else "cp932"
            groups.write_bytes(build_extf([] if mode in ("absent", "legacy") else [group_table(rows, encoding)]))
            metadata.write_bytes(build_extf([_ex_soundinfo_table(
                [(b"10001", 11), (b"10101", 10), (b"bgm", 1),
                 (b"se", 2), (b"ambient", 12), (b"master", 0), (b"badgroup", 100), (b"last", 255),
                 (b"skipped", 256), (b"past-skipped", 1023), (b"after-master", 257), (b"negative", -1),
                 (b"30031", 10), (b"20580", 6)],
                [(b"Name", 3), (b"Group", 1)],
            )]))
            if mode == "escalayer":
                metadata = directory / "sounds.wai"
                # WAI v4 uses 16-byte records indexed by one-based sound ID.
                wai = bytearray(40 + 30690 * 16)
                struct.pack_into("<4sIII", wai, 0, b"XI2\0", 1, 30691, 4)
                for sound, group in [(11773, 6), (30690, 12)]:
                    struct.pack_into("<I", wai, 40 + (sound - 1) * 16 + 8, group)
                metadata.write_bytes(wai)
            with wave.open(str(audio), "wb") as stream:
                stream.setparams((1, 2, 44100, 22050, "NONE", "not compressed"))
                stream.writeframes(struct.pack("<h", 8192) * 22050)
            output = compile_and_run(
                HARNESS,
                [*LIBSYS4_CORE, ROOT / "src/id_pool.c", ROOT / "src/audio_meta.c"],
                ["-lz", "-lm", "-lsndfile"],
                [str(groups), str(metadata), str(audio), mode],
            )
            print(output, end="", flush=True)

    def test_ex_extends_ini_and_voice_volume_routing(self):
        self.run_probe("groups")

    def test_ex_without_ini(self):
        self.run_probe("empty")

    def test_tsumamigui3_background_voice_slider(self):
        self.run_probe("tsumamigui3")

    def test_escalayer_reboot_background_voice_slider(self):
        self.run_probe("escalayer")

    def test_invalid_ex_groups(self):
        self.run_probe("invalid", invalid=True)

    def test_absent_ex_groups(self):
        self.run_probe("absent")

    def test_realistic_message_and_scene_lifecycle(self):
        self.run_probe("life")

    def test_capacity_and_replaced_fades(self):
        self.run_probe("full")

    def test_high_concurrency_voice_and_parent_slots(self):
        self.run_probe("pressure")

    def test_active_voice_scan_bounds(self):
        self.run_probe("bounds")

    def test_ex_group_limit_nested(self):
        self.run_probe("limit-nested")

    def test_ex_group_limit_flat(self):
        self.run_probe("limit-flat")

    def test_ex_group_limit_implicit_master(self):
        self.run_probe("limit-implicit")

    def test_ex_group_limit_without_ini(self):
        self.run_probe("limit-empty")

    def test_parent_link_failure_is_fatal(self):
        with self.assertRaisesRegex(AssertionError, "Failed to link audio mixer 257 .*stream capacity 256 exhausted"):
            self.run_probe("linkfail")
        print("oversized existing configuration: parent link failure is fatal, not silent", flush=True)

    def test_implicit_master_is_not_an_authored_group(self):
        self.run_probe("authored-implicit")

    def test_legacy_default_groups(self):
        self.run_probe("legacy")


if __name__ == "__main__":
    unittest.main()
