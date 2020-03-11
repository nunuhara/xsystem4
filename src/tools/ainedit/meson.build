# sources for ainedit
ainedit = ['../../cJSON.c',
           '../conv.c',
           'ainedit.c',
           'asm.c',
           'json.c',
           'repack.c',
           'text.c',
           'transcode.c',
]

ainedit += flexgen.process('asm_lexer.l', 'text_lexer.l')
ainedit += bisongen.process('asm_parser.y', 'text_parser.y')

executable('ainedit', ainedit,
           dependencies : tool_deps,
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)
