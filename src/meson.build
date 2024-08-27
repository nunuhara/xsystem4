version_h = vcs_tag(command : ['git', 'describe', 'HEAD'],
                    fallback : 'unknown version',
                    input : 'version.h.in',
                    output : 'version.h')

# sources for xsystem4
xsystem4 = [version_h,
            'audio.c',
            'audio_meta.c',
            'audio_mixer.c',
            'asset_manager.c',
            'base64.c',
            'cJSON.c',
            'draw.c',
            'effect.c',
            'ffi.c',
            'font_freetype.c',
            'font_fnl.c',
            'format.c',
            'hacks.c',
            'heap.c',
            'id_pool.c',
            'input.c',
            'json.c',
            'msgqueue.c',
            'page.c',
            'resume.c',
            'savedata.c',
            'scene.c',
            'sprite.c',
            'swf.c',
            'system4.c',
            'text.c',
            'util.c',
            'video.c',
            'vm.c',

            '3d/collision.c',
            '3d/debug.c',
            '3d/model.c',
            '3d/parser.c',
            '3d/particle.c',
            '3d/reign.c',
            '3d/renderer.c',

            'dungeon/dgn.c',
            'dungeon/dtx.c',
            'dungeon/dungeon.c',
            'dungeon/map.c',
            'dungeon/polyobj.c',
            'dungeon/renderer.c',
            'dungeon/skybox.c',
            'dungeon/tes.c',

            'parts/construction.c',
            'parts/debug.c',
            'parts/flash.c',
            'parts/input.c',
            'parts/motion.c',
            'parts/parts.c',
            'parts/render.c',
            'parts/text.c',
            'parts/save.c',

            'hll/ACXLoader.c',
            'hll/ADVSYS.c',
            'hll/AliceLogo.c',
            'hll/AliceLogo2.c',
            'hll/AliceLogo3.c',
            'hll/AliceLogo4.c',
            'hll/AliceLogo5.c',
            'hll/AnteaterADVEngine.c',
            'hll/BanMisc.c',
            'hll/Bitarray.c',
            'hll/CGManager.c',
            'hll/CharSpriteManager.c',
            'hll/ChipmunkSpriteEngine.c',
            'hll/ChrLoader.c',
            'hll/CommonSystemData.c',
            'hll/Confirm.c',
            'hll/Confirm2.c',
            'hll/Confirm3.c',
            'hll/CrayfishLogViewer.c',
            'hll/Cursor.c',
            'hll/DALKDemo.c',
            'hll/DALKEDemo.c',
            'hll/Data.c',
            'hll/DataFile.c',
            'hll/DrawDungeon.c',
            'hll/DrawGraph.c',
            'hll/DrawMovie.c',
            'hll/DrawPluginManager.c',
            'hll/DrawSimpleText.c',
            'hll/File.c',
            'hll/File2.c',
            'hll/FileOperation.c',
            'hll/FillAngle.c',
            'hll/GoatGUIEngine.c',
            'hll/Gpx2Plus.c',
            'hll/GUIEngine.c',
            'hll/iarray.c',
            'hll/IbisInputEngine.c',
            'hll/InputDevice.c',
            'hll/InputString.c',
            'hll/KiwiSoundEngine.c',
            'hll/LoadCG.c',
            'hll/MainEXFile.c',
            'hll/MainSurface.c',
            'hll/MamanyoDemo.c',
            'hll/MamanyoSDemo.c',
            'hll/MarmotModelEngine.c',
            'hll/Math.c',
            'hll/MapLoader.c',
            'hll/MenuMsg.c',
            'hll/MonsterInfo.c',
            'hll/MsgLogManager.c',
            'hll/MsgLogViewer.c',
            'hll/MsgSkip.c',
            'hll/NewFont.c',
            'hll/OutputLog.c',
            'hll/PassRegister.c',
            'hll/PartsEngine.c',
            'hll/PixelRestore.c',
            'hll/PlayDemo.c',
            'hll/PlayMovie.c',
            'hll/ReignEngine.c',
            'hll/SACT2.c',
            'hll/SengokuRanceFont.c',
            'hll/SoundFilePlayer.c',
            'hll/Sound2ex.c',
            'hll/StoatSpriteEngine.c',
            'hll/StretchHelper.c',
            'hll/SystemService.c',
            'hll/SystemServiceEx.c',
            'hll/Timer.c',
            'hll/Toushin3Loader.c',
            'hll/vmAnime.c',
            'hll/vmArray.c',
            'hll/vmCG.c',
            'hll/vmChrLoader.c',
            'hll/vmCursor.c',
            'hll/vmDalkGaiden.c',
            'hll/vmData.c',
            'hll/vmDialog.c',
            'hll/vmDrawGauge.c',
            'hll/vmDrawMsg.c',
            'hll/vmDrawNumber.c',
            'hll/vmFile.c',
            'hll/vmGraph.c',
            'hll/vmGraphQuake.c',
            'hll/vmKey.c',
            'hll/vmMapLoader.c',
            'hll/vmMsgLog.c',
            'hll/vmMusic.c',
            'hll/vmSound.c',
            'hll/vmSprite.c',
            'hll/vmString.c',
            'hll/vmSurface.c',
            'hll/vmSystem.c',
            'hll/vmTimer.c',
            'hll/VSFile.c',
]

xsystem4_deps = [libm, zlib, sdl2, ft2, ffi, cglm, sndfile, libsys4_dep] + gl_deps

if avcodec.found() and avformat.found() and avutil.found() and swscale.found()
    xsystem4 += 'movie_ffmpeg.c'
    xsystem4_deps += [avcodec, avformat, avutil, swscale]
else
    xsystem4 += 'movie_plmpeg.c'
endif

if get_option('debugger').allowed()
    add_project_arguments('-DDEBUGGER_ENABLED', language : 'c')
    xsystem4 += 'debug.c'
    xsystem4 += 'debugger_cmd.c'
    xsystem4 += 'debugger_dap.c'
    if chibi.found()
        xsystem4_deps += chibi
        xsystem4 += 'debugger_scm.c'
        add_project_arguments('-DHAVE_SCHEME', language : 'c')
    endif
    if readline.found()
        xsystem4_deps += readline
        add_project_arguments('-DHAVE_READLINE', language : 'c')
    endif
endif

static_link_args = []
if host_machine.system() == 'windows'
    static_link_args = ['-static', '-lstdc++']
endif

winsys = 'console'
if get_option('buildtype').startswith('release')
    winsys = 'windows'
endif

executable('xsystem4', xsystem4,
           dependencies : xsystem4_deps,
           c_args : ['-Wno-unused-parameter'],
           link_args : static_link_args,
           include_directories : incdir,
           win_subsystem : winsys,
           install : true)
