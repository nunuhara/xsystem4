#!/usr/bin/env python3
"""Host test of single-part serialization with resource/rendering fixtures.

Uses the production part record, parameter and widget serializers. GPU state,
message windows and motion resources are fixtures, not rendered game coverage.
Run: python3 xsystem4/test/native/test_parts_clone_regressions.py
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r"^[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source, re.MULTILINE)
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

FIXTURE = r'''
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/page.h"
#include "vm/heap.h"
#include "vm/v12.h"
#include "parts.h"
#include "parts/parts_internal.h"
#include "hll/iarray.h"
#define CURRENT_SAVE_VERSION 35
#define MIN_SINGLE_PART_SAVE_VERSION 32
#define PARTS_SAVED_FONTS (PARTS_NR_STATES + 1)
struct numeral_font_map { int source, target; };
bool parts_v2 = true, parts_multi_controller = false;
static struct parts_context context={.list=TAILQ_HEAD_INITIALIZER(context.list)};
struct parts_context *parts_current=&context;
#define parts_list (parts_current->list)
static void save_parts_numeral(struct iarray_writer *,struct parts_numeral *);
static void load_parts_numeral(struct iarray_reader *,struct parts *,struct parts_numeral *,
        int,const struct numeral_font_map *);
struct string EMPTY_STRING;
static struct parts *objects[8];
static int updates, rebuilds;
static SDL_FPoint root_pos={10,20};
static int sorted_z;
static SDL_FPoint hitbox_pos;
static struct ain program = {.version=14};
struct ain *ain = &program;
int PE_GetComponentType(int n,int s) { (void)n; (void)s; return 11; }
void *_xcalloc(size_t n,size_t s,const char *f) { (void)f; return calloc(n,s); }
void *_xmalloc(size_t n,const char *f) { (void)f; return malloc(n); }
void *xrealloc_array(void *p,size_t old,size_t n,size_t size) {
    (void)old; return realloc(p,n*size);
}
struct string *make_string(const char *s, size_t n) {
    struct string *r = calloc(1, sizeof(*r)+n+1);
    r->size=n; r->ref=1; memcpy(r->text,s,n); return r;
}
struct string *string_ref(struct string *s) {
    return make_string(s->text,s->size);
}
void free_string(struct string *s) { if(s != &EMPTY_STRING) free(s); }
void iarray_init_writer(struct iarray_writer *w,const char *s) {
    *w=(struct iarray_writer){0}; do { iarray_write(w,*s); } while(*s++);
}
void iarray_write(struct iarray_writer *w,int v) {
    w->data=realloc(w->data,(w->size+1)*sizeof(int)); w->data[w->size++]=v;
}
void iarray_write_at(struct iarray_writer *w,unsigned p,int v) { w->data[p]=v; }
void iarray_free_writer(struct iarray_writer *w) { free(w->data); }
struct page *alloc_page(enum page_type t,int index,int n) {
    struct page *p=calloc(1,sizeof(*p)+n*sizeof(union vm_value));
    p->type=t; p->index=index; p->nr_vars=n; return p;
}
_Noreturn void _vm_error(const char *fmt,...) { (void)fmt; abort(); }
void sys_warning(const char *fmt,...) { (void)fmt; abort(); }
int ain_type_slot_width(const struct ain_type *t) { assert(t->data==AIN_INT); return 1; }
int variable_init_typed(union vm_value *v,const struct ain_type *t,bool a,bool b) {
    (void)t; (void)a; (void)b; v->i=0; return 1;
}
union vm_value variable_initval(enum ain_data_type t) { (void)t; return (union vm_value){.i=0}; }
int alloc_struct(int n) { (void)n; abort(); }
void init_struct(int n,int slot) { (void)n; (void)slot; abort(); }
void create_struct(int n,union vm_value *v) { (void)n; (void)v; abort(); }
int32_t heap_alloc_slot(enum vm_pointer_type t) { (void)t; abort(); }
void heap_set_page(int slot,struct page *p) { (void)slot; (void)p; abort(); }
bool iarray_init_reader(struct iarray_reader *r,struct page *p,const char *s) {
    *r=(struct iarray_reader){.data=p->values,.size=p->nr_vars};
    do { if(iarray_read(r)!=*s) return false; } while(*s++); return !r->error;
}
int iarray_read(struct iarray_reader *r) {
    if(r->pos>=r->size) { r->error=1; return 0; } return r->data[r->pos++].i;
}
void iarray_write_float(struct iarray_writer *w,float f) {
    union vm_value v={.f=f}; iarray_write(w,v.i);
}
float iarray_read_float(struct iarray_reader *r) {
    union vm_value v={.i=iarray_read(r)}; return v.f;
}
void iarray_write_string(struct iarray_writer *w,struct string *s) {
    for(int i=0;s && i<s->size;i++) iarray_write(w,s->text[i]); iarray_write(w,0);
}
struct string *iarray_read_string(struct iarray_reader *r) {
    char s[256]; int n=0,c; while((c=iarray_read(r))) { assert(n<255); s[n++]=c; }
    return make_string(s,n);
}
void iarray_write_string_or_null(struct iarray_writer *w,struct string *s) { iarray_write_string(w,s); }
struct string *iarray_read_string_or_null(struct iarray_reader *r) {
    struct string *s = iarray_read_string(r);
    if (!s->size) { free_string(s); return NULL; }
    return s;
}
void iarray_write_color(struct iarray_writer *w,SDL_Color *c) {
    iarray_write(w,c->r); iarray_write(w,c->g); iarray_write(w,c->b); iarray_write(w,c->a);
}
void iarray_read_color(struct iarray_reader *r,SDL_Color *c) {
    c->r=iarray_read(r); c->g=iarray_read(r); c->b=iarray_read(r); c->a=iarray_read(r);
}
void iarray_write_text_style(struct iarray_writer *w,struct text_style *s) { (void)w; (void)s; }
void iarray_read_text_style(struct iarray_reader *r,struct text_style *s) { (void)r; (void)s; }
void delete_page_vars(struct page *p) { (void)p; }
void free_page(struct page *p) { free(p); }
struct parts *parts_try_get(int n) { return n>=0 && n<8 ? objects[n]:NULL; }
struct parts *parts_get(int n) {
    assert(n>=0 && n<8);
    if(!objects[n]) {
        objects[n]=calloc(1,sizeof(struct parts)); objects[n]->no=n;
        TAILQ_INIT(&objects[n]->children); TAILQ_INIT(&objects[n]->motion);
        TAILQ_INSERT_TAIL(&parts_list,objects[n],parts_list_entry);
    }
    return objects[n];
}
void parts_release(int n) {
    struct parts *p=parts_try_get(n); if(!p) return;
    TAILQ_REMOVE(&parts_list,p,parts_list_entry);
    free_string(p->comment); free_string(p->user_component_name);
    free_string(p->text_box_text);
    for(int i=0;i<3;i++) if(p->states[i].type==PARTS_NUMERAL) free_string(p->states[i].num.cg_name);
    if(p->message_box) {
        if(p->message_box->key_wait.type==PARTS_NUMERAL) free_string(p->message_box->key_wait.num.cg_name);
        free(p->message_box);
    }
    for(int i=0;i<3;i++) free_string(p->sound_names[i]);
    if(p->widget) {
        free_string(p->widget->text); free_string(p->widget->cg_name); free_string(p->widget->flat_name);
        for(int i=0;i<2;i++) { free_string(p->widget->override_cg[i]); free_string(p->widget->override_flat[i]); }
        free(p->widget);
    }
    free(p->radio_children); free(p); objects[n]=NULL;
}
void PE_UpdateComponent(int t) { (void)t; updates++; }
void parts_cg_async_flush_part(int parts_no) { (void)parts_no; }
void parts_widget_rebuild(struct parts *p) { assert(p->component_type==0); rebuilds++; }
void parts_state_reset(struct parts_state *s,enum parts_type t) { memset(s,0,sizeof(*s)); s->type=t; }
void parts_panel_rebuild(struct parts *p) { (void)p; }
void parts_scrollbar_rebuild(struct parts *p) { (void)p; }
void parts_list_resort(struct parts *p) { sorted_z=p->global.z; }
void parts_component_dirty(struct parts *p) { (void)p; }
void parts_recalculate_hitbox(struct parts *p) { hitbox_pos=p->global.pos; }
void parts_dirty(struct parts *p) { (void)p; }
static void parts_update_global_pos(struct parts *p,SDL_FPoint pos) {
    p->global.pos=(SDL_FPoint){pos.x+p->local.pos.x,pos.y+p->local.pos.y};
}
void parts_numeral_font_init(struct parts_numeral_font *f) {
    for(int i=0;i<12;i++) { f->cg[i].w=f->width[i]; f->cg[i].h=16; }
}
int parts_load_numeral_font_separate_string(struct string *cg_name) { (void)cg_name; return -1; }
int parts_load_numeral_font_combined_by_name(struct string *cg_name, int w[12]) { (void)cg_name; (void)w; return -1; }
struct text_style parts_get_effective_text_style(const struct text_style *ts) { return *ts; }
float gfx_size_char(struct text_style *ts,const char *s) { (void)ts; (void)s; return 8; }
char *utf2sjis(const char *s,size_t n) { return strndup(s,n); }
void gfx_delete_texture(Texture *t) { t->handle=0; }
void gfx_init_texture_rgba(Texture *t,int w,int h,SDL_Color c) {
    (void)c; t->handle=1; t->w=w; t->h=h;
}
void gfx_begin_draw_target(Texture *t) { (void)t; }
void gfx_end_draw_target(void) {}
float gfx_render_textf(Texture *t,float x,int y,char *s,struct text_style *ts,bool b) {
    (void)t; (void)x; (void)y; (void)s; (void)ts; (void)b; return 0;
}
void gfx_copy_with_alpha_map(Texture *d,int x,int y,Texture *s,int a,int b,int w,int h) {
    (void)d; (void)x; (void)y; (void)s; (void)a; (void)b; (void)w; (void)h;
}
void parts_set_dims(struct parts *p,struct parts_common *c,int w,int h) {
    (void)p; c->w=w; c->h=h;
}
void PE_SuspendTexture(int n) { (void)n; }
void parts_add_motion(struct parts *p,struct parts_motion *m) { (void)p; (void)m; abort(); }
static void save_parts_state(struct iarray_writer *w,struct parts *p,struct parts_state *s) {
    (void)p; iarray_write(w,s->type); iarray_write(w,s->common.w);
    if(s->type==PARTS_NUMERAL) save_parts_numeral(w,&s->num);
}
static void load_parts_state(struct iarray_reader *r,struct parts *p,struct parts_state *s,int v,
        const struct numeral_font_map *fonts) {
    s->type=iarray_read(r); s->common.w=iarray_read(r);
    if(s->type==PARTS_NUMERAL) load_parts_numeral(r,p,&s->num,v,fonts);
}
static void save_message_window(struct iarray_writer *w,struct parts *p,struct parts_message_window *m) {
    iarray_write(w,m!=NULL); if(m) save_parts_state(w,p,&m->key_wait);
}
static void load_message_window(struct iarray_reader *r,struct parts *p,int v,
        const struct numeral_font_map *f) {
    if(iarray_read(r)) {
        p->message_box=calloc(1,sizeof(*p->message_box));
        load_parts_state(r,p,&p->message_box->key_wait,v,f);
    }
}
static void save_parts_motion(struct iarray_writer *w,struct parts_motion *m) { (void)w; (void)m; abort(); }
static struct parts_motion *load_parts_motion(struct iarray_reader *r) { (void)r; abort(); }
'''

TEST = r'''
static void roundtrip(int version) {
    program.version=version; rebuilds=0;
    struct page *buf=NULL;
    assert(!PE_SaveParts(7,&buf)); assert(!buf);
    assert(!PE_LoadParts(2,&buf)); assert(!PE_LoadParts(2,NULL));
    struct parts *src=parts_get(1), *parent=parts_get(3), *child=parts_get(4);
    src->parent=parent; TAILQ_INSERT_TAIL(&src->children,child,child_list_entry);
    src->comment=make_string("button",6); src->component_type=0;
    src->widget=calloc(1,sizeof(*src->widget));
    src->widget->enabled=true; src->widget->width=321; src->widget->height=45;
    src->widget->checked=true; src->widget->button_mode=true;
    src->widget->text=make_string("label",5);
    parent->global.show=false; parent->global.alpha=40; parent->global.z=90;
    src->local.pos.x=17.5f; src->local.scale.x=1.25f; src->local.alpha=123;
    src->local.z=7; src->local.show=true;
    src->global=src->local; src->global.show=false; src->global.alpha=19; src->global.z=97;
    src->reverse_lr=true; src->state=2;
    for(int i=0;i<3;i++) { src->states[i].type=PARTS_CG; src->states[i].common.w=50+i; }
    assert(PE_SaveParts(1,&buf));
    assert(buf->a_type==(version>=11 ? AIN_ARRAY : AIN_ARRAY_INT));
    int source_no=buf->values[8].i;
    assert(PE_LoadParts(2,&buf));
    struct parts *dst=parts_get(2);
    assert(dst->no==2 && dst->parent==NULL && dst->pending_parent==-1);
    assert(TAILQ_EMPTY(&dst->children) && TAILQ_FIRST(&src->children)==child);
    assert(src->parent==parent && buf->values[8].i==source_no);
    parts_set_show(dst,true);
    assert(dst->global.show && dst->global.alpha==123 && dst->global.z==7 && sorted_z==7);
    assert(dst->global.pos.x==27.5f && dst->global.pos.y==20);
    assert(hitbox_pos.x==27.5f && hitbox_pos.y==20);
    assert(!src->global.show && src->global.alpha==19 && src->global.z==97);
    assert(dst->widget && dst->component_type==0 && PE_GetSpecialComponentType(2)==0);
    assert(PartsEngine_GetComponentType_v12(2,1)==0);
    assert(dst->widget->width==321 && dst->widget->height==45);
    assert(dst->widget->checked && dst->widget->button_mode);
    assert(!strcmp(dst->widget->text->text,"label"));
    assert(!strcmp(dst->comment->text,"button"));
    assert(dst->local.pos.x==17.5f && dst->local.scale.x==1.25f && dst->local.alpha==123);
    assert(dst->reverse_lr && dst->state==2);
    for(int i=0;i<3;i++) assert(dst->states[i].common.w==50+i);
    assert(PE_LoadParts(2,&buf)); assert(rebuilds==2);
    assert(PE_SaveParts(1,&buf));
    int save_version=buf->values[5].i; buf->values[5].i=save_version+1;
    assert(!PE_LoadParts(2,&buf)); assert(parts_try_get(2)); buf->values[5].i=save_version;
    buf->nr_vars--; assert(!PE_LoadParts(2,&buf)); buf->nr_vars++;
    buf->array.rank=2; assert(!PE_LoadParts(2,&buf)); buf->array.rank=1;
    int atype=buf->a_type; buf->a_type=AIN_ARRAY_FLOAT;
    assert(!PE_LoadParts(2,&buf)); buf->a_type=atype;
    if(atype==AIN_ARRAY) {
        buf->array.element_type.data=AIN_FLOAT; assert(!PE_LoadParts(2,&buf));
        buf->array.element_type.data=AIN_INT;
    }
    int count=buf->nr_vars; buf->nr_vars=0; assert(!PE_LoadParts(2,&buf)); buf->nr_vars=count;
    buf->values[0].i='!'; assert(!PE_LoadParts(2,&buf));
    assert(!PE_LoadParts(0,&buf));
    free_page(buf); for(int i=0;i<8;i++) parts_release(i);
    assert(updates>=4);
}
static void text_box_roundtrip(void) {
    struct parts *src=parts_get(1);
    src->component_type=4;
    src->text_box_text=make_string("value",5);
    src->text_box_read_only=true;
    src->text_box_max_length=20;
    struct page *buf=NULL;
    assert(PE_SaveParts(1,&buf));
    assert(PE_LoadParts(2,&buf));
    struct parts *dst=parts_get(2);
    assert(dst->component_type==4 && dst->text_box_read_only && dst->text_box_max_length==20);
    assert(!strcmp(dst->text_box_text->text,"value"));

    // Downgrade this record by removing the two v33 values before the empty
    // motion list and empty comment.
    assert(buf->values[buf->nr_vars-4].i==1 && buf->values[buf->nr_vars-3].i==20);
    memmove(buf->values+buf->nr_vars-4,buf->values+buf->nr_vars-2,2*sizeof(*buf->values));
    buf->nr_vars-=2;
    buf->values[5].i=32;
    buf->values[6].i=buf->nr_vars;
    assert(PE_LoadParts(2,&buf));
    dst=parts_get(2);
    assert(!dst->text_box_read_only && dst->text_box_max_length==0);
    assert(!strcmp(dst->text_box_text->text,"value"));
    free_page(buf);
    parts_release(1); parts_release(2);
}
static void numeral_roundtrip(void) {
    program.version=14;
    parts_nr_numeral_fonts=2;
    parts_numeral_fonts=calloc(2,sizeof(*parts_numeral_fonts));
    for(int i=0;i<2;i++) {
        parts_numeral_fonts[i].type=i;
        parts_numeral_fonts[i].cg_no=100+i;
        for(int j=0;j<12;j++) parts_numeral_fonts[i].width[j]=5+i;
    }
    struct parts *src=parts_get(1);
    src->states[0].type=PARTS_NUMERAL;
    src->states[0].num=(struct parts_numeral){.have_num=true,.num=42,.font_no=1,.show_type=1};
    src->message_box=calloc(1,sizeof(*src->message_box));
    src->message_box->key_wait.type=PARTS_NUMERAL;
    src->message_box->key_wait.num=(struct parts_numeral){.have_num=true,.num=7,.font_no=0};
    struct page *buf=NULL;
    assert(PE_SaveParts(1,&buf));
    PE_ReleaseAllParts();
    assert(!parts_numeral_fonts && !parts_nr_numeral_fonts);
    struct parts_numeral invalid={.have_num=true,.font_no=9};
    assert(!parts_numeral_update(parts_get(3),&invalid));
    invalid.show_type=2;
    assert(parts_numeral_update(parts_get(3),&invalid));
    parts_nr_numeral_fonts=1;
    parts_numeral_fonts=calloc(1,sizeof(*parts_numeral_fonts));
    parts_numeral_fonts[0].cg_no=999;
    assert(PE_LoadParts(2,&buf));
    struct parts *dst=parts_get(2);
    assert(parts_nr_numeral_fonts==3 && parts_numeral_fonts[0].cg_no==999);
    assert(dst->states[0].num.font_no==1 && dst->states[0].common.w==12);
    assert(parts_numeral_fonts[1].cg_no==101 && parts_numeral_fonts[1].width[0]==6);
    assert(dst->message_box->key_wait.num.font_no==2);
    assert(parts_numeral_fonts[2].cg_no==100 && dst->message_box->key_wait.common.w==5);
    assert(PE_LoadParts(2,&buf) && parts_nr_numeral_fonts==3);
    PE_ReleaseAllParts();
    assert(PE_LoadParts(2,&buf));
    assert(parts_nr_numeral_fonts==2 && parts_get(2)->states[0].num.font_no==0);
    int type=buf->values[9].i; buf->values[9].i=99;
    assert(!PE_LoadParts(2,&buf)); buf->values[9].i=type;
    int source=buf->values[8].i; buf->values[8].i=99;
    assert(!PE_LoadParts(2,&buf)); buf->values[8].i=source;
    struct parts *bad=parts_get(5); bad->states[0].type=PARTS_NUMERAL;
    bad->states[0].num.font_no=999;
    struct page *empty=NULL; assert(!PE_SaveParts(5,&empty) && !empty);
    free_page(buf); PE_ReleaseAllParts();
    puts("numeral fonts survive ReleaseAllParts, remap, deduplicate and reject invalid indices: OK");
}
int main(void) {
    roundtrip(10); roundtrip(12); roundtrip(14);
    text_box_roundtrip();
    numeral_roundtrip();
    puts("detached visibility, alpha, Z, root position, sort and hitbox refresh: OK");
    puts("single-part save/load: legacy, AIN v12 and v14 roundtrips and invalid buffers: OK");
}
'''


class PartsCloneTests(unittest.TestCase):
    def test_single_part_roundtrip(self):
        names = ["save_parts_params", "load_parts_params", "save_parts", "load_parts",
                 "PE_SaveParts", "PE_LoadParts"]
        code = FIXTURE
        for path, funcs in [
            ("src/page.c", ["array_type", "unref_array_type", "alloc_array"]),
            ("src/vm_v12.c", ["type_stride", "v12_array_new"]),
            ("src/hll/iarray.c", ["iarray_to_page"]),
            ("src/parts/parts.c", ["parts_numeral_update", "parts_numeral_set_number",
                                  "parts_set_posf", "parts_update_global_show", "parts_set_show",
                                  "parts_release_all", "PE_ReleaseAllParts"]),
            ("src/parts/save.c", ["save_parts_numeral", "load_parts_numeral",
                                 "save_part_fonts", "load_part_fonts"]),
        ]:
            code += "\n".join(function(path, n) for n in funcs)
        code += "\n".join(function("src/parts/save.c", n) for n in names)
        code += function("src/parts/activity.c", "PE_GetSpecialComponentType")
        code += function("src/hll/PartsEngine.c", "PartsEngine_GetComponentType_v12") + TEST
        flags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
            src.write_text(code)
            subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-DUSE_GLES", "-Wall", "-Wextra",
                            "-Werror", "-Wno-misleading-indentation", *flags,
                            f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}",
                            f"-I{ROOT / 'subprojects/libsys4/include'}", str(src), "-lm", "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
