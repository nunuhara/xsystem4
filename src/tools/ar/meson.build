alicear = ['alice-ar.c',
]

executable('alice-ar', alicear,
           dependencies: [libm, zlib],
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           link_with : libsys4)
