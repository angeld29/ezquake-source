# This CMake config was made for the Windows build using msvc only. 
# Nothing will work otherwise
# written by a newbie in CMake :) BorisU

# tested on VS 2017, might work on earlier versions
# start VS command prompt
# use "cmake ."  command in the "build" directory
# solution and projects files will be generated
# you can compile it from VS IDE or
# use "cmake --build . --config Release"

# is it right?
cmake_minimum_required (VERSION 3.1)

message(WARNING "This CMake config was made for the Windows build using msvc only.")

set (PROJECT ezQuake.262)

project (${PROJECT} C) 

set ( APP_NAME ${PROJECT})

#set( CMAKE_BUILD_TYPE RELEASE )

set ( SRC_DIR ${PROJECT_SOURCE_DIR}/.. )

set ( HEADERS
	${SRC_DIR}/adivtab.h
	${SRC_DIR}/anorm_dots.h
	${SRC_DIR}/anorms.h
	${SRC_DIR}/block8.h
	${SRC_DIR}/block16.h
	${SRC_DIR}/bspfile.h
	${SRC_DIR}/cdaudio.h
	${SRC_DIR}/cl_slist.h
	${SRC_DIR}/cl_view.h
	${SRC_DIR}/client.h
	${SRC_DIR}/cmd.h
	${SRC_DIR}/cmodel.h
	${SRC_DIR}/common.h
	${SRC_DIR}/common_draw.h
	${SRC_DIR}/config_manager.h
	${SRC_DIR}/console.h
	${SRC_DIR}/crc.h
	${SRC_DIR}/Ctrl.h
	${SRC_DIR}/Ctrl_EditBox.h
	${SRC_DIR}/Ctrl_PageViewer.h
	${SRC_DIR}/Ctrl_Tab.h
	${SRC_DIR}/cvar.h
	${SRC_DIR}/cvar_groups.h
	${SRC_DIR}/demo_controls.h
	${SRC_DIR}/document_rendering.h
	${SRC_DIR}/draw.h
	${SRC_DIR}/embed_tcl.h
	${SRC_DIR}/EX_browser.h
	${SRC_DIR}/EX_FileList.h
	${SRC_DIR}/EX_qtvlist.h
	${SRC_DIR}/ez_button.h
	${SRC_DIR}/ez_controls.h
	${SRC_DIR}/ez_label.h
	${SRC_DIR}/ez_listview.h
	${SRC_DIR}/ez_listviewitem.h
	${SRC_DIR}/ez_scrollbar.h
	${SRC_DIR}/ez_scrollpane.h
	${SRC_DIR}/ez_slider.h
	${SRC_DIR}/ez_window.h
	${SRC_DIR}/fchecks.h
	${SRC_DIR}/fmod.h
	${SRC_DIR}/fs.h
	${SRC_DIR}/g_public.h
	${SRC_DIR}/gl_bloom.h
	${SRC_DIR}/gl_framebuffer.h
	${SRC_DIR}/gl_local.h
	${SRC_DIR}/gl_md3.h
	${SRC_DIR}/gl_model.h
	${SRC_DIR}/gl_texture.h
	${SRC_DIR}/gl_warp_sin.h
	${SRC_DIR}/hash.h
	${SRC_DIR}/help.h
	${SRC_DIR}/hud.h
	${SRC_DIR}/hud_common.h
	${SRC_DIR}/hud_editor.h
	${SRC_DIR}/ignore.h
	${SRC_DIR}/image.h
	${SRC_DIR}/in_osx.h
	${SRC_DIR}/input.h
	${SRC_DIR}/irc.h
	${SRC_DIR}/irc_filter.h
	${SRC_DIR}/keys.h
	${SRC_DIR}/localtime.h
	${SRC_DIR}/logging.h
	${SRC_DIR}/mathlib.h
	${SRC_DIR}/menu.h
	${SRC_DIR}/menu_demo.h
	${SRC_DIR}/menu_ingame.h
	${SRC_DIR}/menu_mp3player.h
	${SRC_DIR}/menu_multiplayer.h
	${SRC_DIR}/menu_options.h
	${SRC_DIR}/menu_proxy.h
	${SRC_DIR}/modelgen.h
	${SRC_DIR}/modules.h
	${SRC_DIR}/movie.h
	${SRC_DIR}/movie_avi.h
	${SRC_DIR}/mp3_player.h
	${SRC_DIR}/mvd_utils.h
	${SRC_DIR}/mvd_utils_common.h
	${SRC_DIR}/net.h
	${SRC_DIR}/parser.h
	${SRC_DIR}/pmove.h
	${SRC_DIR}/pr_comp.h
	${SRC_DIR}/pr2.h
	${SRC_DIR}/pr2_vm.h
	${SRC_DIR}/progdefs.h
	${SRC_DIR}/progs.h
	${SRC_DIR}/protocol.h
	${SRC_DIR}/q_shared.h
	${SRC_DIR}/qsound.h
	${SRC_DIR}/qtv.h
	${SRC_DIR}/quakedef.h
	${SRC_DIR}/qwsvdef.h
	${SRC_DIR}/r_local.h
	${SRC_DIR}/r_shared.h
	${SRC_DIR}/render.h
	${SRC_DIR}/rulesets.h
	${SRC_DIR}/sbar.h
	${SRC_DIR}/screen.h
	${SRC_DIR}/server.h
	${SRC_DIR}/settings.h
	${SRC_DIR}/settings_page.h
	${SRC_DIR}/sha1.h
	${SRC_DIR}/spritegn.h
	${SRC_DIR}/stats_grid.h
	${SRC_DIR}/sv_log.h
	${SRC_DIR}/sv_mod_frags.h
	${SRC_DIR}/sv_world.h
	${SRC_DIR}/sys.h
	${SRC_DIR}/teamplay.h
	${SRC_DIR}/textencoding.h
	${SRC_DIR}/tp_msgs.h
	${SRC_DIR}/tp_triggers.h
	${SRC_DIR}/tr_types.h
	${SRC_DIR}/utils.h
	${SRC_DIR}/version.h
	${SRC_DIR}/vfs.h
	${SRC_DIR}/vfs_tar.h
	${SRC_DIR}/vid.h
	${SRC_DIR}/vx_camera.h
	${SRC_DIR}/vx_stuff.h
	${SRC_DIR}/vx_tracker.h
	${SRC_DIR}/vx_vertexlights.h
	${SRC_DIR}/wad.h
	${SRC_DIR}/winamp.h
	${SRC_DIR}/xsd.h
	${SRC_DIR}/xsd_command.h
	${SRC_DIR}/xsd_document.h
	${SRC_DIR}/xsd_variable.h
	${SRC_DIR}/zone.h
)

