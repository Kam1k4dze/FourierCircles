cmake_minimum_required(VERSION 3.25)
project(EmbedTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(embed_binary embed_binary.cpp)
add_executable(embed_text embed_text.cpp)

