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
            'mt19937int.c',
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
            'hll/BanMisc.c',
            'hll/Bitarray.c',
            'hll/Confirm.c',
            'hll/Confirm2.c',
            'hll/DataFile.c',
            'hll/DrawGraph.c',
            'hll/DrawPluginManager.c',
            'hll/File.c',
            'hll/Gpx2Plus.c',
            'hll/InputDevice.c',
            'hll/InputString.c',
            'hll/Math.c',
            'hll/MenuMsg.c',
            'hll/MsgLogManager.c',
            'hll/MsgLogViewer.c',
            'hll/MsgSkip.c',
            'hll/OutputLog.c',
            'hll/PlayDemo.c',
            'hll/PlayMovie.c',
            'hll/SACT2.c',
            'hll/SengokuRanceFont.c',
            'hll/SystemServiceEx.c',
            'hll/Timer.c',
]

xsystem4_deps = [libm, zlib, sdl2, sdl2ttf, sdl2mixer, ffi, gl, glew, libsys4_dep]
if chibi.found()
    xsystem4_deps += chibi
    xsystem4 += 'debugger.c'
    add_project_arguments('-DDEBUGGER_ENABLED', language : 'c')
endif

executable('xsystem4', xsystem4,
           dependencies : xsystem4_deps,
           c_args : ['-Wno-unused-parameter'],
           include_directories : incdir,
           install : true)
