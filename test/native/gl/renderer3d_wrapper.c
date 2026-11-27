#include <assert.h>
#include <math.h>
#include "../../../src/3d/renderer.c"

enum RE_plugin_version re_plugin_version;

static uint8_t test_bb_pixels[16] = {
    255, 0, 0, 255,   255, 0, 0, 255,
    255, 0, 0, 255,   255, 0, 0, 255,
};
static struct cg test_bb_cg = {
    .metrics = { .w = 2, .h = 2, .has_alpha = true },
    .pixels = test_bb_pixels,
};

struct cg *asset_cg_load(int no)
{
    return no == 77 ? &test_bb_cg : NULL;
}

GLuint test_load_3d_shader(const char *vertex, const char *fragment, int engine)
{
    re_plugin_version = engine;
    return load_shader(vertex, fragment, (enum RE_plugin_version)engine);
}

int test_bone_texture_unit(void)
{
    return BONE_TEXTURE_UNIT;
}

static void test_init_identity_bones(mat3x4 *bones, int count)
{
    static const mat3x4 identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
    };
    for (int i = 0; i < count; i++)
        memcpy(bones[i], identity, sizeof(mat3x4));
}

static GLuint test_create_bone_texture_from_array(const mat3x4 *bones, int nr_bones)
{
    // Mirrors init_instance_bone_texture() in reign.c.
    mat3x4 init_data[MAX_BONES];
    test_init_identity_bones(init_data, MAX_BONES);
    if (bones && nr_bones > 0) {
        int n = nr_bones > MAX_BONES ? MAX_BONES : nr_bones;
        memcpy(init_data, bones, (size_t)n * sizeof(mat3x4));
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, MAX_BONES * 3, 1, 0, GL_RGBA, GL_FLOAT, init_data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR ? tex : 0;
}

GLuint test_create_bone_texture(void)
{
    GLuint tex = test_create_bone_texture_from_array(NULL, 0);
    if (!tex)
        return 0;
    float bone[12] = { 1, 0, 0, 0.5f,  0, 1, 0, 0,  0, 0, 1, 0 };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (MAX_BONES - 1) * 3, 0, 3, 1, GL_RGBA, GL_FLOAT, bone);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR ? tex : 0;
}

static void make_quad_mesh(struct mesh *m, float x0, float x1, float z0, float z1, float y,
                           bool indexed, bool reverse_winding, uint32_t flags, bool hidden)
{
    memset(m, 0, sizeof(*m));
    m->flags = flags;
    m->hidden = hidden;
    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);
    glGenBuffers(1, &m->attr_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, m->attr_buffer);

    if (indexed) {
        const GLfloat verts[4 * 3] = {
            x0, y, z0,
            x1, y, z0,
            x1, y, z1,
            x0, y, z1,
        };
        const GLushort idx_ccw[6] = { 0, 1, 2, 0, 2, 3 };
        const GLushort idx_cw[6]  = { 0, 2, 1, 0, 3, 2 };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(VATTR_POS);
        glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (const void *)0);
        glGenBuffers(1, &m->index_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->index_buffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx_ccw), reverse_winding ? idx_cw : idx_ccw, GL_STATIC_DRAW);
        m->nr_vertices = 4;
        m->nr_indices = 6;
    } else {
        const GLfloat verts_ccw[6 * 3] = {
            x0, y, z0,  x1, y, z0,  x1, y, z1,
            x0, y, z0,  x1, y, z1,  x0, y, z1,
        };
        const GLfloat verts_cw[6 * 3] = {
            x0, y, z0,  x1, y, z1,  x1, y, z0,
            x0, y, z0,  x0, y, z1,  x1, y, z1,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts_ccw), reverse_winding ? verts_cw : verts_ccw, GL_STATIC_DRAW);
        glEnableVertexAttribArray(VATTR_POS);
        glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (const void *)0);
        m->nr_vertices = 6;
        m->nr_indices = 0;
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void free_quad_mesh(struct mesh *m)
{
    glDeleteVertexArrays(1, &m->vao);
    glDeleteBuffers(1, &m->attr_buffer);
    if (m->index_buffer)
        glDeleteBuffers(1, &m->index_buffer);
}

