# sources for xsystem4
xsystem4 = ['audio.c',
            'cJSON.c',
            'draw.c',
            'effect.c',
            'ffi.c',
            'fnl_render.c',
            'format.c',
            'heap.c',
            'input.c',
            'page.c',
            'resume.c',
            'sact_graphics.c',
            'savedata.c',
            'system4.c',
            'text.c',
            'video.c',
            'vm.c',

            'hll/ACXLoader.c',
            'hll/AliceLogo.c',
            'hll/AliceLogo2.c',
            'hll/AliceLogo3.c',
            'hll/Confirm2.c',
            'hll/DrawGraph.c',
            'hll/DrawPluginManager.c',
            'hll/File.c',
            'hll/InputString.c',
            'hll/Math.c',
            'hll/MsgLogManager.c',
            'hll/MsgLogViewer.c',
            'hll/MsgSkip.c',
            'hll/OutputLog.c',
            'hll/PlayMovie.c',
            'hll/SACT2.c',
            'hll/SengokuRanceFont.c',
            'hll/SystemServiceEx.c',
]

xsystem4_deps = [libm, zlib, sdl2, sdl2ttf, sdl2mixer, ffi, gl, glew, libsys4_dep]
if chibi.found()
    xsystem4_deps += chibi
    xsystem4 += 'debugger.c'
    add_project_arguments('-DDEBUGGER_ENABLED', language : 'c')
endif

flexgen = generator(flex,
                    output : '@BASENAME@.yy.c',
                    arguments : ['-o', '@OUTPUT@', '@INPUT@'])

bisongen = generator(bison,
                     output: ['@BASENAME@.tab.c', '@BASENAME@.tab.h'],
                     arguments : ['--verbose', '@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@'])

executable('xsystem4', xsystem4,
           dependencies : xsystem4_deps,
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir)

subdir('tools')
