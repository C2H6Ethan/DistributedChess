cmake_minimum_required(VERSION 3.30)
project(ChessInCpp)

set(CMAKE_CXX_STANDARD 26)

include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.5
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 1. Core library containing ALL domain logic and the validator
add_library(ChessCore
        Board.cpp
        Board.h
        BitboardUtils.h
        Move.h
        Validator.cpp
        Validator.h
        Search.cpp
        Search.h
)

# 2. Production REST microservice binary (port 8081)
add_executable(chess_engine main.cpp)
target_link_libraries(chess_engine PRIVATE ChessCore httplib::httplib nlohmann_json::nlohmann_json)
