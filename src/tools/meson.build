if build_machine.system() == 'windows'
    iconv = dependency('iconv')
    tool_deps = [libm, zlib, iconv]
else
    tool_deps = [libm, zlib]
endif

subdir('acx')
subdir('aindump')
subdir('ainedit')
subdir('aincmp')
subdir('ar')
subdir('ex')
subdir('fnl')
