/* Genuine GLES3 integration test. Uses real video/draw/Parts code + shaders.
 * Synthetic asymmetric artwork only; not a Dohna Dohna game screenshot.
 */
#include <assert.h>
#include <stdarg.h>
#include "system4/string.h"
#include "../../../src/video.c"
#include "../../../src/parts/parts_internal.h"

struct config config = { .view_width = 128, .view_height = 128 };
extern bool test_blur(struct parts_construction_process *, struct parts_cp_filter *, bool);
extern bool test_gray(struct parts_construction_process *, struct parts_cp_filter *);
extern bool test_v14(struct parts_construction_process *, struct parts_cp_v14 *);
extern void test_render_cg(struct parts *, struct parts_common *);
extern void parts_render_init(void);
extern void test_render_flat(struct parts *, struct parts_flat *);
extern void test_set_reverse(bool, bool);
extern void test_activity_scale(struct parts *, bool, bool, float, float);
extern GLuint test_load_3d_shader(const char *, const char *, int);
int test_bone_texture_unit(void);
GLuint test_create_bone_texture(void);
extern void test_height_detector_gles3(void);
extern void test_skinned_shader_normals_and_unweighted(void);
extern void test_skinned_identity_bone_texture_reset(void (*)(void));
extern void *test_re_renderer_create_with_billboard(int);
extern GLuint test_re_renderer_get_billboard_tex(void *, int);
extern void test_re_renderer_assert_valid(void *, int);
extern void test_re_renderer_destroy(void *);
static struct parts *fixture_part;
struct parts *__wrap_parts_get(int no) { assert(no==1 && fixture_part); return fixture_part; }

