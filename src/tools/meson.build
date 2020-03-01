
flex = find_program('flex', required: false)
bison = find_program('bison', required: false)

if flex.found()
    flexgen = generator(flex,
                        output : '@BASENAME@.yy.c',
                        arguments : ['-o', '@OUTPUT@', '@INPUT@'])
endif

if bison.found()
    bisongen = generator(bison,
                         output: ['@BASENAME@.tab.c', '@BASENAME@.tab.h'],
                         arguments : ['@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@'])
endif

if build_machine.system() == 'windows'
    iconv = dependency('iconv')
    tool_deps = [libm, zlib, iconv]
else
    tool_deps = [libm, zlib]
endif

subdir('aindump')
subdir('ainedit')
subdir('ar')
subdir('ex')