void test_height_detector_gles3(void)
{
    struct RE_renderer *r = RE_renderer_new(RE_SEAL_PLUGIN);
    RE_renderer_set_viewport_size(r, 64, 64);

    struct mesh meshes[4];
    // Mesh 0: indexed ground plane at y = 3.5 across [-10, 10] x [-10, 10]
    make_quad_mesh(&meshes[0], -10.0f, 10.0f, -10.0f, 10.0f, 3.5f, true, false, 0, false);
    // Mesh 1: non-indexed elevated platform at y = 7.0 across [0, 8] x [0, 8] with reversed winding
    make_quad_mesh(&meshes[1], 0.0f, 8.0f, 0.0f, 8.0f, 7.0f, false, true, 0, false);
    // Mesh 2: MESH_NO_HEIGHT_DETECTION roof at y = 9.0 across [-8, -2] x [-8, -2] (must be skipped)
    make_quad_mesh(&meshes[2], -8.0f, -2.0f, -8.0f, -2.0f, 9.0f, false, false, MESH_NO_HEIGHT_DETECTION, false);
    // Mesh 3: hidden roof at y = 9.5 across [-8, -2] x [2, 8] (must be skipped)
    make_quad_mesh(&meshes[3], -8.0f, -2.0f, 2.0f, 8.0f, 9.5f, true, false, 0, true);

    struct model model = {
        .nr_meshes = 4,
        .meshes = meshes,
        .aabb = { { -10.0f, 0.0f, -10.0f }, { 10.0f, 10.0f, 10.0f } },
    };

    // Set up distinct GL_DRAW_FRAMEBUFFER and GL_READ_FRAMEBUFFER plus hostile GL state.
    GLint saved_draw_fbo = 0, saved_read_fbo = 0, saved_vp[4], saved_scissor_box[4];
    GLboolean saved_blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_read_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_vp);
    glGetIntegerv(GL_SCISSOR_BOX, saved_scissor_box);

    GLuint draw_fbo = 0, read_fbo = 0;
    glGenFramebuffers(1, &draw_fbo);
    glGenFramebuffers(1, &read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    glViewport(3, 5, 17, 19);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glActiveTexture(GL_TEXTURE2);
    assert(glGetError() == GL_NO_ERROR);

    struct height_detector *hd = RE_renderer_create_height_detector(r, &model);
    assert(hd != NULL);
    assert(glGetError() == GL_NO_ERROR);

    // Verify both FBO bindings and all hostile GL states were preserved.
    GLint got_draw_fbo = 0, got_read_fbo = 0, got_vp[4], got_active = 0, got_dfunc = 0;
    GLboolean got_dmask = GL_TRUE, got_cmask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &got_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &got_read_fbo);
    glGetIntegerv(GL_VIEWPORT, got_vp);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &got_active);
    glGetIntegerv(GL_DEPTH_FUNC, &got_dfunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &got_dmask);
    glGetBooleanv(GL_COLOR_WRITEMASK, got_cmask);
    assert(got_draw_fbo == (GLint)draw_fbo);
    assert(got_read_fbo == (GLint)read_fbo);
    assert(got_vp[0] == 3 && got_vp[1] == 5 && got_vp[2] == 17 && got_vp[3] == 19);
    assert(got_active == GL_TEXTURE2);
    assert(glIsEnabled(GL_SCISSOR_TEST));
    assert(glIsEnabled(GL_CULL_FACE));
    assert(glIsEnabled(GL_BLEND));
    assert(!glIsEnabled(GL_DEPTH_TEST));
    assert(got_dmask == GL_FALSE);
    assert(got_dfunc == GL_GREATER);
    assert(!got_cmask[0] && !got_cmask[1] && !got_cmask[2] && !got_cmask[3]);

    // Restore normal GL state before cleaning up test FBOs.
    glDisable(GL_SCISSOR_TEST);
    glScissor(saved_scissor_box[0], saved_scissor_box[1], saved_scissor_box[2], saved_scissor_box[3]);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    if (saved_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)saved_draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved_read_fbo);
    glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
    glDeleteFramebuffers(1, &draw_fbo);
    glDeleteFramebuffers(1, &read_fbo);

    // Verify accurate height readbacks and mesh filtering.
    float y = 0.0f;
    assert(RE_renderer_detect_height(hd, -5.0f, -5.0f, &y));
    assert(fabsf(y - 3.5f) < 0.05f);  // MESH_NO_HEIGHT_DETECTION at y=9.0 was skipped
    assert(RE_renderer_detect_height(hd, -5.0f, 5.0f, &y));
    assert(fabsf(y - 3.5f) < 0.05f);  // hidden mesh at y=9.5 was skipped
    assert(RE_renderer_detect_height(hd, 4.0f, 4.0f, &y));
    assert(fabsf(y - 7.0f) < 0.05f);  // elevated platform at y=7.0 won depth test
    assert(!RE_renderer_detect_height(hd, 25.0f, 25.0f, &y));

    RE_renderer_free_height_detector(hd);
    for (int i = 0; i < 4; i++)
        free_quad_mesh(&meshes[i]);
    RE_renderer_free(r);
    assert(glGetError() == GL_NO_ERROR);
}