void *_xmalloc(size_t n, const char *f) { (void)f; void *p=malloc(n); assert(p); return p; }
void *_xcalloc(size_t n, size_t z, const char *f) { (void)f; void *p=calloc(n,z); assert(p); return p; }
void *_xrealloc(void *old, size_t n, const char *f) { (void)f; void *p=realloc(old,n); assert(p); return p; }
_Noreturn void sys_error(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap); abort();
}
void sys_message(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); vfprintf(stdout,fmt,ap); va_end(ap);
}
char *sjis_filename2utf(const char *src, size_t len) {
    (void)src; (void)len;
    return strdup("Synthetic renderer regression");
}
static int diagnostic_test_messages, diagnostic_driver_errors;
void sys_warning(const char *fmt, ...) {
    char message[1024];
    va_list ap; va_start(ap,fmt); vsnprintf(message,sizeof(message),fmt,ap); va_end(ap);
    fputs(message,stderr);
    if (strstr(message,"GLES debug message")) {
        if (strstr(message,"native diagnostic fixture"))
            diagnostic_test_messages++;
        else
            diagnostic_driver_errors++;
    }
}
static int draws, feedbacks;
static bool zero_blend_constant;
void __real_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void __wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    GLint fbo=0, dst=0, src=0, active=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&src);
    glActiveTexture(active);
    if (fbo) glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&dst);
    draws++;
    if (dst && dst==src) { feedbacks++; fprintf(stderr,"framebuffer feedback detected\n"); abort(); }
    if (zero_blend_constant) glBlendColor(0,0,0,0);
    __real_glDrawElements(mode,count,type,indices);
}
static void no_error(void) { GLenum e=glGetError(); if(e)fprintf(stderr,"GL error 0x%x\n",e); assert(e==GL_NO_ERROR); }
static bool reject_debug_context;
static int context_attempts, debug_context_attempts;
SDL_GLContext __real_SDL_GL_CreateContext(SDL_Window *window);
SDL_GLContext __wrap_SDL_GL_CreateContext(SDL_Window *window) {
    int flags;
    assert(SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS,&flags)==0);
    context_attempts++;
    if (flags & SDL_GL_CONTEXT_DEBUG_FLAG) {
        debug_context_attempts++;
        if (reject_debug_context) {
            SDL_SetError("synthetic driver rejection of debug context");
            return NULL;
        }
    }
    return __real_SDL_GL_CreateContext(window);
}
static void gles_debug_context_test(void) {
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","1",1)==0);
    reject_debug_context=true;
    gfx_create_window_and_context(SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    reject_debug_context=false;
    assert(sdl.window && sdl.gl.context && SDL_GL_GetCurrentContext()==sdl.gl.context);
    assert(context_attempts==2 && debug_context_attempts==1);
    SDL_GL_DeleteContext(sdl.gl.context);
    SDL_DestroyWindow(sdl.window);
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","0",1)==0);
    puts("PASS: diagnostic startup requests a debug context and retries without the flag");
}
static void gles_diagnostic_test(void) {
    if (!SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        puts("SKIP: GL_KHR_debug not supported");
        return;
    }
    PFNGLDEBUGMESSAGEINSERTKHRPROC insert = (PFNGLDEBUGMESSAGEINSERTKHRPROC)
        SDL_GL_GetProcAddress("glDebugMessageInsertKHR");
    assert(insert);
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","0",1)==0);
    gl_enable_diagnostics();
    assert(!glIsEnabled(GL_DEBUG_OUTPUT_KHR));
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","1",1)==0);
    gl_enable_diagnostics();
    insert(GL_DEBUG_SOURCE_APPLICATION_KHR,GL_DEBUG_TYPE_ERROR_KHR,1,
           GL_DEBUG_SEVERITY_HIGH_KHR,-1,"native diagnostic fixture");
    assert(diagnostic_test_messages==1);
    insert(GL_DEBUG_SOURCE_APPLICATION_KHR,GL_DEBUG_TYPE_OTHER_KHR,1,
           GL_DEBUG_SEVERITY_HIGH_KHR,-1,"native diagnostic fixture");
    assert(diagnostic_test_messages==1);
    glDisable(GL_DEBUG_OUTPUT_KHR);
    glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","0",1)==0);
    no_error();
    puts("PASS: opt-in GLES diagnostics report errors but not informational messages");
}
static void gles_driver_diagnostic_test(void) {
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","1",1)==0);
    gfx_create_window_and_context(SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    int flags;
    assert(SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS,&flags)==0);
    if ((flags & SDL_GL_CONTEXT_DEBUG_FLAG) && SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        gl_enable_diagnostics();
        int before=diagnostic_driver_errors;
        glEnable(GL_TEXTURE_2D);
        assert(glGetError()==GL_INVALID_ENUM);
        assert(diagnostic_driver_errors>before);
        puts("PASS: GLES debug context reports a real driver-generated GL error");
    } else {
        puts("SKIP: debug context with GL_KHR_debug not supported");
    }
    SDL_GL_DeleteContext(sdl.gl.context);
    SDL_DestroyWindow(sdl.window);
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,flags & ~SDL_GL_CONTEXT_DEBUG_FLAG)==0);
    assert(SDL_setenv("ALICEHUB_GLES_DEBUG","0",1)==0);
}
static void write_ppm(const char *name, const unsigned char *px, int w, int h) {
    FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;i++) fwrite(px+4*i,1,3,f);
    fclose(f);
}
static void cpu_blur(unsigned char *out,const unsigned char *in,int W,int H,int x0,int y0,int w,int h,int r,bool v) {
    memcpy(out,in,(size_t)W*H*4);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)for(int c=0;c<3;c++) {
        int sum=0;
        for(int k=-r;k<=r;k++) {
            int xx=x+(v?0:k), yy=y+(v?k:0);
            if(xx>=0&&xx<w&&yy>=0&&yy<h)sum+=in[((y0+yy)*W+x0+xx)*4+c];
        }
        out[((y0+y)*W+x0+x)*4+c]=(unsigned char)round((double)sum/(2*r+1));
    }
}
static void check_pixels(unsigned char *got,const unsigned char *expected,size_t n,int tolerance) {
    for(size_t i=0;i<n;i++) if(abs(got[i]-expected[i])>tolerance) {
        fprintf(stderr,"pixel byte %zu: got %d expected %d\n",i,got[i],expected[i]); abort();
    }
}
static void check_job_attributes_disabled(void) {
    GLint count;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&count);
    glBindVertexArray(sdl.gl.vao);
    for (GLint i=0;i<count;i++) {
        GLint enabled;
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
        assert(!enabled);
    }
    glBindVertexArray(0);
    no_error();
}
static void job_attribute_test(void) {
    Texture src={0}, dst={0};
    gfx_init_texture_blank(&src,4,4);
    gfx_init_texture_blank(&dst,4,4);
    gfx_fill_with_alpha(&src,0,0,4,4,37,91,173,109);
    check_job_attributes_disabled();
    gfx_copy_with_alpha_map(&dst,0,0,&src,0,0,4,4);
    check_job_attributes_disabled();
    unsigned char *pixels=gfx_get_pixels(&dst);
    const unsigned char expected[]={37,91,173,109};
    for (int i=0;i<16;i++)
        check_pixels(pixels+4*i,expected,sizeof(expected),0);
    free(pixels);
    gfx_delete_texture(&dst);
    gfx_delete_texture(&src);
    no_error();
    puts("PASS: fill without UVs and textured copy leave no errors or enabled job attributes");
}
// modifier_on_frame is a post-present gameplay hook, not part of rendering.
void modifier_on_frame(void) {}
static bool expect_resumed_frame;
static int resumed_frames;
static unsigned char expected_resumed_rgba[4]={37,91,173,255};
void __real_SDL_GL_SwapWindow(SDL_Window *window);
void __wrap_SDL_GL_SwapWindow(SDL_Window *window) {
    if (expect_resumed_frame) {
        unsigned char pixels[128*128*4];
        glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        for (int i=0;i<128*128;i++) {
            check_pixels(pixels+4*i,expected_resumed_rgba,4,0);
        }
        if (!resumed_frames) write_ppm("resume-surface-gles-synthetic.ppm",pixels,128,128);
        resumed_frames++;
        expect_resumed_frame=false;
    }
    __real_SDL_GL_SwapWindow(window);
}
static void resume_surface_tests(void) {
    // Keep the production context and its scene texture, but replace the real
    // window/drawable repeatedly: the Android surface recreation case, not a
    // context-loss simulation and not a full game/Activity lifecycle test.
    glBindFramebuffer(GL_FRAMEBUFFER,main_surface_fb);
    glClearColor(37/255.f,91/255.f,173/255.f,1); glClear(GL_COLOR_BUFFER_BIT);
    gfx_initialized=true;
    gfx_reset_view();
    for (int n=0;n<32;n++) {
        SDL_Window *previous=sdl.window;
        sdl.window=SDL_CreateWindow("Synthetic recreated surface",0,0,128,128,
                                   SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        assert(sdl.window);
        assert(SDL_GL_MakeCurrent(sdl.window,sdl.gl.context)==0);
        SDL_DestroyWindow(previous);
        assert(glIsTexture(main_surface.handle));
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,128,128);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        unsigned char black[4];
        glReadPixels(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,black);
        const unsigned char expected_black[4]={0,0,0,255};
        check_pixels(black,expected_black,4,0);
        expect_resumed_frame=true;
        gfx_restore_window();
        assert(!expect_resumed_frame); // Must present without a new script frame.
        GLint framebuffer=0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
        assert(framebuffer==(GLint)main_surface_fb);
        no_error();
    }
    assert(resumed_frames==32);
    puts("PASS: 32 real SDL/GLES drawable recreations retain and re-present the scene without script redraw");
}
static void replacement_context_negative_control(void) {
    // A new *unshared* context is different from the retained-context drawable
    // test. It intentionally has none of the game's textures. This does not
    // claim to reproduce a particular phone's context loss or recover from it.
    GLuint retained_texture=gfx_main_surface()->handle;
    assert(glIsTexture(retained_texture));
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)==0);
    SDL_GLContext replacement=SDL_GL_CreateContext(sdl.window);
    assert(replacement && replacement!=sdl.gl.context);
    assert(SDL_GL_GetCurrentContext()==replacement);
    assert(!glIsTexture(retained_texture));
    no_error();
    assert(SDL_GL_MakeCurrent(sdl.window,sdl.gl.context)==0);
    assert(glIsTexture(retained_texture));
    SDL_GL_DeleteContext(replacement);
    no_error();
    puts("PASS: replacement-context negative control has no retained texture; re-present is not context recovery");
}
static void replacement_context_recovery_with_backup(void) {
    gfx_initialized=true;
    gfx_reset_view();
    glBindFramebuffer(GL_FRAMEBUFFER,main_surface_fb);
    glViewport(0,0,128,128);
    glClearColor(220/255.f,110/255.f,40/255.f,1); glClear(GL_COLOR_BUFFER_BIT);
    unsigned char cg_px[16*16*4];
    for (int i=0;i<16*16;i++) { cg_px[i*4+0]=30; cg_px[i*4+1]=200; cg_px[i*4+2]=170; cg_px[i*4+3]=255; }
    Texture cg_tex={0}; gfx_init_texture_with_pixels(&cg_tex,16,16,cg_px);
    Texture lazy_tex={0}; gfx_init_texture_with_pixels(&lazy_tex,16,16,cg_px);
    GLuint old_main_handle=main_surface.handle;
    GLuint old_cg_handle=cg_tex.handle;
    GLuint old_lazy_handle=lazy_tex.handle;
    SDL_GLContext old_ctx=sdl.gl.context;
    gfx_prepare_context_backup();
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)==0);
    SDL_GLContext rep=SDL_GL_CreateContext(sdl.window);
    assert(rep && rep!=old_ctx);
    assert(SDL_GL_GetCurrentContext()==rep);
    assert(!glIsTexture(old_main_handle));
    assert(!glIsTexture(old_cg_handle));
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,128,128);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    expected_resumed_rgba[0]=220; expected_resumed_rgba[1]=110;
    expected_resumed_rgba[2]=40;  expected_resumed_rgba[3]=255;
    expect_resumed_frame=true;
    gfx_restore_window();
    assert(!expect_resumed_frame);
    assert(sdl.gl.context==rep);
    assert(glIsProgram(default_shader.program));
    assert(glIsFramebuffer(main_surface_fb));
    assert(glIsTexture(main_surface.handle));
    GLuint rec_lazy_handle=gfx_remap_texture_handle(old_lazy_handle);
    assert(rec_lazy_handle && glIsTexture(rec_lazy_handle));
    size_t live_before_delete=live_tex_count;
    gfx_delete_texture(&lazy_tex);
    assert(lazy_tex.handle==0 && lazy_tex.ctx_gen==0);
    assert(!glIsTexture(rec_lazy_handle));
    assert(live_tex_count+1==live_before_delete);
    assert(gfx_remap_texture_handle(old_lazy_handle)==0);
    assert(gfx_remap_texture(&cg_tex));
    assert(cg_tex.handle && glIsTexture(cg_tex.handle));
    gfx_copy(&main_surface,0,0,&cg_tex,0,0,16,16);
    SDL_Color px=gfx_get_pixel(&main_surface,8,8);
    if (!(px.r==30 && px.g==200 && px.b==170)) {
        fprintf(stderr,"restored cg_tex pixel got (%d,%d,%d,%d) expected (30,200,170,255)\n",px.r,px.g,px.b,px.a);
        abort();
    }
    gfx_delete_texture(&cg_tex);
    SDL_GL_DeleteContext(old_ctx);
    no_error();
    puts("PASS: replacement EGLContext recovery with backup restores non-black surface and sprites");
}
static void replacement_context_deferred_until_surface_ready(void) {
    gfx_initialized=true;
    gfx_reset_view();
    glBindFramebuffer(GL_FRAMEBUFFER,main_surface_fb);
    glViewport(0,0,128,128);
    glClearColor(180/255.f,75/255.f,210/255.f,1); glClear(GL_COLOR_BUFFER_BIT);
    unsigned char px[8*8*4];
    for (int i=0;i<8*8;i++) { px[i*4+0]=45; px[i*4+1]=215; px[i*4+2]=95; px[i*4+3]=255; }
    Texture tex={0}; gfx_init_texture_with_pixels(&tex,8,8,px);
    SDL_GLContext old_ctx=sdl.gl.context;
    gfx_prepare_context_backup();
    assert(has_backup_main_surface && backup_tex_count>=1);

    // Simulate foreground drain before EGLSurface / replacement EGLContext is current.
    assert(SDL_GL_MakeCurrent(sdl.window,NULL)==0);
    sdl.gl.context=NULL;
    assert(!gfx_release_context_backup_if_ready());
    assert(has_backup_main_surface && backup_tex_count>=1);
    gfx_restore_window();
    assert(has_backup_main_surface && backup_tex_count>=1);

    // Replacement context becomes current later (e.g., SDL_RENDER_DEVICE_RESET).
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)==0);
    SDL_GLContext rep=SDL_GL_CreateContext(sdl.window);
    assert(rep && rep!=old_ctx && SDL_GL_GetCurrentContext()==rep);
    expected_resumed_rgba[0]=180; expected_resumed_rgba[1]=75;
    expected_resumed_rgba[2]=210; expected_resumed_rgba[3]=255;
    expect_resumed_frame=true;
    gfx_restore_window();
    assert(!expect_resumed_frame);
    assert(!has_backup_main_surface && backup_tex_count==0);
    assert(gfx_remap_texture(&tex));
    assert(tex.handle && glIsTexture(tex.handle));
    unsigned char *got=gfx_get_pixels(&tex);
    check_pixels(got,px,sizeof(px),0); free(got);
    gfx_delete_texture(&tex);
    SDL_GL_DeleteContext(old_ctx);
    no_error();
    puts("PASS: deferred EGLContext recovery retains backup across early foreground drain");
}
static void replacement_context_recovery_without_backup(void) {
    gfx_release_context_backup();
    unsigned char stale_px[8*8*4]; memset(stale_px,99,sizeof(stale_px));
    Texture stale={0}; gfx_init_texture_with_pixels(&stale,8,8,stale_px);
    void *r3d=test_re_renderer_create_with_billboard(77);
    test_re_renderer_assert_valid(r3d,77);
    GLuint old_bb_tex=test_re_renderer_get_billboard_tex(r3d,77);
    assert(old_bb_tex && glIsTexture(old_bb_tex));
    SDL_GLContext old_ctx=sdl.gl.context;
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)==0);
    SDL_GLContext rep2=SDL_GL_CreateContext(sdl.window);
    assert(rep2 && rep2!=old_ctx);
    assert(SDL_GL_GetCurrentContext()==rep2);
    assert(!glIsTexture(stale.handle));
    assert(!glIsTexture(old_bb_tex));
    gfx_restore_window();
    assert(sdl.gl.context==rep2);
    assert(glIsProgram(default_shader.program));
    assert(glIsFramebuffer(main_surface_fb));
    assert(glIsTexture(main_surface.handle));
    assert(!gfx_remap_texture(&stale));
    assert(stale.handle==0);
    test_re_renderer_assert_valid(r3d,77);
    test_re_renderer_destroy(r3d);
    SDL_GL_DeleteContext(old_ctx);
    no_error();
    puts("PASS: unbacked replacement EGLContext recovery rebuilds GL objects and clears stale handles");
}
static void shader3d_tests(void) {
    // Compile/link the production 3D shader pairs through the real loader.
    // Bone matrices live in a texture (not a uniform block), so the programs
    // also link on GLES drivers that count block members against
    // GL_MAX_VERTEX_UNIFORM_VECTORS (e.g. SwiftShader: 256).
    GLint max_tex=0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_tex);
    assert(max_tex >= 308*3);
    const char *names[]={"reign","reign_shadow","reign_outline"};
    for (int engine=0;engine<3;engine++)for(int i=0;i<3;i++) {
        char vertex[128],fragment[128];
        snprintf(vertex,sizeof(vertex),"shaders/%s.v.glsl",names[i]);
        snprintf(fragment,sizeof(fragment),"shaders/%s.f.glsl",names[i]);
        GLuint program=test_load_3d_shader(vertex,fragment,engine);
        GLint blocks=-1;
        glGetProgramiv(program,GL_ACTIVE_UNIFORM_BLOCKS,&blocks);
        assert(blocks==0);
        GLint loc=glGetUniformLocation(program,"bone_texture");
        assert(loc>=0);
        GLint unit=-1;
        glGetUniformiv(program,loc,&unit);
        assert(unit==test_bone_texture_unit());
        glDeleteProgram(program); no_error();
    }
    GLuint tex=test_create_bone_texture();
    assert(tex && glIsTexture(tex));
    glDeleteTextures(1,&tex); no_error();
    puts("PASS: 9 production 3D shader pairs link; 308-bone RGBA32F bone texture is bound to its own unit");
}
static void height_detector_gles3_test(void) {
    int before=diagnostic_driver_errors;
    if (SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        assert(SDL_setenv("ALICEHUB_GLES_DEBUG","1",1)==0);
        gl_enable_diagnostics();
    }
    test_height_detector_gles3();
    no_error();
    assert(diagnostic_driver_errors==before);
    if (SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        glDisable(GL_DEBUG_OUTPUT_KHR);
        glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
        assert(SDL_setenv("ALICEHUB_GLES_DEBUG","0",1)==0);
    }
    puts("PASS: GLES3 height detector reads R16UI via RGBA_INTEGER/UNSIGNED_INT with 0 GL errors and preserves FBO/GL state");
}
static void skinned_shader_gles3_test(void) {
    test_skinned_shader_normals_and_unweighted();
    no_error();
    puts("PASS: skinned reign.v.glsl rotates normals accurately (+Y not transposed -Y) and preserves unweighted vertices");
}
static void trigger_unshared_context_reset(void) {
    gfx_release_context_backup();
    SDL_GLContext old_ctx=sdl.gl.context;
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)==0);
    SDL_GLContext rep=SDL_GL_CreateContext(sdl.window);
    assert(rep && rep!=old_ctx && SDL_GL_GetCurrentContext()==rep);
    gfx_restore_window();
    assert(sdl.gl.context==rep);
    SDL_GL_DeleteContext(old_ctx);
}
static void skinned_identity_bone_reset_test(void) {
    test_skinned_identity_bone_texture_reset(trigger_unshared_context_reset);
    no_error();
    puts("PASS: identity RGBA32F bone texture upload and context reset retain valid non-zero skinned output");
}
static void blur_tests(void) {
    enum { W=15,H=11,N=W*H*4 };
    unsigned char input[N], expected[N], next[N];
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
        int i=(y*W+x)*4;
        input[i]=(x%3==0)?255:0; input[i+1]=(y%3==0)?255:0;
        input[i+2]=((x+y)%4==0)?255:0; input[i+3]=(x*11+y*13)%256;
    }
    zero_blend_constant=true;
    for(int vertical=0;vertical<2;vertical++)for(int full=0;full<2;full++)for(int pinned=0;pinned<2;pinned++) {
        struct parts_construction_process cp={0};
        gfx_init_texture_with_pixels(&cp.common.texture,W,H,input);
        struct parts_cp_filter op={.x=2,.y=1,.w=10,.h=8,.blur=1,.full_size=full};
        cpu_blur(expected,input,W,H,full?0:2,full?0:1,full?W:10,full?H:8,1,vertical);
        if(pinned)gfx_begin_draw_target(&cp.common.texture);
        assert(test_blur(&cp,&op,vertical));
        if(pinned)gfx_end_draw_target();
        unsigned char *got=gfx_get_pixels(&cp.common.texture);
        check_pixels(got,expected,N,1); free(got);
        if(full&&!vertical&&!pinned) {
            cpu_blur(next,expected,W,H,0,0,W,H,1,true);
            assert(test_blur(&cp,&op,true));
            got=gfx_get_pixels(&cp.common.texture); check_pixels(got,next,N,2);
            write_ppm("blur-gles-synthetic.ppm",got,W,H); free(got);
        }
        gfx_delete_texture(&cp.common.texture); no_error();
    }
    zero_blend_constant=false;
    puts("PASS: GLES H/V blur, full/subrect, pinned/unpinned, RGBA preservation, two-pass blur");
    puts("PASS: RGB blur remains correct with a zero fixed-function blend constant");
}
static void straight_alpha_cut_test(void) {
    unsigned char pixel[] = {200,80,40,128};
    unsigned char base[] = {1,2,3,255, 0,0,0,0, 20,200,100,128, 4,5,6,255};
    unsigned char expected[] = {1,2,3,255, 200,80,40,128, 140,120,60,192, 4,5,6,255};
    Texture src={0}, dst={0};
    gfx_init_texture_with_pixels(&src,1,1,pixel);
    gfx_init_texture_with_pixels(&dst,4,1,base);
    gfx_begin_draw_target(&dst);
    gfx_copy_stretch_blend_straight_alpha(&dst,1,0,2,1,&src,0,0,1,1);
    gfx_end_draw_target();
    unsigned char *got=gfx_get_pixels(&dst);
    check_pixels(got,expected,sizeof(expected),1);
    free(got);
    gfx_delete_texture(&dst); gfx_delete_texture(&src); no_error();
    puts("PASS: GLES translucent construction cut preserves straight color and source-over coverage");
}
static void gray_v14_and_fill_shader_tests(void) {
    enum { W=15,H=11,N=W*H*4 };
    unsigned char input[N], expected[N];
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
        int i=(y*W+x)*4;
        input[i]=(unsigned char)((x*17+y*3)%256);
        input[i+1]=(unsigned char)((x*5+y*23)%256);
        input[i+2]=(unsigned char)((x*13+y*7)%256);
        input[i+3]=(unsigned char)((x*19+y*29)%256);
    }
    // 1. build_gray_filter (full_size & subrect, pinned & unpinned, no feedback loop)
    for(int full=0;full<2;full++)for(int pinned=0;pinned<2;pinned++) {
        struct parts_construction_process cp={0};
        gfx_init_texture_with_pixels(&cp.common.texture,W,H,input);
        struct parts_cp_filter op={.x=2,.y=1,.w=10,.h=8,.full_size=full};
        memcpy(expected,input,N);
        int x0=full?0:2, y0=full?0:1, rw=full?W:10, rh=full?H:8;
        for(int y=0;y<rh;y++)for(int x=0;x<rw;x++) {
            int idx=((y0+y)*W+(x0+x))*4;
            unsigned char g=(unsigned char)round(0.299*input[idx]+0.587*input[idx+1]+0.114*input[idx+2]);
            expected[idx]=expected[idx+1]=expected[idx+2]=g;
        }
        if(pinned)gfx_begin_draw_target(&cp.common.texture);
        assert(test_gray(&cp,&op));
        if(pinned)gfx_end_draw_target();
        unsigned char *got=gfx_get_pixels(&cp.common.texture);
        check_pixels(got,expected,N,1); free(got);
        gfx_delete_texture(&cp.common.texture); no_error();
    }
    // 2. build_v14 cmd 132 color reverse (full_size & subrect, no feedback loop)
    for(int full=0;full<2;full++) {
        struct parts_construction_process cp={0};
        gfx_init_texture_with_pixels(&cp.common.texture,W,H,input);
        struct parts_cp_v14 op={.command=132,.dx=2,.dy=1,.dw=10,.dh=8,.full_size=full};
        memcpy(expected,input,N);
        int x0=full?0:2, y0=full?0:1, rw=full?W:10, rh=full?H:8;
        for(int y=0;y<rh;y++)for(int x=0;x<rw;x++) {
            int idx=((y0+y)*W+(x0+x))*4;
            expected[idx]=255-input[idx];
            expected[idx+1]=255-input[idx+1];
            expected[idx+2]=255-input[idx+2];
        }
        assert(test_v14(&cp,&op));
        unsigned char *got=gfx_get_pixels(&cp.common.texture);
        check_pixels(got,expected,N,1); free(got);
        gfx_delete_texture(&cp.common.texture); no_error();
    }
    // 3. run_fill_shader callers: gfx_fill_amap_over_border, under_border, gradation_ud
    {
        Texture dst={0};
        gfx_init_texture_with_pixels(&dst,W,H,input);
        memcpy(expected,input,N);
        for(int y=1;y<1+8;y++)for(int x=2;x<2+10;x++) {
            int idx=(y*W+x)*4;
            if(input[idx+3]>=120) expected[idx+3]=42;
        }
        gfx_fill_amap_over_border(&dst,2,1,10,8,42,120);
        unsigned char *got=gfx_get_pixels(&dst);
        check_pixels(got,expected,N,1); free(got);
        gfx_delete_texture(&dst); no_error();
    }
    {
        Texture dst={0};
        gfx_init_texture_with_pixels(&dst,W,H,input);
        memcpy(expected,input,N);
        for(int y=1;y<1+8;y++)for(int x=2;x<2+10;x++) {
            int idx=(y*W+x)*4;
            if(input[idx+3]<=120) expected[idx+3]=210;
        }
        gfx_fill_amap_under_border(&dst,2,1,10,8,210,120);
        unsigned char *got=gfx_get_pixels(&dst);
        check_pixels(got,expected,N,1); free(got);
        gfx_delete_texture(&dst); no_error();
    }
    {
        Texture dst={0};
        gfx_init_texture_with_pixels(&dst,W,H,input);
        memcpy(expected,input,N);
        for(int y=0;y<8;y++)for(int x=0;x<10;x++) {
            int idx=((1+y)*W+(2+x))*4;
            double ty=((double)y+0.5)/8.0;
            expected[idx+3]=(unsigned char)round(20.0+(220.0-20.0)*ty);
        }
        gfx_fill_amap_gradation_ud(&dst,2,1,10,8,20,220);
        unsigned char *got=gfx_get_pixels(&dst);
        check_pixels(got,expected,N,2); free(got);
        gfx_delete_texture(&dst); no_error();
    }
    puts("PASS: GLES gray filter, v14 color reverse, and fill_amap shaders (0 feedback loops)");
}
static void sprite_tests(bool activity, bool v2) {
    enum { W=32,H=24,N=W*H*4,S=128 };
    unsigned char input[N];
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
        int i=(y*W+x)*4;
        input[i]=x<8?255:20; input[i+1]=y<6?255:30;
        input[i+2]=(x>=8&&x<24&&abs(y-12)<x/3)?255:40; input[i+3]=255;
    }
    struct parts p={0}; struct parts_common c={0};
    parts_v2=v2;
    p.local.pos=p.global.pos=(SDL_FPoint){64,64}; p.local.scale.x=1; p.local.scale.y=1;
    p.global.scale.x=p.global.scale.y=1;
    p.world_matrix_self=&p; fixture_part=&p;
    p.global.alpha=255; p.global.multiply_color=(SDL_Color){255,255,255,255};
    c.w=W; c.h=H; c.origin_offset=(Point){-W/2,-H/2};
    gfx_init_texture_with_pixels(&c.texture,W,H,input);
    // Use an RGBA offscreen target so clear/output has exact known channels.
    Texture target={0}; gfx_init_texture_blank(&target,S,S);
    for(int mags=0;mags<(activity?4:1);mags++)for(int flags=0;flags<4;flags++) {
        if (activity) {
            test_activity_scale(&p, flags&1, flags&2, mags&1?-1:1, mags&2?-1:1);
            assert(p.reverse_lr == !!(flags&1) && p.reverse_tb == !!(flags&2));
            assert(p.local.scale.x == (((!v2 && (flags&1)) ^ !!(mags&1)) ? -1 : 1));
            assert(p.local.scale.y == (((!v2 && (flags&2)) ^ !!(mags&2)) ? -1 : 1));
        } else {
            test_set_reverse(flags&1,flags&2);
        }
        GLuint fb=gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER,&target,0,0,S,S);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        test_render_cg(&p,&c);
        gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER,fb);
        unsigned char *got=gfx_get_pixels(&target);
        // Offscreen Parts projection and texture readback both use texture row order.
        for(int y=0;y<S;y++)for(int x=0;x<S;x++) {
            int ox=x-(64-W/2), oy=y-(64-H/2);
            if(ox>=0&&ox<W&&oy>=0&&oy<H) {
                int flip=flags^mags;
                int sx=(flip&1)?W-1-ox:ox, sy=(flip&2)?H-1-oy:oy;
                check_pixels(got+(y*S+x)*4,input+(sy*W+sx)*4,4,0);
            } else {
                static const unsigned char black[4]={0,0,0,255};
                check_pixels(got+(y*S+x)*4,black,4,0);
            }
        }
        char name[96]; snprintf(name,sizeof name,"sprite-%s-v%d-%d-mag%d-gles-synthetic.ppm",
                               activity?"activity":"api",v2?2:1,flags,mags);
        write_ppm(name,got,S,S); free(got); no_error();
    }
    gfx_delete_texture(&target); gfx_delete_texture(&c.texture);
    printf("PASS: actual %s v%d CG renderer normal/LR/TB/LR+TB pixels and placement (no double flip)\n",
           activity?"activity loader":"HLL API", v2?2:1);
}

