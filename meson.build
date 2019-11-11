project('system4', 'c', default_options : ['c_std=c11 -Wall -Wextra'])
system4 = ['ain.c',
           'instructions.c',
           'string.c',
           'system.c',
           'utfsjis.c',
]
xsystem4 = ['ini.c',
            'page.c',
            'system4.c',
            'vm.c',
]
hll_libs = ['hll/Math.c',
            'hll/OutputLog.c',
            'hll/SACT2.c',
]
zdep = dependency('zlib')
mdep = meson.get_compiler('c').find_library('m', required: false)

executable('system4', xsystem4 + system4 + hll_libs, dependencies: [mdep, zdep])
executable('aindump', ['aindump.c'] + system4, dependencies: [mdep, zdep])