set ( SOURCES 
	${SRC_DIR}/cl_cam.c
	${SRC_DIR}/cl_cmd.c
	${SRC_DIR}/cl_demo.c
	${SRC_DIR}/cl_ents.c
	${SRC_DIR}/cl_input.c
	${SRC_DIR}/cl_main.c
	${SRC_DIR}/cl_multiview.c
	${SRC_DIR}/cl_nqdemo.c
	${SRC_DIR}/cl_parse.c
	${SRC_DIR}/cl_pred.c
	${SRC_DIR}/cl_screen.c
	${SRC_DIR}/cl_slist.c
	${SRC_DIR}/cl_tcl.c
	${SRC_DIR}/cl_tent.c
	${SRC_DIR}/cl_view.c
	${SRC_DIR}/cmd.c
	${SRC_DIR}/cmodel.c
	${SRC_DIR}/collision.c
	${SRC_DIR}/common.c
	${SRC_DIR}/common_draw.c
	${SRC_DIR}/com_msg.c
	${SRC_DIR}/config_manager.c
	${SRC_DIR}/console.c
	${SRC_DIR}/crc.c
	${SRC_DIR}/Ctrl.c
	${SRC_DIR}/Ctrl_EditBox.c
	${SRC_DIR}/Ctrl_PageViewer.c
	${SRC_DIR}/Ctrl_ScrollBar.c
	${SRC_DIR}/Ctrl_Tab.c
	${SRC_DIR}/cvar.c
	${SRC_DIR}/demo_controls.c
	${SRC_DIR}/document_rendering.c
	${SRC_DIR}/EX_browser.c
	${SRC_DIR}/EX_browser_net.c
	${SRC_DIR}/EX_browser_pathfind.c
	${SRC_DIR}/EX_browser_ping.c
	${SRC_DIR}/EX_browser_qtvlist.c
	${SRC_DIR}/EX_browser_sources.c
	${SRC_DIR}/EX_FileList.c
	${SRC_DIR}/EX_qtvlist.c
	${SRC_DIR}/ez_button.c
	${SRC_DIR}/ez_controls.c
	${SRC_DIR}/ez_label.c
	${SRC_DIR}/ez_listview.c
	${SRC_DIR}/ez_listviewitem.c
	${SRC_DIR}/ez_scrollbar.c
	${SRC_DIR}/ez_scrollpane.c
	${SRC_DIR}/ez_slider.c
	${SRC_DIR}/ez_window.c
	${SRC_DIR}/fchecks.c
	${SRC_DIR}/fmod.c
	${SRC_DIR}/fragstats.c
	${SRC_DIR}/fs.c
	${SRC_DIR}/gl_bloom.c
	${SRC_DIR}/gl_draw.c
	${SRC_DIR}/gl_framebuffer.c
	${SRC_DIR}/gl_md3.c
	${SRC_DIR}/gl_mesh.c
	${SRC_DIR}/gl_model.c
	${SRC_DIR}/gl_ngraph.c
	${SRC_DIR}/gl_refrag.c
	${SRC_DIR}/gl_rlight.c
	${SRC_DIR}/gl_rmain.c
	${SRC_DIR}/gl_rmisc.c
	${SRC_DIR}/gl_rpart.c
	${SRC_DIR}/gl_rsurf.c
	${SRC_DIR}/gl_texture.c
	${SRC_DIR}/gl_warp.c
	${SRC_DIR}/hash.c
	${SRC_DIR}/help.c
	${SRC_DIR}/help_files.c
	${SRC_DIR}/host.c
	${SRC_DIR}/hud.c
	${SRC_DIR}/hud_common.c
	${SRC_DIR}/hud_editor.c
	${SRC_DIR}/hud_radar.c
	${SRC_DIR}/hud_weapon_stats.c
	${SRC_DIR}/ignore.c
	${SRC_DIR}/image.c
	${SRC_DIR}/in_sdl2.c
	${SRC_DIR}/irc.c
	${SRC_DIR}/irc_filter.c
	${SRC_DIR}/keys.c
	${SRC_DIR}/logging.c
	${SRC_DIR}/match_tools.c
	${SRC_DIR}/mathlib.c
	${SRC_DIR}/md4.c
	${SRC_DIR}/menu.c
	${SRC_DIR}/menu_demo.c
	${SRC_DIR}/menu_ingame.c
	${SRC_DIR}/menu_mp3player.c
	${SRC_DIR}/menu_multiplayer.c
	${SRC_DIR}/menu_options.c
	${SRC_DIR}/menu_proxy.c
	${SRC_DIR}/modules.c
	${SRC_DIR}/movie.c
	${SRC_DIR}/mp3_audacious.c
	${SRC_DIR}/mp3_mpd.c
	${SRC_DIR}/mp3_player.c
	${SRC_DIR}/mp3_winamp.c
	${SRC_DIR}/mp3_xmms2.c
	${SRC_DIR}/mp3_xmms.c
	${SRC_DIR}/mvd_autotrack.c
	${SRC_DIR}/mvd_utils.c
	${SRC_DIR}/mvd_xmlstats.c
	${SRC_DIR}/net.c
	${SRC_DIR}/net_chan.c
	${SRC_DIR}/parser.c
	${SRC_DIR}/pmove.c
	${SRC_DIR}/pmovetst.c
	${SRC_DIR}/pr2_cmds.c
	${SRC_DIR}/pr2_edict.c
	${SRC_DIR}/pr2_exec.c
	${SRC_DIR}/pr2_vm.c
	${SRC_DIR}/pr_cmds.c
	${SRC_DIR}/pr_edict.c
	${SRC_DIR}/pr_exec.c
	${SRC_DIR}/q_shared.c
	${SRC_DIR}/qtv.c
	${SRC_DIR}/r_part.c
	${SRC_DIR}/rulesets.c
	${SRC_DIR}/sbar.c
	${SRC_DIR}/settings_page.c
	${SRC_DIR}/sha1.c
	${SRC_DIR}/skin.c
	${SRC_DIR}/snd_main.c
	${SRC_DIR}/snd_mem.c
	${SRC_DIR}/snd_mix.c
	${SRC_DIR}/snd_ov.c
	${SRC_DIR}/snd_voip.c
	${SRC_DIR}/stats_grid.c
	${SRC_DIR}/sv_ccmds.c
	${SRC_DIR}/sv_demo.c
	${SRC_DIR}/sv_demo_misc.c
	${SRC_DIR}/sv_demo_qtv.c
	${SRC_DIR}/sv_ents.c
	${SRC_DIR}/sv_init.c
	${SRC_DIR}/sv_login.c
	${SRC_DIR}/sv_main.c
	${SRC_DIR}/sv_master.c
	${SRC_DIR}/sv_mod_frags.c
	${SRC_DIR}/sv_move.c
	${SRC_DIR}/sv_nchan.c
	${SRC_DIR}/sv_phys.c
	${SRC_DIR}/sv_save.c
	${SRC_DIR}/sv_send.c
	${SRC_DIR}/sv_user.c
	${SRC_DIR}/sv_world.c
	${SRC_DIR}/sys_sdl2.c
	${SRC_DIR}/teamplay.c
	${SRC_DIR}/textencoding.c
	${SRC_DIR}/tp_msgs.c
	${SRC_DIR}/tp_triggers.c
	${SRC_DIR}/utils.c
	${SRC_DIR}/version.c
	${SRC_DIR}/vfs_doomwad.c
	${SRC_DIR}/vfs_gzip.c
	${SRC_DIR}/vfs_mmap.c
	${SRC_DIR}/vfs_os.c
	${SRC_DIR}/vfs_pak.c
	${SRC_DIR}/vfs_tar.c
	${SRC_DIR}/vfs_tcp.c
	${SRC_DIR}/vfs_zip.c
	${SRC_DIR}/vid_common_gl.c
	${SRC_DIR}/vid_sdl2.c
	${SRC_DIR}/vx_camera.c
	${SRC_DIR}/vx_coronas.c
	${SRC_DIR}/vx_motiontrail.c
	${SRC_DIR}/vx_stuff.c
	${SRC_DIR}/vx_tracker.c
	${SRC_DIR}/vx_vertexlights.c
	${SRC_DIR}/wad.c                                    	
	${SRC_DIR}/xsd.c
	${SRC_DIR}/xsd_command.c
	${SRC_DIR}/xsd_document.c
	${SRC_DIR}/xsd_variable.c
	${SRC_DIR}/zone.c
)

