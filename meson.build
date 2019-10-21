project('system4', 'c', default_options : ['c_std=c11'])
system4 = ['system4.c', 'ain.c', 'instructions.c', 'vm.c', 'string.c', 'page.c', 'utfsjis.c']
zdep = dependency('zlib')
executable('system4', system4, dependencies: zdep)
