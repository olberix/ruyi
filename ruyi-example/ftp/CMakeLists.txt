project(ruyi-ftp-example C)

add_executable(ftp_client ftp_client.c)
target_link_libraries(ftp_client PRIVATE ruyi_static)

add_executable(ftp_server ftp_server.c)
target_link_libraries(ftp_server PRIVATE ruyi_static)
