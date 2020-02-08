# sources for ainedit
ainedit = ['../cJSON.c',
           'ainedit.c',
           'asm.c',
           'json.c',
           'repack.c',
           #'ainedit/parser.c',
           #'ainedit/tokens.c',
]

if flex.found()
    lgen = generator(flex,
                     output : '@BASENAME@.yy.c',
                     arguments : ['-o', '@OUTPUT@', '@INPUT@'])
    lfiles = lgen.process('asm_lexer.l')
else
    lfiles = 'asm_lexer.yy.c'
endif

if bison.found()
    pgen = generator(bison,
                     output: ['@BASENAME@.tab.c', '@BASENAME@.tab.h'],
                     arguments : ['@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@'])
    pfiles = pgen.process('asm_parser.y')
else
    pfiles = ['asm_parser.tab.c', 'asm_parser.tab.h']
endif

executable('ainedit', ainedit, lfiles, pfiles,
           dependencies : [libm, zlib],
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)
