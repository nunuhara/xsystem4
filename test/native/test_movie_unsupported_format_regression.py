import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_CORE = [
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
]


def compile_and_run(c_source: str, extra_sources=None, extra_flags=None, run_args=None) -> str:
    extra_sources = extra_sources or []
    extra_flags = extra_flags or []
    run_args = run_args or []
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = Path(tmpdir) / "test.c"
        bin_path = Path(tmpdir) / "test_bin"
        src_path.write_text(c_source, encoding="utf-8")
        cmd = [
            "cc",
            "-std=c11",
            "-D_DEFAULT_SOURCE",
            "-Wall",
            "-Wextra",
            "-Wno-unused-function",
            "-Wno-unused-parameter",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "subprojects/libsys4/include"),
            str(src_path),
            *(str(p) for p in extra_sources),
            "-o", str(bin_path),
            *extra_flags,
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise AssertionError(f"Compile failed:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")
        run = subprocess.run([str(bin_path), *run_args], capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(
                f"Run failed (rc={run.returncode}):\nSTDOUT:\n{run.stdout}\nSTDERR:\n{run.stderr}"
            )
        return run.stdout


def extract_function(rel_path: str, name: str) -> str:
    src = (ROOT / rel_path).read_text(encoding="utf-8")
    idx = -1
    for pat in (
        f"struct movie_context *{name}(",
        f"static bool {name}(",
        f"void {name}(",
        f"bool {name}(",
        f"int {name}(",
        f"uint32_t {name}(",
    ):
        idx = src.find(pat)
        if idx >= 0:
            break
    if idx < 0:
        raise AssertionError(f"Function {name} not found in {rel_path}")
    brace = src.index("{", idx)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[idx : i + 1]
    raise AssertionError(f"Unclosed function {name} in {rel_path}")


class MovieUnsupportedFormatRegressionTest(unittest.TestCase):
    def test_unsupported_container_gracefully_skips_without_error_or_crash(self):
        # Extract the exact movie_plmpeg.c functions and test them against real pl_mpeg.h.
        fns = "\n\n".join([
            extract_function("src/movie_plmpeg.c", "movie_context_is_playing"),
            extract_function("src/movie_plmpeg.c", "movie_load"),
            extract_function("src/movie_plmpeg.c", "movie_free"),
            extract_function("src/movie_plmpeg.c", "movie_play"),
            extract_function("src/movie_plmpeg.c", "movie_play_at"),
            extract_function("src/movie_plmpeg.c", "movie_draw"),
            extract_function("src/movie_plmpeg.c", "movie_is_end"),
            extract_function("src/movie_plmpeg.c", "movie_get_position"),
            extract_function("src/movie_plmpeg.c", "movie_get_width"),
            extract_function("src/movie_plmpeg.c", "movie_get_height"),
            extract_function("src/movie_plmpeg.c", "movie_set_volume"),
            extract_function("src/movie_plmpeg.c", "movie_skip_one"),
            extract_function("src/movie_plmpeg.c", "movie_has_active_playback"),
            extract_function("src/movie_plmpeg.c", "movie_get_playback_seq"),
            extract_function("src/movie_plmpeg.c", "movie_skip_active"),
        ])

        harness = f"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

typedef unsigned int GLuint;
typedef void SDL_mutex;
#define STS_MIXER_SAMPLE_FORMAT_FLOAT 1
typedef struct {{
    void *userdata;
    void (*callback)(void *, void *);
    struct {{
        int frequency;
        int audio_format;
    }} sample;
}} sts_mixer_stream_t;
struct sact_sprite {{ int id; }};
#define GL_TEXTURE0 0
#define GL_TEXTURE1 1
#define GL_TEXTURE2 2
#define GL_DRAW_FRAMEBUFFER 0
#define GFX_RECTANGLE 0
typedef float mat4[4][4];
#define WORLD_TRANSFORM(w, h, x, y) {{{{ (w), (h), (x), (y) }}}}
#define WV_TRANSFORM(w, h) {{{{ (w), (h), 0, 0 }}}}
typedef struct {{ unsigned int program; }} Shader;
struct texture {{ int w; int h; }};
struct gfx_render_job {{
    Shader *shader;
    int shape;
    int texture;
    float *world_transform;
    float *view_transform;
    void *data;
}};
static Shader movie_shader = {{ 0 }};
static struct {{ int view_width; int view_height; }} config = {{ 1280, 720 }};
struct apeg_context {{
    int width;
    int height;
    int end_time_ms;
    int sound_id;
    bool has_audio;
    bool has_sond_chunk;
    bool playing;
    bool has_manual_time;
    bool has_rendered_frame;
    bool eof;
    int start_time_ms;
    uint32_t start_ticks_ms;
    int current_time_ms;
    int seeked_time_ms;
    FILE *fp;
    plm_video_t *vdec;
    uint8_t *chunk_buf;
    void *keyframes;
}};

struct movie_context {{
    plm_t *plm;
    struct apeg_context *apeg;
    SDL_mutex *decoder_mutex;
    plm_frame_t *pending_video_frame;
    GLuint textures[3];
    sts_mixer_stream_t sts_stream;
    int voice;
    int volume;
    bool plm_playing;
    bool skipped;
    bool unsupported;
    double stream_time;
    uint32_t wall_time_ms;
    SDL_mutex *timer_mutex;
}};

static struct movie_context **active_movies = NULL;
static int nr_active_movies = 0;
static uint32_t movie_playback_seq = 0;
static int gl_reset_unreg_calls = 0;
static int mixer_stop_calls = 0;
static int mixer_play_calls = 0;
static int gl_tex_init_calls = 0;
static int mutex_create_calls = 0;
static int mutex_lock_calls = 0;
static char g_base_dir[1024];

static char *gamedir_path_icase(const char *rel) {{
    char *full = path_join(g_base_dir, rel);
    if (file_exists(full))
        return full;
    free(full);
    return NULL;
}}

static void register_movie(struct movie_context *mc) {{ (void)mc; }}
static void unregister_movie(struct movie_context *mc) {{ (void)mc; }}
static void init_movie_textures(struct movie_context *mc) {{ (void)mc; gl_tex_init_calls++; }}
static void movie_mc_on_gl_reset(void *user) {{ (void)user; }}
static void gfx_unregister_reset_callback(void (*cb)(void *), void *user) {{ (void)cb; (void)user; gl_reset_unreg_calls++; }}
static bool glIsTexture(GLuint t) {{ return t != 0; }}
static void glDeleteTextures(int n, const GLuint *t) {{ (void)n; (void)t; assert(!"glDeleteTextures must not run for unsupported stub"); }}
static SDL_mutex *SDL_CreateMutex(void) {{ mutex_create_calls++; return (SDL_mutex *)0x1; }}
static void SDL_DestroyMutex(SDL_mutex *m) {{ (void)m; assert(!"SDL_DestroyMutex must not run for unsupported stub"); }}
static int SDL_LockMutex(SDL_mutex *m) {{ assert(m != NULL); mutex_lock_calls++; return 0; }}
static int SDL_UnlockMutex(SDL_mutex *m) {{ assert(m != NULL); return 0; }}
static uint32_t SDL_GetTicks(void) {{ return 1234; }}
static void mixer_stream_stop(int voice) {{ (void)voice; mixer_stop_calls++; }}
static void mixer_stream_stop_stream(sts_mixer_stream_t *s) {{ if (s && s->callback) mixer_stop_calls++; }}
static int mixer_stream_play(sts_mixer_stream_t *s, int vol) {{ (void)s; (void)vol; mixer_play_calls++; return 1; }}
static bool mixer_stream_set_volume(int voice, int vol) {{ (void)voice; (void)vol; return true; }}
static bool mixer_stream_set_volume_stream(sts_mixer_stream_t *s, int vol) {{ (void)s; (void)vol; return true; }}
static void audio_callback(void *sample, void *userdata) {{ (void)sample; (void)userdata; }}
static void kiwi_stop_sound(int id) {{ (void)id; }}
static void kiwi_play_sound(int id) {{ (void)id; }}
static void kiwi_seek_sound(int id, int ms) {{ (void)id; (void)ms; }}
static void kiwi_unprepare_sound(int id) {{ (void)id; }}
static void apeg_update_clock(struct apeg_context *a) {{ (void)a; }}
static void apeg_seek_to(struct movie_context *mc, int ms) {{ (void)mc; (void)ms; }}
static plm_frame_t *apeg_decode_next_frame(struct apeg_context *a, int ms) {{ (void)a; (void)ms; return NULL; }}
static void load_movie_shader(void) {{ movie_shader.program = 1; }}
static void movie_alloc_yuv_textures(struct movie_context *mc) {{ (void)mc; }}
static void update_texture(int unit, GLuint tex, plm_plane_t *p) {{ (void)unit; (void)tex; (void)p; }}
static void glActiveTexture(int unit) {{ (void)unit; }}
static struct texture *sprite_get_texture(struct sact_sprite *sp) {{ (void)sp; static struct texture t = {{ 1280, 720 }}; return &t; }}
static GLuint gfx_set_framebuffer(int target, struct texture *t, int x, int y, float w, float h) {{ (void)target; (void)t; (void)x; (void)y; (void)w; (void)h; return 0; }}
static void gfx_reset_framebuffer(int target, GLuint fbo) {{ (void)target; (void)fbo; }}
static void sprite_dirty(struct sact_sprite *sp) {{ (void)sp; }}
static void gfx_clear(void) {{}}
static void gfx_render(struct gfx_render_job *job) {{ (void)job; assert(!"gfx_render must not run for unsupported stub"); }}
static void gfx_swap(void) {{}}
bool movie_play_at(struct movie_context *mc, int time_ms);
bool movie_is_end(struct movie_context *mc);
int movie_get_position(struct movie_context *mc);

{fns}

int main(int argc, char **argv) {{
    assert(argc == 2);
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", argv[1]);

    // 1. NULL, empty, missing file, and directory must still return NULL.
    assert(movie_load(NULL) == NULL);
    assert(movie_load("") == NULL);
    assert(movie_load("Data/Movie/DoesNotExist.alm") == NULL);
    assert(movie_load("Data/Movie") == NULL);

    // 2. Existing ASF/WMV file (like Rance 9 CN Opening.alm) returns an immediately-ended skip stub.
    struct movie_context *mc = movie_load("Data/Movie/Opening.alm");
    assert(mc != NULL);
    assert(mc->unsupported == true);
    assert(mc->skipped == true);
    assert(mc->plm == NULL);
    assert(mc->decoder_mutex == NULL && mc->timer_mutex == NULL);
    assert(gl_tex_init_calls == 0 && mutex_create_calls == 0);

    uint32_t seq_before = movie_get_playback_seq();
    assert(movie_set_volume(mc, 80) == true);
    assert(mc->volume == 80);
    assert(movie_play(mc) == true);
    assert(movie_play_at(mc, 500) == true);
    assert(movie_get_playback_seq() == seq_before);
    assert(movie_has_active_playback() == false);
    assert(movie_skip_active() == false);
    assert(movie_is_end(mc) == true);
    assert(movie_get_position(mc) == 0);
    assert(movie_get_width(mc) == 0);
    assert(movie_get_height(mc) == 0);
    assert(mutex_lock_calls == 0);
    struct sact_sprite dummy_sp = {{ .id = 7 }};
    assert(movie_draw(mc, &dummy_sp) == true);
    assert(movie_draw(mc, NULL) == false);
    movie_free(mc);
    assert(mixer_play_calls == 0 && mixer_stop_calls == 0);

    // 3. Existing 0-byte dummy movie file also skips cleanly without error.
    struct movie_context *empty_mc = movie_load("Data/Movie/Empty.alm");
    assert(empty_mc != NULL);
    assert(empty_mc->unsupported == true);
    assert(movie_play(empty_mc) == true);
    assert(movie_is_end(empty_mc) == true);
    assert(movie_draw(empty_mc, &dummy_sp) == true);
    movie_free(empty_mc);

    puts("OK");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            movie_dir = Path(tmpdir) / "Data" / "Movie"
            movie_dir.mkdir(parents=True)
            # Authentic ASF/WMV header magic matching Rance 9 CN Opening.alm.
            asf_header = bytes.fromhex("3026b2758e66cf11a6d900aa0062ce6c") + b"\x00" * 512
            (movie_dir / "Opening.alm").write_bytes(asf_header)
            (movie_dir / "Empty.alm").write_bytes(b"")

            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], [tmpdir])
            self.assertEqual(out.strip(), "OK")

        # Also assert movie_draw guards mc->unsupported in source.
        src = (ROOT / "src/movie_plmpeg.c").read_text(encoding="utf-8")
        self.assertIn("if (mc->unsupported || mc->skipped)", src)
        self.assertIn("plm_get_num_video_streams(mc->plm) <= 0", src)


if __name__ == "__main__":
    unittest.main()