struct skinned_test_vertex {
    GLfloat pos[3];
    GLfloat normal[3];
    GLfloat uv[2];
    GLint bone_idx[4];
    GLfloat bone_wt[4];
};

static void draw_skinned_quad(GLuint prog, GLuint bone_tex, const mat4 local_mat,
                              const struct skinned_test_vertex *verts, int nr_verts)
{
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nr_verts * sizeof(*verts)), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(VATTR_POS);
    glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, sizeof(*verts), (const void *)offsetof(struct skinned_test_vertex, pos));
    glEnableVertexAttribArray(VATTR_NORMAL);
    glVertexAttribPointer(VATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(*verts), (const void *)offsetof(struct skinned_test_vertex, normal));
    glEnableVertexAttribArray(VATTR_UV);
    glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, sizeof(*verts), (const void *)offsetof(struct skinned_test_vertex, uv));
    glEnableVertexAttribArray(VATTR_BONE_INDEX);
    glVertexAttribIPointer(VATTR_BONE_INDEX, 4, GL_INT, sizeof(*verts), (const void *)offsetof(struct skinned_test_vertex, bone_idx));
    glEnableVertexAttribArray(VATTR_BONE_WEIGHT);
    glVertexAttribPointer(VATTR_BONE_WEIGHT, 4, GL_FLOAT, GL_FALSE, sizeof(*verts), (const void *)offsetof(struct skinned_test_vertex, bone_wt));

    mat3 normal_mat;
    glm_mat4_pick3((vec4 *)local_mat, normal_mat);
    glUniformMatrix4fv(glGetUniformLocation(prog, "local_transform"), 1, GL_FALSE, local_mat[0]);
    glUniformMatrix3fv(glGetUniformLocation(prog, "normal_transform"), 1, GL_FALSE, normal_mat[0]);
    glUniform1i(glGetUniformLocation(prog, "has_bones"), GL_TRUE);
    bind_bone_texture(bone_tex);

    glDrawArrays(GL_TRIANGLES, 0, nr_verts);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

