project('system4', 'c', default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

system4 = ['ain.c',
           'instructions.c',
           'string.c',
           'system.c',
           'utfsjis.c',
]

xsystem4 = ['ald.c',
            'cJSON.c',
            'cg.c',
            'qnt.c',
            'ini.c',
            'input.c',
            'page.c',
            'sact_graphics.c',
            'savedata.c',
            'sdl_draw.c',
            'sdl_text.c',
            'sdl_video.c',
            'system4.c',
            'vm.c',

            'hll/ACXLoader.c',
            'hll/AliceLogo.c',
            'hll/AliceLogo2.c',
            'hll/AliceLogo3.c',
            'hll/Confirm2.c',
            'hll/DrawGraph.c',
            'hll/DrawPluginManager.c',
            'hll/Math.c',
            'hll/MsgLogManager.c',
            'hll/MsgSkip.c',
            'hll/OutputLog.c',
            'hll/PlayMovie.c',
            'hll/SACT2.c',
            'hll/SystemServiceEx.c',
]

zlib = dependency('zlib')
libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2')
sdl2ttf = dependency('SDL2_ttf')
xsystem4_deps = [libm, zlib, sdl2, sdl2ttf]

chibi = dependency('chibi-scheme', required : get_option('debugger'))
if chibi.found()
    xsystem4_deps += chibi
    xsystem4 += 'debugger.c'
    add_project_arguments('-DDEBUGGER_ENABLED', language : 'c')
endif

executable('system4', xsystem4 + system4, dependencies: xsystem4_deps, c_args : ['-Wno-unused-parameter'])
executable('aindump', ['aindump.c'] + system4, dependencies: [libm, zlib])
