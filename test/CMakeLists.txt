cmake_minimum_required(VERSION 3.10)

project(esp32-windsensor-tests)

add_library(testingMemoryLib TestingMemory.c)
add_library(errorMessagesLib ../main/ErrorMessages.c)
add_library(messageFormatterLib ../main/MessageFormatter.c)
target_link_libraries(messageFormatterLib errorMessagesLib testingMemoryLib)
add_library(messagesLib ../main/Messages.c)

add_executable(messageFormatterTest MessageFormatterTest.c)
target_link_libraries(messageFormatterTest errorMessagesLib messagesLib messageFormatterLib)

add_executable(errorMessagesTest ErrorMessagesTest.c)
target_link_libraries(errorMessagesTest errorMessagesLib)

add_executable(messagesTest MessagesTest.c)
target_link_libraries(messagesTest messagesLib)