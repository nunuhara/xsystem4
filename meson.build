project('system4', 'c',
        default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DXSYS4_DATA_DIR="' + get_option('prefix') / 'share' / 'xsystem4"', language : 'c')

zlib = dependency('zlib')
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2')
sdl2ttf = dependency('SDL2_ttf')
sdl2mixer = dependency('SDL2_mixer')
ffi = dependency('libffi')
gl = dependency('GL')
glew = dependency('glew')
tj = dependency('libturbojpeg')
webp = dependency('libwebp')
png = dependency('libpng')
chibi = dependency('chibi-scheme', required : get_option('debugger'))

flex = find_program('flex')
bison = find_program('bison')

incdir = include_directories('include')

libsys4_proj = subproject('libsys4')
libsys4_dep = libsys4_proj.get_variable('libsys4_dep')

subdir('src')

install_subdir('shaders', install_dir : 'share' / 'xsystem4')
install_subdir('fonts', install_dir : 'share' / 'xsystem4')