static GLuint create_white_1x1_texture(void)
{
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static void setup_reign_normal_test_program(GLuint prog, GLuint white_tex)
{
    mat4 ident = GLM_MAT4_IDENTITY_INIT;
    glUseProgram(prog);
    reset_default_vertex_attribs();
    glUniformMatrix4fv(glGetUniformLocation(prog, "view_transform"), 1, GL_FALSE, ident[0]);
    glUniformMatrix4fv(glGetUniformLocation(prog, "proj_transform"), 1, GL_FALSE, ident[0]);
    glUniformMatrix4fv(glGetUniformLocation(prog, "shadow_transform"), 1, GL_FALSE, ident[0]);
    glUniform3f(glGetUniformLocation(prog, "camera_pos"), 0.0f, 0.0f, 2.0f);
    glUniform4f(glGetUniformLocation(prog, "uv_tiling"), 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(prog, "use_normal_map"), GL_FALSE);
    glUniform1i(glGetUniformLocation(prog, "use_blend_texture"), GL_FALSE);
    glUniform1i(glGetUniformLocation(prog, "use_specular_map"), GL_FALSE);
    glUniform1f(glGetUniformLocation(prog, "alpha_mod"), 1.0f);
    glUniform3f(glGetUniformLocation(prog, "global_ambient"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "instance_ambient"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "diffuse_mod"), 1.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(prog, "diffuse_type"), DIFFUSE_NORMAL);
    glUniform1f(glGetUniformLocation(prog, "shadow_darkness"), 0.0f);
    glUniform1i(glGetUniformLocation(prog, "alpha_mode"), ALPHA_BLEND);
    glUniform1i(glGetUniformLocation(prog, "fog_type"), 0);
    glUniform1f(glGetUniformLocation(prog, "rim_exponent"), 0.0f);
    glUniform3f(glGetUniformLocation(prog, "rim_color"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "specular_color"), 0.0f, 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(prog, "specular_shininess"), 0.0f);

    // Light 0: -dir = +Y => diffuse (GREEN) for +Y normal, globe_diffuse (RED) for -Y normal.
    glUniform3f(glGetUniformLocation(prog, "dir_lights[0].dir"), 0.0f, -1.0f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "dir_lights[0].diffuse"), 0.0f, 1.0f, 0.0f);
    glUniform3f(glGetUniformLocation(prog, "dir_lights[0].globe_diffuse"), 1.0f, 0.0f, 0.0f);
    for (int i = 1; i < NR_DIR_LIGHTS; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "dir_lights[%d].dir", i);
        glUniform3f(glGetUniformLocation(prog, buf), 0.0f, 0.0f, 0.0f);
        snprintf(buf, sizeof(buf), "dir_lights[%d].diffuse", i);
        glUniform3f(glGetUniformLocation(prog, buf), 0.0f, 0.0f, 0.0f);
        snprintf(buf, sizeof(buf), "dir_lights[%d].globe_diffuse", i);
        glUniform3f(glGetUniformLocation(prog, buf), 0.0f, 0.0f, 0.0f);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, white_tex);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
}

static void fill_quad_verts(struct skinned_test_vertex out[6], float x0, float x1, float y0, float y1,
                            const float nrm[3], const int bidx[4], const float bwt[4])
{
    const float coords[6][2] = {
        { x0, y0 }, { x1, y0 }, { x1, y1 },
        { x0, y0 }, { x1, y1 }, { x0, y1 },
    };
    for (int i = 0; i < 6; i++) {
        out[i].pos[0] = coords[i][0];
        out[i].pos[1] = coords[i][1];
        out[i].pos[2] = 0.0f;
        memcpy(out[i].normal, nrm, 3 * sizeof(float));
        out[i].uv[0] = out[i].uv[1] = 0.5f;
        memcpy(out[i].bone_idx, bidx, 4 * sizeof(int));
        memcpy(out[i].bone_wt, bwt, 4 * sizeof(float));
    }
}

