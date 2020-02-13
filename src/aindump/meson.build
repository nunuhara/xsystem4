aindump = ['../cJSON.c',
           'aindump.c',
           'dasm.c',
           'guess_filenames.c',
           'json.c',
]

executable('aindump', aindump,
           dependencies : [libm, zlib],
           include_directories : incdir,
           link_with : libsys4)
