project('system4', 'c',
        default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DXSYS4_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'xsystem4"', language : 'c')

zlib = dependency('zlib')
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2')
ft2 = dependency('freetype2')
ffi = dependency('libffi')
tj = dependency('libturbojpeg')
webp = dependency('libwebp')
png = dependency('libpng')
cglm = dependency('cglm', fallback : ['cglm', 'cglm_dep'])
sndfile = dependency('sndfile')

gles = dependency('glesv2', version : '>=3', required: get_option('opengles'))
if gles.found()
    gl_deps = [gles]
    add_project_arguments('-DUSE_GLES', language : 'c')
else
    gl = dependency('GL')
    glew = dependency('glew')
    gl_deps = [gl, glew]
endif

chibi = dependency('chibi-scheme', required : false)
readline = dependency('readline', required : false)

flex = find_program('flex')
bison = find_program('bison')

incdir = include_directories('include')

libsys4_proj = subproject('libsys4')
libsys4_dep = libsys4_proj.get_variable('libsys4_dep')

subdir('src')

install_subdir('shaders', install_dir : get_option('datadir') / 'xsystem4')
install_subdir('fonts', install_dir : get_option('datadir') / 'xsystem4')
install_data('debugger.scm', install_dir : get_option('datadir') / 'xsystem4')
