# Software mixer: decodes with the stream readers in bb.audio and plays through SDL's
# audio device (or a dummy clock / WAV dump for headless testing).
if(TARGET SDL2 AND TARGET ogg AND TARGET vorbis)
  bb_start_module(audio.mixer)
  set(DEPENDS_ON bb.audio)
  set(SOURCES audio.mixer.cpp audio.mixer.h)
  set(LIBS SDL2)
  bb_end_module()
endif()