void test_skinned_shader_normals_and_unweighted(void)
{
    GLint orig_draw_fbo = 0, orig_read_fbo = 0, orig_vp[4];
    GLboolean orig_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean orig_cull = glIsEnabled(GL_CULL_FACE);
    GLboolean orig_blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &orig_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &orig_read_fbo);
    glGetIntegerv(GL_VIEWPORT, orig_vp);

    GLuint fbo = 0, rt = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rt);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 32, 32);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bone 0 = R_z(+90 deg) stored row-major:
    //   col-major maps (+1,0,0) -> (0,+1,0) and (0,+1,0) -> (-1,0,0).
    //   row 0 = (0, -1, 0, 0), row 1 = (1, 0, 0, 0), row 2 = (0, 0, 1, 0).
    mat3x4 bones[1] = {{
        { 0.0f, -1.0f, 0.0f, 0.0f },
        { 1.0f,  0.0f, 0.0f, 0.0f },
        { 0.0f,  0.0f, 1.0f, 0.0f },
    }};
    GLuint bone_tex = test_create_bone_texture_from_array(bones, 1);
    assert(bone_tex != 0);
    GLuint white_tex = create_white_1x1_texture();
    GLuint prog = load_shader("shaders/reign.v.glsl", "shaders/reign.f.glsl", RE_REIGN_PLUGIN);
    setup_reign_normal_test_program(prog, white_tex);

    // Quad A (left, x in [-0.75, -0.25], y in [-0.25, 0.25]): weighted to bone 0 (R_z +90 deg),
    // input normal = +X => rotated normal MUST be +Y => GREEN (0, 255, 0), NOT RED (255, 0, 0).
    struct skinned_test_vertex q_rot[6];
    const float nrm_x[3] = { 1.0f, 0.0f, 0.0f };
    const int idx0[4] = { 0, -1, -1, -1 };
    const float wt1[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    fill_quad_verts(q_rot, -0.25f, 0.25f, -0.25f, 0.25f, nrm_x, idx0, wt1);
    mat4 left_mat = GLM_MAT4_IDENTITY_INIT;
    glm_translate(left_mat, (vec3){ -0.5f, 0.0f, 0.0f });
    draw_skinned_quad(prog, bone_tex, left_mat, q_rot, 6);

    // Quad B (right, x in [0.25, 0.75], y in [-0.25, 0.25]): unweighted (weight = 0),
    // must NOT collapse to (0, 0, 0) and must render GREEN (0, 255, 0).
    struct skinned_test_vertex q_unw[6];
    const float nrm_y[3] = { 0.0f, 1.0f, 0.0f };
    const float wt0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    fill_quad_verts(q_unw, 0.25f, 0.75f, -0.25f, 0.25f, nrm_y, idx0, wt0);
    mat4 ident = GLM_MAT4_IDENTITY_INIT;
    draw_skinned_quad(prog, bone_tex, ident, q_unw, 6);

    // Quad C (top center, x in [-0.25, 0.25], y in [0.35, 0.85]): out-of-bounds bone index,
    // must be safely ignored and render GREEN (0, 255, 0) with 0 GL errors.
    struct skinned_test_vertex q_oob[6];
    const int idx_oob[4] = { 999, -1, -1, -1 };
    fill_quad_verts(q_oob, -0.25f, 0.25f, 0.35f, 0.85f, nrm_y, idx_oob, wt1);
    draw_skinned_quad(prog, bone_tex, ident, q_oob, 6);

    unsigned char px[32 * 32 * 4];
    glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, px);
    assert(glGetError() == GL_NO_ERROR);

    // Sample center of Quad A: ndc (-0.5, 0.0) -> pixel (8, 16)
    unsigned char *pa = &px[(16 * 32 + 8) * 4];
    assert(pa[0] <= 2 && pa[1] >= 253 && pa[2] <= 2);

    // Sample center of Quad B: ndc (+0.5, 0.0) -> pixel (24, 16)
    unsigned char *pb = &px[(16 * 32 + 24) * 4];
    assert(pb[0] <= 2 && pb[1] >= 253 && pb[2] <= 2);

    // Sample center of Quad C: ndc (0.0, +0.6) -> pixel (16, 25)
    unsigned char *pc = &px[(25 * 32 + 16) * 4];
    assert(pc[0] <= 2 && pc[1] >= 253 && pc[2] <= 2);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteProgram(prog);
    glDeleteTextures(1, &bone_tex);
    glDeleteTextures(1, &white_tex);
    if (orig_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (orig_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (orig_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)orig_draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)orig_read_fbo);
    glViewport(orig_vp[0], orig_vp[1], orig_vp[2], orig_vp[3]);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &rt);
    assert(glGetError() == GL_NO_ERROR);
}

struct bone_reset_fixture {
    mat3x4 bone_transforms[1];
    GLuint bone_tex;
    bool bones_dirty;
    int reset_count;
};

static void bone_reset_fixture_cb(void *user)
{
    // Mirrors reset_instance_gl() in reign.c: recreate GL texture and re-upload CPU bones.
    struct bone_reset_fixture *f = user;
    f->bone_tex = test_create_bone_texture_from_array(f->bone_transforms, 1);
    f->bones_dirty = true;
    f->reset_count++;
}

