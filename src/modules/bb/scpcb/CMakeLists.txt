bb_start_module(scpcb)
set(DEPENDS_ON bb.blitz bb.audio bb.bank)
set(SOURCES scpcb.cpp scpcb.h ue.cpp)
bb_end_module()

# JSON documents of UE Reborn (header only)
target_include_directories(bb.scpcb PRIVATE ${PROJECT_SOURCE_DIR}/deps/json-hpp/tree/single_include/nlohmann)
