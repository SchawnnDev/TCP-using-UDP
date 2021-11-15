cmake_minimum_required(VERSION 3.10.2)
project(ProjetAlgoReseaux C)

set(CMAKE_C_STANDARD 99)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Application "source"
file(GLOB SOURCE_SRC src/source/*.c headers/global/*.h headers/source/*.h)
add_executable(Source ${SOURCE_SRC})
target_link_libraries(Source PRIVATE Threads::Threads)

# Application "destination"
file(GLOB DESTINATION_SRC src/destination/*.c headers/global/*.h headers/destination/*.h)
add_executable(Destination ${DESTINATION_SRC})