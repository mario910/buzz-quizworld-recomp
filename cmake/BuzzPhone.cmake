# Embedded HTTP controller: source-pinned, no downloads or runtime interpreter.
if(NOT TARGET buzz_phone)
    add_library(buzz_phone STATIC
        ${CMAKE_CURRENT_LIST_DIR}/../port/phone_server.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../port/phone_sessions.cpp)
    target_compile_features(buzz_phone PUBLIC cxx_std_17)
    target_include_directories(buzz_phone PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../port)
    if(WIN32)
        target_link_libraries(buzz_phone PUBLIC ws2_32 iphlpapi bcrypt)
        target_compile_definitions(buzz_phone PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(buzz_phone PUBLIC Threads::Threads)
    endif()
endif()
