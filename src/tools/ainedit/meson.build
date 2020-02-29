# sources for ainedit
ainedit = ['../../cJSON.c',
           '../conv.c',
           'ainedit.c',
           'asm.c',
           'json.c',
           'repack.c',
           'text.c',
]

if flex.found()
    lfiles = flexgen.process('asm_lexer.l', 'text_lexer.l')
else
    lfiles = ['asm_lexer.yy.c', 'text_lexer.yy.c']
endif

if bison.found()
    pfiles = bisongen.process('asm_parser.y', 'text_parser.y')
else
    pfiles = ['asm_parser.tab.c', 'asm_parser.tab.h', 'text_parser.tab.c', 'text_parser.tab.h']
endif


executable('ainedit', ainedit, lfiles, pfiles,
           dependencies : [libm, zlib],
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)