/* Position tracking is inside the FLAT's coordinate system. The component
 * transform (including its origin offset) must still place mode-0 particles.
 * Mode 1 follows animated emitter keys; mode 2 keeps its captured birth point.
 */
static void flat_emitter_origin_tests(void) {
    struct string *cg_name=calloc(1,sizeof(struct string)+3);
    struct string *em_name=calloc(1,sizeof(struct string)+5);
    cg_name->size=2; em_name->size=4;
    strcpy(cg_name->text,"cg"); strcpy(em_name->text,"emit");
    unsigned char art[8*8*4];
    for(int i=0;i<8*8;i++) { art[i*4]=255; art[i*4+1]=art[i*4+2]=0; art[i*4+3]=255; }
    Texture textures[3]={{0}}; gfx_init_texture_with_pixels(&textures[0],8,8,art);
    Texture target={0}; gfx_init_texture_blank(&target,128,128);
    struct string *group_name=calloc(1,sizeof(struct string)+6);
    group_name->size=5; strcpy(group_name->text,"group");
    struct flat_library libs[3]={{.name=cg_name,.type=FLAT_LIB_CG},
        {.name=em_name,.type=FLAT_LIB_EMITTER}};
    struct flat_emitter *em=&libs[1].emitter;
    *em=(struct flat_emitter){.library_name=cg_name,.particle_align=5,
        .create_count=1,.particle_lifetime=3,.direction_type=1,
        .begin_scale=1,.end_scale=1,.begin_x_scale=1,.end_x_scale=1,
        .begin_y_scale=1,.end_y_scale=1};
    struct flat_key_data_graphic keys[3];
    for(int i=0;i<3;i++) keys[i]=(struct flat_key_data_graphic){
        .pos_x=12+i*4,.pos_y=16+i*4,.scale_x=1,.scale_y=1,
        .alpha=255,.mul_r=255,.mul_g=255,.mul_b=255};
    struct flat_timeline tl={.library_name=em_name,.type=FLAT_TIMELINE_GRAPHIC,
        .frame_count=3,.graphic={.count=3,.keys=keys}};
    struct flat fl={.hdr={.fps=60,.meter=128,.game_view_width=128,.version=11},
        .nr_libraries=3,.libraries=libs,.nr_timelines=1,.timelines=&tl};
    struct flat_key_data_graphic parent_keys[3];
    for(int i=0;i<3;i++)parent_keys[i]=(struct flat_key_data_graphic){
        .pos_x=8,.pos_y=10,.scale_x=1,.scale_y=1,
        .alpha=255,.mul_r=255,.mul_g=255,.mul_b=255};
    struct flat_timeline parent_tl={.library_name=group_name,.type=FLAT_TIMELINE_GRAPHIC,
        .frame_count=3,.graphic={.count=3,.keys=parent_keys}};
    libs[2]=(struct flat_library){.name=group_name,.type=FLAT_LIB_TIMELINE,
        .timeline={.nr_timelines=1,.timelines=&tl}};
    for(int v2=0;v2<2;v2++)for(int mode=0;mode<3;mode++)for(int placement=0;placement<2;placement++)for(int nested=0;nested<2;nested++) {
        parts_v2=v2; em->pos_track_mode=mode; em->parent_key_mode=1;
        fl.timelines=nested?&parent_tl:&tl;
        struct parts p={0}; p.local.pos=p.global.pos=(SDL_FPoint){32+placement*24,40+placement*16};
        p.local.scale.x=p.local.scale.y=p.global.scale.x=p.global.scale.y=1;
        p.global.alpha=255; p.global.multiply_color=(SDL_Color){255,255,255,255};
        p.world_matrix_self=&p;
        struct parts_flat f={.flat=&fl,.textures=textures,.nr_libraries=3,
            .common={.origin_offset={-3,-5}},.root_state=parts_flat_layer_state_new(1)};
        if(nested)f.root_state->children[0]=parts_flat_layer_state_new(1);
        for(int frame=0;frame<3;frame++) {
            f.root_state->current_frame=frame;
            if(nested)f.root_state->children[0]->current_frame=frame;
            GLuint fb=gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER,&target,0,0,128,128);
            glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
            test_render_flat(&p,&f); gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER,fb);
            unsigned char *got=gfx_get_pixels(&target);
            int key_frame=mode==1?frame:0;
            int cx=p.local.pos.x-3+(mode?keys[key_frame].pos_x:0);
            int cy=p.local.pos.y-5+(mode?keys[key_frame].pos_y:0);
            if(mode&&nested){cx+=8;cy+=10;}
            for(int y=0;y<128;y++)for(int x=0;x<128;x++) {
                unsigned char expected[4]={0,0,0,255};
                if(x>=cx-4&&x<cx+4&&y>=cy-4&&y<cy+4)expected[0]=255;
                if(memcmp(got+(y*128+x)*4,expected,4))
                    fprintf(stderr,"FLAT v2=%d mode=%d placement=%d nested=%d frame=%d pixel=(%d,%d)\n",
                        v2,mode,placement,nested,frame,x,y);
                check_pixels(got+(y*128+x)*4,expected,4,0);
            }
            free(got); no_error();
        }
        parts_flat_layer_state_free(f.root_state);
    }
    gfx_delete_texture(&target); gfx_delete_texture(&textures[0]); free(cg_name); free(em_name); free(group_name);
    puts("PASS: FLAT emitter modes 0/1/2 retain component placement across hierarchy/frames in legacy/v2 rendering");
}

