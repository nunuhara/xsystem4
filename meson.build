project('system4', 'c', default_options : ['c_std=c11'])
system4 = ['system4.c', 'ain.c', 'instructions.c', 'vm.c', 'string.c', 'page.c', 'utfsjis.c', 'hll/math.c']

zdep = dependency('zlib')
mdep = meson.get_compiler('c').find_library('m', required: false)

executable('system4', system4, dependencies: [mdep, zdep])
