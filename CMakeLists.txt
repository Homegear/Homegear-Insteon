cmake_minimum_required(VERSION 3.9)
project(homegear_insteon)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/PhysicalInterfaces/IInsteonInterface.cpp
        src/PhysicalInterfaces/IInsteonInterface.h
        src/PhysicalInterfaces/Insteon_Hub_X10.cpp
        src/PhysicalInterfaces/Insteon_Hub_X10.h
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Insteon.cpp
        src/Insteon.h
        src/InsteonCentral.cpp
        src/InsteonCentral.h
        src/InsteonMessage.cpp
        src/InsteonMessage.h
        src/InsteonMessages.cpp
        src/InsteonMessages.h
        src/InsteonPacket.cpp
        src/InsteonPacket.h
        src/InsteonPeer.cpp
        src/InsteonPeer.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/PacketManager.cpp
        src/PacketManager.h
        src/PacketQueue.cpp
        src/PacketQueue.h
        src/PendingQueues.cpp
        src/PendingQueues.h
        src/QueueManager.cpp
        src/QueueManager.h)

add_custom_target(homegear COMMAND ../../makeAll.sh SOURCES ${SOURCE_FILES})

add_library(homegear_insteon ${SOURCE_FILES})