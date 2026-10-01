# Windows used to rely on FMOD for OGG; the audio.mixer driver decodes with libvorbis everywhere
add_subdirectory(tree)
target_output_to_toolchain(ogg)
