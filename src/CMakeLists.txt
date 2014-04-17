set(CMAKE_C_FLAGS "-Wall -g")

file( GLOB SRCS *.c *.h)
add_subdirectory(Socket_Comms)
add_subdirectory(ConfigUtils)

include_directories(Socket_Comms ConfigUtils)

link_directories(${TMR_BINARY_DIR}/ConfigUtils ${TMR_BINARY_DIR}/Socket_Comms)
#link_directories(ConfigUtils Socket_Comms)
add_executable(TMReader ../src/TMReader.c)

target_link_libraries(TMReader mercuryapi ltkc ltkctm CONFIGUTILS SOCKETS)