int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    gles_debug_context_test();
    gles_driver_diagnostic_test();
    int attempts_before=context_attempts, debug_attempts_before=debug_context_attempts;
    gfx_create_window_and_context(SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(context_attempts==attempts_before+1 && debug_context_attempts==debug_attempts_before);
    printf("GL_VERSION=%s\nGL_RENDERER=%s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    gles_diagnostic_test();
    sdl.format=SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32); sdl.w=sdl.h=128;
    gl_initialize(); main_surface_init(128,128); gfx_draw_init(); parts_render_init(); no_error();
    job_attribute_test();
    flat_emitter_origin_tests(); shader3d_tests(); height_detector_gles3_test(); skinned_shader_gles3_test(); blur_tests(); straight_alpha_cut_test(); gray_v14_and_fill_shader_tests(); sprite_tests(false,true);
    sprite_tests(true,false); sprite_tests(true,true);
    resume_surface_tests(); replacement_context_negative_control();
    replacement_context_recovery_with_backup(); replacement_context_deferred_until_surface_ready(); replacement_context_recovery_without_backup();
    skinned_identity_bone_reset_test();
    blur_tests(); sprite_tests(false,true); no_error();
    puts("PASS: 2D copy shaders and Parts CG shader render cleanly on recovered replacement context");
    printf("PASS: %d actual GL draws; %d feedback loops\n",draws,feedbacks);
    gfx_fini(); return 0;
}
