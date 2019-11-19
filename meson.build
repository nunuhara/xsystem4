project('system4', 'c', default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
system4 = ['ain.c',
           'instructions.c',
           'string.c',
           'system.c',
           'utfsjis.c',
]
xsystem4 = ['ald.c',
            'cg.c',
            'qnt.c',
            'ini.c',
            'input.c',
            'page.c',
            'sdl_draw.c',
            'sdl_video.c',
            'system4.c',
            'vm.c',
]
hll_libs = ['hll/DrawPluginManager.c',
            'hll/Math.c',
            'hll/MsgLogManager.c',
            'hll/MsgSkip.c',
            'hll/OutputLog.c',
            'hll/SACT2.c',
]
zdep = dependency('zlib')
mdep = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2')

executable('system4', xsystem4 + system4 + hll_libs, dependencies: [mdep, zdep, sdl2])
executable('aindump', ['aindump.c'] + system4, dependencies: [mdep, zdep])
