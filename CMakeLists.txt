# CMakeLists.txt for Android build

cmake_minimum_required(VERSION 3.14)
project(xsystem4 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# Apply to the engine and its fetched SDL/cglm shared libraries. Some NDK
# toolchain modes ignore ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES.
if(ANDROID)
  add_link_options("LINKER:-z,max-page-size=16384")
endif()

include(FetchContent)
if(ALICEHUB_IOS OR NINTENDO_SWITCH)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
  set(CGLM_STATIC ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
  cglm
  URL https://github.com/recp/cglm/archive/refs/tags/v0.9.2.tar.gz
  URL_HASH SHA1=cb8472aa8c2ab67b66378dbaf10c2c7368d4e4c3
)
FetchContent_MakeAvailable(cglm)

add_library(ffi STATIC IMPORTED)
set_target_properties(ffi PROPERTIES
  IMPORTED_LOCATION ${CMAKE_STAGING_PREFIX}/lib/libffi.a
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_STAGING_PREFIX}/include)

find_package(Freetype REQUIRED)
find_package(SDL2 REQUIRED)
if(NINTENDO_SWITCH AND CMAKE_STAGING_PREFIX)
  list(PREPEND CMAKE_FIND_ROOT_PATH "${CMAKE_STAGING_PREFIX}")
endif()
find_package(SndFile REQUIRED)
if(NOT SndFile_WITH_EXTERNAL_LIBS OR NOT SndFile_WITH_OPUS)
  message(FATAL_ERROR "libsndfile must be built with Ogg Vorbis and Ogg Opus support")
endif()
find_package(ZLIB REQUIRED)

add_subdirectory(subprojects/libsys4)

