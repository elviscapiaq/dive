# transfer_infra/CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(transfer_infra)

# Define source files for the client library
set(TRANSFER_CLIENT_SRCS
    socket_connection.cc
    tcp_client.cc
)

# Define header files for the client library (optional, but good practice for IDEs)
set(TRANSFER_CLIENT_HDRS
    log.h
    message_handler.h
    messages.h
    serializable.h
    socket_connection.h
    tcp_client.h
)

# Create the client static library
add_library(transfer_client ${TRANSFER_CLIENT_SRCS} ${TRANSFER_CLIENT_HDRS})

# Set include directories for the client library
# This makes the headers available when other targets link against transfer_client
target_include_directories(transfer_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Define source files for the server library
set(TRANSFER_SERVER_SRCS
    socket_connection.cc
    tcp_server.cc
)

# Define header files for the server library (optional)
set(TRANSFER_SERVER_HDRS
    log.h
    message_handler.h
    messages.h
    serializable.h
    socket_connection.h
    tcp_server.h
)

# Create the server static library
add_library(transfer_server ${TRANSFER_SERVER_SRCS} ${TRANSFER_SERVER_HDRS})

# Set include directories for the server library
target_include_directories(transfer_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# (Optional) If your connection.h and socket_connection.h contain
# common implementations used by both client and server, you might
# consider creating a separate common library or ensuring these are
# part of both library compilations, as done above.