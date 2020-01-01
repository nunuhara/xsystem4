project('system4', 'c',
        default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

zlib = dependency('zlib')
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2')
sdl2ttf = dependency('SDL2_ttf')
sdl2mixer = dependency('SDL2_mixer')
gl = dependency('GL')
glew = dependency('glew')
chibi = dependency('chibi-scheme', required : get_option('debugger'))

incdir = include_directories('include')

subdir('src')
