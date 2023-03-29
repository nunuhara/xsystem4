project('system4', 'c',
        default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DXSYS4_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'xsystem4"', language : 'c')

static_libs = false
if host_machine.system() == 'windows'
    static_libs = true
endif

zlib = dependency('zlib', static : static_libs)
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2', static : static_libs)
ft2 = dependency('freetype2', static : static_libs)
ffi = dependency('libffi', static : static_libs)
tj = dependency('libturbojpeg', static : static_libs)
webp = dependency('libwebp', static : static_libs)
png = dependency('libpng', static : static_libs)
cglm = dependency('cglm', fallback : ['cglm', 'cglm_dep'])
sndfile = dependency('sndfile', static : static_libs)

gles = dependency('glesv2', version : '>=3', required: get_option('opengles'))
if gles.found()
    gl_deps = [gles]
    add_project_arguments('-DUSE_GLES', language : 'c')
else
    gl = dependency('GL')
    glew = dependency('glew')
    gl_deps = [gl, glew]
endif

chibi = dependency('chibi-scheme', required : false, static : static_libs)
readline = dependency('readline', required : false, static : static_libs)

flex = find_program('flex')
bison = find_program('bison')

incdir = include_directories('include')

libsys4_proj = subproject('libsys4')
libsys4_dep = libsys4_proj.get_variable('libsys4_dep')

subdir('src')

install_subdir('shaders', install_dir : get_option('datadir') / 'xsystem4')
install_subdir('fonts', install_dir : get_option('datadir') / 'xsystem4')
install_data('debugger.scm', install_dir : get_option('datadir') / 'xsystem4')
