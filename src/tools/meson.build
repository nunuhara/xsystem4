if build_machine.system() == 'windows'
    iconv = dependency('iconv')
    tool_deps = [libm, zlib, iconv, libsys4_dep]
else
    tool_deps = [libm, zlib, libsys4_dep]
endif

subdir('acx')
subdir('aindump')
subdir('ainedit')
subdir('aincmp')
subdir('ar')
subdir('ex')
subdir('fnl')