include_directories ( 
	${SRC_DIR}/msvc32-libs/include
	${SRC_DIR}/msvc32-libs/include/expat
	${SRC_DIR}/msvc32-libs/include/jansson
	${SRC_DIR}/msvc32-libs/include/PCRE
	${SRC_DIR}/msvc32-libs/include/SDL2
)

source_group ("Header Files" FILES ${HEADERS})
source_group ("Source Files" FILES ${SOURCES})

find_package(Git)
if(GIT_FOUND)
  message("git found: ${GIT_EXECUTABLE}")
endif ()

#how to find it properly on windows?  replace git with bash?
set ( BASH_EXECUTABLE "C:/Program Files/Git/bin/bash.exe" )

#TODO make them custom targets

execute_process ( COMMAND ${BASH_EXECUTABLE} version.sh -r
	WORKING_DIRECTORY ${SRC_DIR} 
	OUTPUT_VARIABLE rev 
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process ( COMMAND ${BASH_EXECUTABLE} version.sh -v
	WORKING_DIRECTORY ${SRC_DIR} 
	OUTPUT_VARIABLE ver 
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_definitions ( -DVERSION="${ver}" -DREVISION="${rev}" )

execute_process ( COMMAND ${BASH_EXECUTABLE} json2c.sh help_commands.json help_commands.c
	WORKING_DIRECTORY ${SRC_DIR} 
)

execute_process ( COMMAND ${BASH_EXECUTABLE} json2c.sh help_variables.json help_variables.c
	WORKING_DIRECTORY ${SRC_DIR} 
)

list (APPEND SOURCES
	${SRC_DIR}/help_commands.c
	${SRC_DIR}/help_variables.c
)

find_package(OpenGL REQUIRED)
if ( NOT ${OPENGL_FOUND} )
    message ( FATAL_ERROR "OPENGL not found" )
endif ()

add_definitions ( -DUSE_PR2 -DJSS_CAM )

if  ( WIN32 )
	set ( WIN_SOURCES
		${SRC_DIR}/cd_null.c
		${SRC_DIR}/localtime_win.c
		${SRC_DIR}/movie_avi.c
		${SRC_DIR}/sys_win.c
	)
	source_group ( "Windows specific Source Files" FILES ${WIN_SOURCES} )
	list ( APPEND SOURCES
		${WIN_SOURCES}
	)

	set ( LIBRARIES 
		comctl32
		wldap32
		winmm
		ws2_32
		jansson
		libcurl
		expat
		pcre
		SDL2
		imm32	# SDL2 needs it
		version	# SDL2 needs it
)

	add_definitions ( -DPCRE_STATIC -DCURL_STATICLIB -DXML_STATIC )
	link_directories(${SRC_DIR}/msvc32-libs/lib)
endif ()

option ( WITH_NQPROGS " Adds Net Quake progs support" ON  )
if ( WITH_NQPROGS)
	add_definitions ( -DNQPROGS )
endif ()

option ( WITH_WINAMP " Adds MP3 player interface" ON  )
if ( WITH_WINAMP)
	add_definitions ( -DWITH_WINAMP )
endif ()

option ( WITH_ZIP "Adds pk3 packs and compressed demo files support" ON  )
if ( WITH_ZIP )
	set ( ZIP_SOURCES
		${SRC_DIR}/minizip/ioapi.c
		${SRC_DIR}/minizip/unzip.c
	)
	set ( ZIP_HEADERS
		${SRC_DIR}/minizip/ioapi.h
		${SRC_DIR}/minizip/unzip.h
	)
	source_group ( "Minizip Source Files" FILES ${ZIP_SOURCES} )
	source_group ( "Minizip Header Files" FILES ${ZIP_HEADERS} )
	list ( APPEND SOURCES ${ZIP_SOURCES} )
	set ( ZIP_INCLUDES ${SRC_DIR}/minizip )
	add_definitions ( -DWITH_ZIP )	
endif ()

option ( WITH_ZLIB "Adds .gz and .zip support" ON  )
if ( WITH_ZLIB )
	set ( ZLIB_INCLUDES 	${SRC_DIR}/msvc32-libs/include/zlib )
	set ( ZLIB_LIBRARIES zlib )
	add_definitions ( -DWITH_ZLIB )	
endif ()

option ( WITH_PNG "Adds PNG support" ON  ) # requires ZLIB
if ( WITH_PNG )
	set ( PNG_INCLUDES 	${SRC_DIR}/msvc32-libs/include/libpng )
	set ( PNG_LIBRARIES libpng16 )
	add_definitions ( -DWITH_PNG_STATIC -DWITH_PNG -D__Q_PNG14__ )	
endif ()

option ( WITH_JPEG "Adds JPEG support" ON  )
if ( WITH_ZIP )
	set ( JPEG_INCLUDES ${SRC_DIR}/msvc32-libs/include/jpeg )
	set ( JPEG_LIBRARIES libjpeg )
	add_definitions ( -DWITH_JPEG_STATIC -DWITH_JPEG )	
endif ()

option( WITH_SPEEX "Adds in-game voice support" ON  )
if ( WITH_SPEEX )
	set ( SPEEX_INCLUDES ${SRC_DIR}/msvc32-libs/include/speex )
	set ( SPEEX_LIBRARIES libspeex libspeexdsp )
	add_definitions ( -DWITH_SPEEX )	
endif ()

#if (MSVC)
#	add_definitions (/W4)
#elseif (CMAKE_COMPILER_IS_GNUCXX)
#	add_definitions (-Wall -pedantic)
#else ()
#	message ("Unknown compiler")
#endif ()

# better way to do it?
set ( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MP /MT /arch:SSE2" )

#adding headers as dependencies to show them properly in the project file
add_executable ( ${PROJECT} WIN32 ${SOURCES} ${HEADERS} ${ZIP_HEADERS} )
target_link_libraries ( ${PROJECT} 
	${LIBRARIES}
	${OPENGL_LIBRARIES} 
	${JPEG_LIBRARIES} 
	${ZLIB_LIBRARIES}
	${PNG_LIBRARIES}
	${SPEEX_LIBRARIES}
	)

target_include_directories ( ${PROJECT} PRIVATE ${ZIP_INCLUDES} )
target_include_directories ( ${PROJECT} PRIVATE ${ZLIB_INCLUDES} )
target_include_directories ( ${PROJECT} PRIVATE ${PNG_INCLUDES} )
target_include_directories ( ${PROJECT} PRIVATE ${JPEG_INCLUDES} )
target_include_directories ( ${PROJECT} PRIVATE ${SPEEX_INCLUDES} )