static void render_and_assert_skinned_identity(GLuint bone_tex)
{
    GLint orig_draw_fbo = 0, orig_read_fbo = 0, orig_vp[4];
    GLboolean orig_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean orig_cull = glIsEnabled(GL_CULL_FACE);
    GLboolean orig_blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &orig_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &orig_read_fbo);
    glGetIntegerv(GL_VIEWPORT, orig_vp);

    GLuint fbo = 0, rt = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rt);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 16, 16);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint white_tex = create_white_1x1_texture();
    GLuint prog = load_shader("shaders/reign.v.glsl", "shaders/reign.f.glsl", RE_REIGN_PLUGIN);
    setup_reign_normal_test_program(prog, white_tex);

    struct skinned_test_vertex q[6];
    const float nrm_y[3] = { 0.0f, 1.0f, 0.0f };
    const int idx0[4] = { 0, -1, -1, -1 };
    const float wt1[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    fill_quad_verts(q, -0.5f, 0.5f, -0.5f, 0.5f, nrm_y, idx0, wt1);
    mat4 ident = GLM_MAT4_IDENTITY_INIT;
    draw_skinned_quad(prog, bone_tex, ident, q, 6);

    unsigned char px[16 * 16 * 4];
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
    assert(glGetError() == GL_NO_ERROR);

    unsigned char *center = &px[(8 * 16 + 8) * 4];
    assert(center[0] <= 2 && center[1] >= 253 && center[2] <= 2);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteProgram(prog);
    glDeleteTextures(1, &white_tex);
    if (orig_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (orig_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (orig_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)orig_draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)orig_read_fbo);
    glViewport(orig_vp[0], orig_vp[1], orig_vp[2], orig_vp[3]);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &rt);
    assert(glGetError() == GL_NO_ERROR);
}

void test_skinned_identity_bone_texture_reset(void (*trigger_context_reset)(void))
{
    struct bone_reset_fixture f = {0};
    test_init_identity_bones(f.bone_transforms, 1);
    f.bone_tex = test_create_bone_texture_from_array(f.bone_transforms, 1);
    assert(f.bone_tex && glIsTexture(f.bone_tex));

    // Verify valid non-zero skinned output before context loss.
    render_and_assert_skinned_identity(f.bone_tex);

    gfx_register_reset_callback(bone_reset_fixture_cb, &f);
    trigger_context_reset();
    gfx_unregister_reset_callback(bone_reset_fixture_cb, &f);

    assert(f.reset_count == 1);
    assert(f.bones_dirty);
    assert(f.bone_tex && glIsTexture(f.bone_tex));

    // Verify valid non-zero skinned output after context reset & bone texture re-upload.
    render_and_assert_skinned_identity(f.bone_tex);

    glDeleteTextures(1, &f.bone_tex);
    assert(glGetError() == GL_NO_ERROR);
}

void *test_re_renderer_create_with_billboard(int cg_no)
{
    struct RE_renderer *r = RE_renderer_new(RE_SEAL_PLUGIN);
    RE_renderer_set_viewport_size(r, 32, 32);
    assert(RE_renderer_load_billboard_texture_by_no(r, cg_no));
    return r;
}

GLuint test_re_renderer_get_billboard_tex(void *ptr, int cg_no)
{
    struct RE_renderer *r = ptr;
    struct billboard_texture *bt = ht_get_int(r->billboard_textures_by_no, cg_no, NULL);
    return bt ? bt->texture : 0;
}

void test_re_renderer_assert_valid(void *ptr, int cg_no)
{
    struct RE_renderer *r = ptr;
    assert(r->program && glIsProgram(r->program));
    assert(r->depth_buffer && glIsRenderbuffer(r->depth_buffer));
    assert(r->shadow.program && glIsProgram(r->shadow.program));
    assert(r->shadow.fbo && glIsFramebuffer(r->shadow.fbo));
    assert(r->shadow.texture && glIsTexture(r->shadow.texture));
    assert(r->outline.program && glIsProgram(r->outline.program));
    assert(r->billboard_particle_vao && glIsVertexArray(r->billboard_particle_vao));
    assert(r->billboard_instance_vao && glIsVertexArray(r->billboard_instance_vao));
    struct billboard_texture *bt = ht_get_int(r->billboard_textures_by_no, cg_no, NULL);
    assert(bt && bt->texture && glIsTexture(bt->texture));
}

void test_re_renderer_destroy(void *ptr)
{
    RE_renderer_free((struct RE_renderer *)ptr);
}
