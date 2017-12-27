/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#ifndef INSTEONCENTRAL_H_
#define INSTEONCENTRAL_H_

#include <homegear-base/BaseLib.h>
#include "InsteonPeer.h"
#include "InsteonPacket.h"
#include "QueueManager.h"
#include "PacketManager.h"
#include "InsteonMessages.h"

#include <memory>
#include <mutex>
#include <string>

using namespace BaseLib;

namespace Insteon
{
class InsteonMessages;

class InsteonCentral : public BaseLib::Systems::ICentral
{
public:
	//In table variables
	int32_t getFirmwareVersion() { return _firmwareVersion; }
	void setFirmwareVersion(int32_t value) { _firmwareVersion = value; saveVariable(0, value); }
	int32_t getCentralAddress() { return _centralAddress; }
	void setCentralAddress(int32_t value) { _centralAddress = value; saveVariable(1, value); }
	//End

	InsteonCentral(ICentralEventSink* eventHandler);
	InsteonCentral(uint32_t deviceType, std::string serialNumber, int32_t address, ICentralEventSink* eventHandler);
	virtual ~InsteonCentral();

	virtual void stopThreads();
	virtual void dispose(bool wait = true);

	virtual bool onPacketReceived(std::string& senderID, std::shared_ptr<BaseLib::Systems::Packet> packet);
	virtual std::string handleCliCommand(std::string command);
	virtual uint64_t getPeerIdFromSerial(std::string& serialNumber) { std::shared_ptr<InsteonPeer> peer = getPeer(serialNumber); if(peer) return peer->getID(); else return 0; }
	virtual bool enqueuePendingQueues(int32_t deviceAddress, bool wait = false);
	void unpair(uint64_t id);

	std::shared_ptr<InsteonPeer> getPeer(int32_t address);
	std::shared_ptr<InsteonPeer> getPeer(uint64_t id);
	std::shared_ptr<InsteonPeer> getPeer(std::string serialNumber);
	virtual bool isInPairingMode() { return _pairing; }
	virtual std::shared_ptr<InsteonMessages> getMessages() { return _messages; }
	std::shared_ptr<InsteonPacket> getSentPacket(int32_t address) { return _sentPackets.get(address); }

	virtual void loadVariables();
	virtual void saveVariables();
	virtual void loadPeers();
	virtual void savePeers(bool full);

	virtual void sendPacket(std::shared_ptr<BaseLib::Systems::IPhysicalInterface> physicalInterface, std::shared_ptr<InsteonPacket> packet, bool stealthy = false);

	virtual void handleNak(std::shared_ptr<InsteonPacket> packet);
	virtual void handleAck(std::shared_ptr<InsteonPacket> packet);
	virtual void handleDatabaseOpResponse(std::shared_ptr<InsteonPacket> packet);
	virtual void handleLinkingModeResponse(std::shared_ptr<InsteonPacket> packet);
	virtual void handlePairingRequest(std::shared_ptr<InsteonPacket> packet);

	virtual PVariable addDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t flags);
	virtual PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, uint64_t id, std::map<std::string, bool> fields);
	virtual PVariable getInstallMode(BaseLib::PRpcClientInfo clientInfo);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t channel, ParameterGroup::Type::Enum type, std::string remoteSerialNumber, int32_t remoteChannel, PVariable paramset);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable paramset);
	virtual PVariable setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput = true);
protected:
	//In table variables
	int32_t _firmwareVersion = 0;
	int32_t _centralAddress = 0;
	//End

	std::atomic_bool _stopWorkerThread;
	std::thread _workerThread;

	std::atomic_bool _pairing;
	QueueManager _queueManager;
	PacketManager _receivedPackets;
	PacketManager _sentPackets;
	std::shared_ptr<InsteonMessages> _messages;

	std::atomic<uint32_t> _timeLeftInPairingMode;
	void pairingModeTimer(int32_t duration, bool debugOutput = true);
	std::atomic_bool _stopPairingModeThread;
	std::atomic_bool _abortPairingModeThread;
	std::mutex _pairingModeThreadMutex;
	std::thread _pairingModeThread;
	int64_t _manualPairingModeStarted = -1;
	std::mutex _unpairThreadMutex;
	std::thread _unpairThread;

	std::mutex _peerInitMutex;
	std::mutex _pairingMutex;
	std::mutex _enqueuePendingQueuesMutex;

	void addPeer(std::shared_ptr<InsteonPeer> peer);
	std::shared_ptr<InsteonPeer> createPeer(int32_t address, int32_t firmwareVersion, uint32_t deviceType, std::string serialNumber, bool save = true);
	void createPairingQueue(int32_t address, std::string interfaceID, std::shared_ptr<InsteonPeer> peer = nullptr);
	void deletePeer(uint64_t id);
	virtual std::shared_ptr<IPhysicalInterface> getPhysicalInterface(int32_t peerAddress, std::string interfaceID = "");
	virtual void setUpInsteonMessages();
	virtual void worker();
	virtual void init();
	void enablePairingMode(std::string interfaceID = "");
	void disablePairingMode(std::string interfaceID = "");

	void addHomegearFeatures(std::shared_ptr<InsteonPeer> peer);
	void addHomegearFeaturesValveDrive(std::shared_ptr<InsteonPeer> peer);
};

}

#endif