if(ALICEHUB_IOS)
  enable_language(OBJC)
  add_library(xsystem4 STATIC)
  target_sources(xsystem4 PRIVATE ../ios/EnginePlatform.m)
  set_source_files_properties(../ios/EnginePlatform.m PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_include_directories(xsystem4 PRIVATE ../ios)
  target_compile_definitions(xsystem4 PRIVATE ALICEHUB_IOS SDL_MAIN_HANDLED)
elseif(NINTENDO_SWITCH)
  add_executable(xsystem4)
  target_compile_definitions(xsystem4 PRIVATE ALICEHUB_SWITCH_XSYSTEM4 SDL_MAIN_HANDLED XSYS4_DATA_DIR="romfs:")
  include(../cmake/switch/Packaging.cmake)
  set(romfs "${CMAKE_CURRENT_BINARY_DIR}/romfs")
  file(COPY fonts shaders DESTINATION "${romfs}")
  alicehub_switch_package(xsystem4 "${romfs}")
  target_sources(xsystem4 PRIVATE ../cmake/switch/hub/runtime_menu_system4.c)
else()
  add_library(xsystem4 SHARED)
endif()

target_compile_definitions(xsystem4 PRIVATE
  _DEFAULT_SOURCE
  USE_GLES
  )

target_include_directories(xsystem4 PRIVATE include)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VCS_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE)
configure_file(src/version.h.in version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_sources(xsystem4 PRIVATE
  src/audio.c
  src/audio_meta.c
  src/audio_mixer.c
  src/asset_manager.c
  src/base64.c
  src/cJSON.c
  src/draw.c
  src/effect.c
  src/ffi.c
  src/font_freetype.c
  src/font_fnl.c
  src/format.c
  src/hacks.c
  src/escalayer_reboot_cn_ui.c
  src/heap.c
  src/icon.c
  src/id_pool.c
  src/input.c
  src/json.c
  src/message_layout.c
  src/movie_plmpeg.c
  src/modifier.c
  src/msgqueue.c
  src/page.c
  src/resume.c
  src/savedata.c
  src/scene.c
  src/screenshot.c
  src/sprite.c
  src/swf.c
  src/system4.c
  src/text.c
  src/util.c
  src/video.c
  src/vm.c
  src/vm_v12.c

  src/3d/collision.c
  src/3d/debug.c
  src/3d/model.c
  src/3d/mpr.c
  src/3d/parser.c
  src/3d/particle.c
  src/3d/reign.c
  src/3d/renderer.c
  src/3d/s3de.c

  src/dungeon/dgn.c
  src/dungeon/dtx.c
  src/dungeon/dungeon.c
  src/dungeon/generator.c
  src/dungeon/map.c
  src/dungeon/mtrand43.c
  src/dungeon/polyobj.c
  src/dungeon/renderer.c
  src/dungeon/skybox.c
  src/dungeon/tes.c

  src/parts/activity.c
  src/parts/widgets.c
  src/parts/scrollbar.c
  src/parts/message_window.c
  src/parts/construction.c
  src/parts/debug.c
  src/parts/flash.c
  src/parts/flat.c
  src/parts/input.c
  src/parts/layoutbox.c
  src/parts/message.c
  src/parts/motion.c
  src/parts/parts.c
  src/parts/texture.c
  src/parts/render.c
  src/parts/text.c
  src/parts/save.c

  src/hll/ACXLoader.c
  src/hll/ADVSYS.c
  src/hll/AFAFactory.c
  src/hll/AliceLogo.c
  src/hll/AliceLogo2.c
  src/hll/AliceLogo3.c
  src/hll/AliceLogo4.c
  src/hll/AliceLogo5.c
  src/hll/AnteaterADVEngine.c
  src/hll/AnteaterADVLogList.c
  src/hll/Array.c
  src/hll/BanMisc.c
  src/hll/Bitarray.c
  src/hll/CalcTable.c
  src/hll/CGManager.c
  src/hll/CharSpriteManager.c
  src/hll/ChipmunkSpriteEngine.c
  src/hll/ChrLoader.c
  src/hll/CommonSystemData.c
  src/hll/Confirm.c
  src/hll/Confirm2.c
  src/hll/Confirm3.c
  src/hll/CrayfishLogViewer.c
  src/hll/Cursor.c
  src/hll/DALKDemo.c
  src/hll/DALKEDemo.c
  src/hll/Data.c
  src/hll/DataFile.c
  src/hll/Discord.c
  src/hll/DrawDungeon.c
  src/hll/DrawEffect.c
  src/hll/DrawGraph.c
  src/hll/DrawMovie.c
  src/hll/DrawNumeral.c
  src/hll/DrawPluginManager.c
  src/hll/DrawRain.c
  src/hll/DrawRipple.c
  src/hll/DrawSimpleText.c
  src/hll/DrawSnow.c
  src/hll/File.c
  src/hll/File2.c
  src/hll/FileOperation.c
  src/hll/FillAngle.c
  src/hll/GoatGUIEngine.c
  src/hll/Gpx2Plus.c
  src/hll/GUIEngine.c
  src/hll/HashMap.c
  src/hll/HTTPDownloader.c
  src/hll/iarray.c
  src/hll/IbisInputEngine.c
  src/hll/InputDevice.c
  src/hll/InputString.c
  src/hll/KiwiSoundEngine.c
  src/hll/KiwiSoundEngine_v12.c
  src/hll/LoadCG.c
  src/hll/MADLoader.c
  src/hll/MainEXFile.c
  src/hll/MainEXFile_v12.c
  src/hll/MainSurface.c
  src/hll/MamanyoDemo.c
  src/hll/MamanyoSDemo.c
  src/hll/MarmotModelEngine.c
  src/hll/Math.c
  src/hll/MapLoader.c
  src/hll/MenuMsg.c
  src/hll/MonsterInfo.c
  src/hll/MsgLogManager.c
  src/hll/MsgLogViewer.c
  src/hll/MsgSkip.c
  src/hll/MusicSystem.c
  src/hll/NewFont.c
  src/hll/OutputLog.c
  src/hll/P3MapSprite.c
  src/hll/P3SquareSprite.c
  src/hll/PassRegister.c
  src/hll/PastelChime2.c
  src/hll/PartsEngine.c
  src/hll/PixelRestore.c
  src/hll/PlayDemo.c
  src/hll/PlayMovie.c
  src/hll/ReignEngine.c
  src/hll/SACT2.c
  src/hll/SengokuRanceFont.c
  src/hll/SoundFilePlayer.c
  src/hll/Sound2ex.c
  src/hll/StoatSpriteEngine.c
  src/hll/StretchHelper.c
  src/hll/SystemService.c
  src/hll/Sys43VM.c
  src/hll/SystemServiceEx.c
  src/hll/TextSurfaceManager.c
  src/hll/Timer.c
  src/hll/Toushin3Loader.c
  src/hll/vmAnime.c
  src/hll/vmArray.c
  src/hll/vmCG.c
  src/hll/vmChrLoader.c
  src/hll/vmCursor.c
  src/hll/vmDalkGaiden.c
  src/hll/vmData.c
  src/hll/vmDialog.c
  src/hll/vmDrawGauge.c
  src/hll/vmDrawMsg.c
  src/hll/vmDrawNumber.c
  src/hll/vmFile.c
  src/hll/vmGraph.c
  src/hll/vmGraphQuake.c
  src/hll/vmKey.c
  src/hll/vmMapLoader.c
  src/hll/vmMsgLog.c
  src/hll/vmMusic.c
  src/hll/vmSound.c
  src/hll/vmSprite.c
  src/hll/vmString.c
  src/hll/vmSurface.c
  src/hll/vmSystem.c
  src/hll/vmTimer.c
  src/hll/ValueEncryption.c
  src/hll/VSFile.c
  )

target_link_libraries(xsystem4 PRIVATE
  m ZLIB::ZLIB SDL2::SDL2 Freetype::Freetype ffi cglm SndFile::sndfile sys4)
if(ALICEHUB_IOS)
  target_link_libraries(xsystem4 PRIVATE "-framework OpenGLES")
  install(TARGETS sys4 cglm)
  install(FILES include/xsystem4_main.h include/modifier_api.h DESTINATION include)
elseif(NINTENDO_SWITCH)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SWITCH_GLES REQUIRED IMPORTED_TARGET glesv2)
  target_link_libraries(xsystem4 PRIVATE PkgConfig::SWITCH_GLES EGL glapi drm_nouveau stdc++)
else()
  target_link_libraries(xsystem4 PRIVATE GLESv3)
endif()

target_compile_options(xsystem4 PRIVATE -Wno-unused-parameter)

install(TARGETS xsystem4)
