file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(server main.cpp ${SOURCES})

target_include_directories(server PUBLIC include/)

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

target_include_directories( server PRIVATE external/ )
target_link_libraries(server PRIVATE 
  rukh::rukh
  spdlog::spdlog
  nlohmann_json::nlohmann_json
)
