AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++17 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_insteon.la
mod_insteon_la_SOURCES = InsteonPacket.h PendingQueues.cpp Factory.cpp GD.h InsteonMessage.h InsteonPeer.cpp InsteonPacket.cpp PacketQueue.cpp QueueManager.h InsteonMessages.cpp InsteonMessage.cpp Insteon.cpp GD.cpp InsteonMessages.h InsteonPeer.h Factory.h Insteon.h PhysicalInterfaces/IInsteonInterface.h PhysicalInterfaces/IInsteonInterface.cpp PhysicalInterfaces/Insteon_Hub_X10.h PhysicalInterfaces/Insteon_Hub_X10.cpp InsteonCentral.h InsteonCentral.cpp PacketQueue.h PendingQueues.h PacketManager.h PacketManager.cpp QueueManager.cpp Interfaces.cpp Interfaces.h
mod_insteon_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_insteon.la
