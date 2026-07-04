file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(server main.cpp ${SOURCES})

target_include_directories(server PUBLIC include/)

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)


target_compile_definitions(server PRIVATE
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=0,SPDLOG_ACTIVE_LEVEL=2>
)
target_compile_options(server PRIVATE
  $<IF:$<CONFIG:Release>,-O3,-O0 -g>
)


target_include_directories( server PRIVATE external/ )
target_link_libraries(server PRIVATE 
  rukh::rukh
  spdlog::spdlog
  nlohmann_json::nlohmann_json
)
