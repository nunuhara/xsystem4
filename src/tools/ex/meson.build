exdump = ['../conv.c',
          'dump.c',
          'exdump.c',
]

lfiles = flexgen.process('ex_lexer.l')
pfiles = bisongen.process('ex_parser.y')

exbuild = ['../conv.c',
           'ast.c',
           'exbuild.c',
           'pack.c',
]

executable('exdump', exdump,
           dependencies : [libm, zlib],
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)

executable('exbuild', exbuild, lfiles, pfiles,
           dependencies : [libm, zlib],
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)
