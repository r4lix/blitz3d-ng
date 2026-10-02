if(TARGET SDL2)
  bb_start_module(runtime.sdl)
  set(DEPENDS_ON bb.runtime bb.input bb.pixmap bb.graphics.sdl bb.blitz3d.gl bb.timer bb.audio bb.system)
  set(SOURCES runtime.sdl.cpp runtime.sdl.h overlay.cpp overlay.h
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_impl_opengl3.cpp)
  # Dear ImGui (and its GL loader) must not share a translation unit with GLEW
  set_source_files_properties(imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_impl_opengl3.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  if(BB_NX)
    set_source_files_properties(imgui/imgui_impl_opengl3.cpp PROPERTIES COMPILE_DEFINITIONS IMGUI_IMPL_OPENGL_ES3)
  endif()

  set(LIBS SDL2)
  if(BB_MOBILE)
    set(LIBS ${LIBS} SDL2main)
  endif()

  if(BB_LINUX)
    set(LIBS ${LIBS} dl)
  elseif(BB_MACOS)
    set(SYSTEM_LIBS "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation" "-framework CoreVideo" "-framework CoreAudio" "-framework ForceFeedback" "-framework AudioToolBox" "-framework Metal" "-framework GameController" "-framework CoreHaptics" "-framework Carbon" iconv)
  elseif(BB_NX)
    # SDL2 (portlib) needs the mesa EGL stack and libnx
    set(SYSTEM_LIBS GLESv2 EGL glapi drm_nouveau nx)
  elseif(BB_WINDOWS)
    set(LIBS ${LIBS} dxguid)
  elseif(BB_IOS)
    set(SYSTEM_LIBS "-framework Foundation" "-framework UIKit" "-framework CoreFoundation" "-framework CoreVideo" "-framework CoreAudio" "-framework AVFAudio" "-framework AudioToolBox" "-framework Metal" "-framework GameController" "-framework CoreHaptics" "-framework CoreBluetooth" "-framework CoreMotion" "-framework CoreGraphics" "-framework QuartzCore" iconv)
  elseif(BB_ANDROID)
    set(SYSTEM_LIBS GLESv1_CM GLESv2 OpenSLES android dl)
    set(EXTRA_FILES SDL.jar Runtime.jar)
  endif()
  bb_end_module()

  if(BB_ANDROID)
    add_jar(RTjar android/Game.java
      INCLUDE_JARS ${ANDROID_JAR}
                   ${TOOLCHAIN_PATH}/lib/SDL.jar
      OUTPUT_NAME Runtime
      OUTPUT_DIR ${TOOLCHAIN_PATH}/lib
    )

    file(INSTALL android/manifest.template.xml DESTINATION ${TOOLCHAIN_PATH})
  endif()
endif()
