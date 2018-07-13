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

#include "InsteonCentral.h"
#include "GD.h"

namespace Insteon {

InsteonCentral::InsteonCentral(ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(INSTEON_FAMILY_ID, GD::bl, eventHandler)
{
	init();
}

InsteonCentral::InsteonCentral(uint32_t deviceID, std::string serialNumber, int32_t address, ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(INSTEON_FAMILY_ID, GD::bl, deviceID, serialNumber, address, eventHandler)
{
	init();
}

InsteonCentral::~InsteonCentral()
{
	dispose();
}

void InsteonCentral::init()
{
	try
	{
		if(_initialized) return; //Prevent running init two times
		_initialized = true;

		_messages = std::shared_ptr<InsteonMessages>(new InsteonMessages());
		_stopWorkerThread = false;
		_stopPairingModeThread = false;
		_abortPairingModeThread = false;
		_pairing = false;
		_timeLeftInPairingMode = 0;

		setUpInsteonMessages();

		_bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &InsteonCentral::worker, this);

		for(std::map<std::string, std::shared_ptr<IInsteonInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			_physicalInterfaceEventhandlers[i->first] = i->second->addEventHandler((IPhysicalInterface::IPhysicalInterfaceEventSink*)this);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void InsteonCentral::dispose(bool wait)
{
	try
	{
		if(_disposing) return;
		_disposing = true;
		GD::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
		for(std::map<std::string, std::shared_ptr<IInsteonInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			//Just to make sure cycle through all physical devices. If event handler is not removed => segfault
			i->second->removeEventHandler(_physicalInterfaceEventhandlers[i->first]);
		}

		stopThreads();

		_queueManager.dispose(false);
		_receivedPackets.dispose(false);
		_sentPackets.dispose(false);
	}
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::stopThreads()
{
	try
	{
		_unpairThreadMutex.lock();
		_bl->threadManager.join(_unpairThread);
		_unpairThreadMutex.unlock();

		_pairingModeThreadMutex.lock();
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_pairingModeThreadMutex.unlock();

		_peersMutex.lock();
		for(std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::const_iterator i = _peers.begin(); i != _peers.end(); ++i)
		{
			i->second->dispose();
		}
		_peersMutex.unlock();

		_stopWorkerThread = true;
		GD::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
		_bl->threadManager.join(_workerThread);
	}
    catch(const std::exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::setUpInsteonMessages()
{
	try
	{
		_messages->add(std::shared_ptr<InsteonMessage>(new InsteonMessage(0x01, -1, InsteonPacketFlags::Broadcast, ACCESSPAIREDTOSENDER, FULLACCESS, &InsteonCentral::handlePairingRequest)));

		_messages->add(std::shared_ptr<InsteonMessage>(new InsteonMessage(0x09, 0x01, InsteonPacketFlags::DirectAck, ACCESSPAIREDTOSENDER, FULLACCESS, &InsteonCentral::handleLinkingModeResponse)));

		_messages->add(std::shared_ptr<InsteonMessage>(new InsteonMessage(0x2F, -1, InsteonPacketFlags::Direct, ACCESSPAIREDTOSENDER, FULLACCESS, &InsteonCentral::handleDatabaseOpResponse)));

		_messages->add(std::shared_ptr<InsteonMessage>(new InsteonMessage(0x2F, -1, InsteonPacketFlags::DirectAck, ACCESSPAIREDTOSENDER, FULLACCESS, &InsteonCentral::handleDatabaseOpResponse)));
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<IPhysicalInterface> InsteonCentral::getPhysicalInterface(int32_t peerAddress, std::string interfaceID)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(peerAddress, interfaceID);
		if(queue && queue->getPhysicalInterface()) return queue->getPhysicalInterface();
		std::shared_ptr<InsteonPeer> peer = getPeer(peerAddress);
		return peer ? peer->getPhysicalInterface() : GD::defaultPhysicalInterface;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return GD::defaultPhysicalInterface;
}

void InsteonCentral::worker()
{
	try
	{
		std::chrono::milliseconds sleepingTime(10);
		uint32_t counter = 0;
		int32_t lastPeer;
		lastPeer = 0;
		//One loop on the Raspberry Pi takes about 30Âµs
		while(!_stopWorkerThread)
		{
			try
			{
				std::this_thread::sleep_for(sleepingTime);
				if(_stopWorkerThread) return;
				if(counter > 10000)
				{
					counter = 0;
					_peersMutex.lock();
					if(_peers.size() > 0)
					{
						int32_t windowTimePerPeer = _bl->settings.workerThreadWindow() / _peers.size();
						if(windowTimePerPeer > 2) windowTimePerPeer -= 2;
						sleepingTime = std::chrono::milliseconds(windowTimePerPeer);
					}
					_peersMutex.unlock();
				}
				if(_manualPairingModeStarted > -1 && (BaseLib::HelperFunctions::getTime() - _manualPairingModeStarted) > 60000)
				{
					disablePairingMode();
					_manualPairingModeStarted = -1;
				}
				_peersMutex.lock();
				if(!_peers.empty())
				{
					if(!_peers.empty())
					{
						std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator nextPeer = _peers.find(lastPeer);
						if(nextPeer != _peers.end())
						{
							nextPeer++;
							if(nextPeer == _peers.end()) nextPeer = _peers.begin();
						}
						else nextPeer = _peers.begin();
						lastPeer = nextPeer->first;
					}
				}
				_peersMutex.unlock();
				std::shared_ptr<InsteonPeer> peer(getPeer(lastPeer));
				if(peer && !peer->deleting) peer->worker();
				counter++;
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::loadPeers()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			int32_t peerID = row->second.at(0)->intValue;
			GD::out.printMessage("Loading peer " + std::to_string(peerID));
			int32_t address = row->second.at(2)->intValue;
			std::shared_ptr<InsteonPeer> peer(new InsteonPeer(peerID, address, row->second.at(3)->textValue, _deviceId, this));
			if(!peer->load(this)) continue;
			if(!peer->getRpcDevice()) continue;
			_peersMutex.lock();
			_peers[peer->getAddress()] = peer;
			if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
			_peersById[peerID] = peer;
			_peersMutex.unlock();
			peer->getPhysicalInterface()->addPeer(peer->getAddress());
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	_peersMutex.unlock();
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	_peersMutex.unlock();
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    	_peersMutex.unlock();
    }
}

void InsteonCentral::loadVariables()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getDeviceVariables(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			_variableDatabaseIds[row->second.at(2)->intValue] = row->second.at(0)->intValue;
			switch(row->second.at(2)->intValue)
			{
			case 0:
				_firmwareVersion = row->second.at(3)->intValue;
				break;
			case 1:
				_centralAddress = row->second.at(3)->intValue;
				break;
			}
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::savePeers(bool full)
{
	try
	{
		_peersMutex.lock();
		for(std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peers.begin(); i != _peers.end(); ++i)
		{
			//Necessary, because peers can be assigned to multiple virtual devices
			if(i->second->getParentID() != _deviceId) continue;
			//We are always printing this, because the init script needs it
			GD::out.printMessage("(Shutdown) => Saving peer " + std::to_string(i->second->getID()));
			i->second->save(full, full, full);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
	_peersMutex.unlock();
}

void InsteonCentral::saveVariables()
{
	try
	{
		if(_deviceId == 0) return;
		saveVariable(0, _firmwareVersion);
		saveVariable(1, _centralAddress);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<InsteonPeer> InsteonCentral::getPeer(int32_t address)
{
	try
	{
		_peersMutex.lock();
		if(_peers.find(address) != _peers.end())
		{
			std::shared_ptr<InsteonPeer> peer(std::dynamic_pointer_cast<InsteonPeer>(_peers.at(address)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<InsteonPeer>();
}

std::shared_ptr<InsteonPeer> InsteonCentral::getPeer(uint64_t id)
{
	try
	{
		_peersMutex.lock();
		if(_peersById.find(id) != _peersById.end())
		{
			std::shared_ptr<InsteonPeer> peer(std::dynamic_pointer_cast<InsteonPeer>(_peersById.at(id)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<InsteonPeer>();
}

std::shared_ptr<InsteonPeer> InsteonCentral::getPeer(std::string serialNumber)
{
	try
	{
		_peersMutex.lock();
		if(_peersBySerial.find(serialNumber) != _peersBySerial.end())
		{
			std::shared_ptr<InsteonPeer> peer(std::dynamic_pointer_cast<InsteonPeer>(_peersBySerial.at(serialNumber)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<InsteonPeer>();
}

bool InsteonCentral::onPacketReceived(std::string& senderID, std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(_disposing) return false;
		std::shared_ptr<InsteonPacket> insteonPacket(std::dynamic_pointer_cast<InsteonPacket>(packet));
		if(!insteonPacket) return false;
		if(GD::bl->debugLevel >= 4) std::cout << BaseLib::HelperFunctions::getTimeString(insteonPacket->timeReceived()) << " Insteon packet received: " + insteonPacket->hexString() << std::endl;
		if(insteonPacket->senderAddress() == _address) //Packet spoofed
		{
			std::shared_ptr<InsteonPeer> peer(getPeer(insteonPacket->destinationAddress()));
			if(peer)
			{
				if(senderID != peer->getPhysicalInterfaceID()) return true; //Packet we sent was received by another interface
				GD::out.printWarning("Warning: Central address of packet to peer " + std::to_string(peer->getID()) + " was spoofed. Packet was: " + packet->hexString());
				peer->serviceMessages->set("CENTRAL_ADDRESS_SPOOFED", 1, 0);
				std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string> { "CENTRAL_ADDRESS_SPOOFED" });
				std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable> { PVariable(new Variable((int32_t)1)) });
                std::string eventSource = "device-" + std::to_string(peer->getID());
                std::string address = peer->getSerialNumber() + ":0";
				raiseRPCEvent(eventSource, peer->getID(), 0, address, valueKeys, values);
				return true;
			}
			return false;
		}
		std::shared_ptr<IPhysicalInterface> physicalInterface = getPhysicalInterface(insteonPacket->senderAddress(), insteonPacket->interfaceID());
		//Allow pairing packets from all interfaces when pairing mode is enabled.
		if(!(_pairing && insteonPacket->messageType() == 0x01) && physicalInterface->getID() != senderID) return true;

		bool handled = false;
		if(_receivedPackets.set(insteonPacket->senderAddress(), insteonPacket, insteonPacket->timeReceived())) handled = true;
		if(insteonPacket->flags() == InsteonPacketFlags::DirectNak || insteonPacket->flags() == InsteonPacketFlags::GroupCleanupDirectNak)
		{
			handleNak(insteonPacket);
			handled =  true;
		}
		else
		{
			std::shared_ptr<InsteonMessage> message = _messages->find(insteonPacket);
			if(message)
			{
				if(message->checkAccess(insteonPacket, _queueManager.get(insteonPacket->senderAddress(), senderID)))
				{
					if(_bl->debugLevel >= 5) GD::out.printDebug("Debug: Device " + std::to_string(_deviceId) + ": Access granted for packet " + insteonPacket->hexString());
					message->invokeMessageHandler(insteonPacket);
					handled =  true;
				}
				else if(_bl->debugLevel >= 5) GD::out.printDebug("Debug: Device " + std::to_string(_deviceId) + ": Access rejected for packet " + insteonPacket->hexString());
			}
			else
			{
				handleAck(insteonPacket);
			}
		}

		std::shared_ptr<InsteonPeer> peer(getPeer(insteonPacket->senderAddress()));
		if(!peer) return false;
		std::shared_ptr<InsteonPeer> team;
		if(handled)
		{
			//This block is not necessary for teams as teams will never have queues.
			std::shared_ptr<PacketQueue> queue = _queueManager.get(insteonPacket->senderAddress(), senderID);
			if(queue && queue->getQueueType() != PacketQueueType::PEER)
			{
				peer->setLastPacketReceived();
				peer->serviceMessages->endUnreach();
				return true; //Packet is handled by queue. Don't check if queue is empty!
			}
		}
		peer->packetReceived(insteonPacket);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return false;
}

void InsteonCentral::sendPacket(std::shared_ptr<IPhysicalInterface> physicalInterface, std::shared_ptr<InsteonPacket> packet, bool stealthy)
{
	try
	{
		if(!packet || !physicalInterface) return;
		uint32_t responseDelay = physicalInterface->responseDelay();
		std::shared_ptr<InsteonPacketInfo> packetInfo = _sentPackets.getInfo(packet->destinationAddress());
		if(!stealthy) _sentPackets.set(packet->destinationAddress(), packet);
		if(packetInfo)
		{
			int64_t timeDifference = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - packetInfo->time;
			if(timeDifference < responseDelay)
			{
				packetInfo->time += responseDelay - timeDifference; //Set to sending time
				std::this_thread::sleep_for(std::chrono::milliseconds(responseDelay - timeDifference));
			}
		}
		if(stealthy) _sentPackets.keepAlive(packet->destinationAddress());
		packetInfo = _receivedPackets.getInfo(packet->destinationAddress());
		if(packetInfo)
		{
			int64_t time = BaseLib::HelperFunctions::getTime();
			int64_t timeDifference = time - packetInfo->time;
			if(timeDifference >= 0 && timeDifference < responseDelay)
			{
				int64_t sleepingTime = responseDelay - timeDifference;
				if(sleepingTime > 1) sleepingTime -= 1;
				packet->setTimeSending(time + sleepingTime + 1);
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepingTime));
			}
			//Set time to now. This is necessary if two packets are sent after each other without a response in between
			packetInfo->time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			_receivedPackets.deletePacket(packet->destinationAddress(), packetInfo->id, true);
		}
		else if(_bl->debugLevel > 4) GD::out.printDebug("Debug: Sending packet " + packet->hexString() + " immediately, because it seems it is no response (no packet information found).", 7);
		physicalInterface->sendPacket(packet);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::unpair(uint64_t id)
{
	try
	{
		std::shared_ptr<InsteonPeer> peer(getPeer(id));
		if(!peer) return;

		while(!peer->pendingQueues->empty()) peer->pendingQueues->pop();
		peer->serviceMessages->setConfigPending(true);

		std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(getPhysicalInterface(peer->getAddress()), PacketQueueType::UNPAIRING, peer->getAddress());
		queue->peer = peer;

		peer->getPhysicalInterface()->removePeer(peer->getAddress());

		std::vector<uint8_t> payload;
		payload.push_back(0); //Write
		payload.push_back(2); //Write
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xF7); //Second byte of link database address
		payload.push_back(0x08); //Entry size?
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0xC1); //Always the same checksum, so we don't calculate to safe ressources
		std::shared_ptr<InsteonPacket> configPacket(new InsteonPacket(0x2F, 0, peer->getAddress(), 3, 3, InsteonPacketFlags::Direct, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::DirectAck, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(0); //Write
		payload.push_back(2); //Write
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xFF); //Second byte of link database address
		payload.push_back(0x08); //Entry size?
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0xB9); //Always the same checksum, so we don't calculate to safe ressources
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, peer->getAddress(), 3, 3, InsteonPacketFlags::Direct, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::DirectAck, std::vector<std::pair<uint32_t, int32_t>>()));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::deletePeer(uint64_t id)
{
	try
	{
		std::shared_ptr<InsteonPeer> peer(getPeer(id));
		if(!peer) return;
		peer->deleting = true;
		PVariable deviceAddresses(new Variable(VariableType::tArray));
		deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber())));

		PVariable deviceInfo(new Variable(VariableType::tStruct));
		deviceInfo->structValue->insert(StructElement("ID", PVariable(new Variable((int32_t)peer->getID()))));
		PVariable channels(new Variable(VariableType::tArray));
		deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

		std::shared_ptr<HomegearDevice> rpcDevice = peer->getRpcDevice();
		for(Functions::iterator i = rpcDevice->functions.begin(); i != rpcDevice->functions.end(); ++i)
		{
			deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber() + ":" + std::to_string(i->first))));
			channels->arrayValue->push_back(PVariable(new Variable(i->first)));
		}

        std::vector<uint64_t> deletedIds{ id };
		raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

		{
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			if(_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
			if(_peersById.find(id) != _peersById.end()) _peersById.erase(id);
			if(_peers.find(peer->getAddress()) != _peers.end()) _peers.erase(peer->getAddress());
		}

		if(_currentPeer && _currentPeer->getID() == id) _currentPeer.reset();

		int32_t i = 0;
		while(peer.use_count() > 1 && i < 600)
		{
			if(_currentPeer && _currentPeer->getID() == id) _currentPeer.reset();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			i++;
		}
		if(i == 600) GD::out.printError("Error: Peer deletion took too long.");

		peer->deleteFromDatabase();
		GD::out.printMessage("Removed peer " + std::to_string(peer->getID()));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::string InsteonCentral::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;
		if(_currentPeer)
		{
			if(command == "unselect" || command == "u")
			{
				_currentPeer.reset();
				return "Peer unselected.\n";
			}
			return _currentPeer->handleCliCommand(command);
		}
		if(command == "help" || command == "h")
		{
			stringStream << "List of commands (shortcut in brackets):" << std::endl << std::endl;
			stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
			stringStream << "pairing on (pon)\tEnables pairing mode" << std::endl;
			stringStream << "pairing off (pof)\tDisables pairing mode" << std::endl;
			stringStream << "peers list (ls)\t\tList all peers" << std::endl;
			stringStream << "peers remove (prm)\tRemove a peer (without unpairing)" << std::endl;
			stringStream << "peers select (ps)\tSelect a peer" << std::endl;
			stringStream << "peers setname (pn)\tName a peer" << std::endl;
			stringStream << "peers unpair (pup)\tUnpair a peer" << std::endl;
			stringStream << "unselect (u)\t\tUnselect this device" << std::endl;
			return stringStream.str();
		}
		if(command.compare(0, 10, "pairing on") == 0 || command.compare(0, 3, "pon") == 0)
		{
			int32_t duration = 60;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'o') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help")
					{
						stringStream << "Description: This command enables pairing mode." << std::endl;
						stringStream << "Usage: pairing on [DURATION]" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  DURATION:\tOptional duration in seconds to stay in pairing mode." << std::endl;
						return stringStream.str();
					}
					duration = BaseLib::Math::getNumber(element, false);
					if(duration < 5 || duration > 3600) return "Invalid duration. Duration has to be greater than 5 and less than 3600.\n";
				}
				index++;
			}

			setInstallMode(nullptr, true, duration, nullptr, false);
			stringStream << "Pairing mode enabled." << std::endl;
			return stringStream.str();
		}
		else if(command.compare(0, 11, "pairing off") == 0 || command.compare(0, 3, "pof") == 0)
		{
			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'o') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help")
					{
						stringStream << "Description: This command disables pairing mode." << std::endl;
						stringStream << "Usage: pairing off" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  There are no parameters." << std::endl;
						return stringStream.str();
					}
				}
				index++;
			}

			setInstallMode(nullptr, false, -1, nullptr, false);
			stringStream << "Pairing mode disabled." << std::endl;
			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers remove") == 0 || command.compare(0, 3, "prm") == 0)
		{
			uint64_t peerID = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'r') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					peerID = BaseLib::Math::getNumber(element, false);
					if(peerID == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command removes a peer without trying to unpair it first." << std::endl;
				stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to remove. Example: 513" << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				if(_currentPeer && _currentPeer->getID() == peerID) _currentPeer.reset();
				deletePeer(peerID);
				stringStream << "Removed peer " << std::to_string(peerID) << "." << std::endl;
			}
			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers unpair") == 0 || command.compare(0, 3, "pup") == 0)
		{
			uint64_t peerID = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'u') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					peerID = BaseLib::Math::getNumber(element, false);
					if(peerID == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command unpairs a peer." << std::endl;
				stringStream << "Usage: peers unpair PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to unpair. Example: 513" << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				if(_currentPeer && _currentPeer->getID() == peerID) _currentPeer.reset();
				stringStream << "Unpairing peer " << std::to_string(peerID) << std::endl;
				unpair(peerID);
			}
			return stringStream.str();
		}
		else if(command.compare(0, 10, "peers list") == 0 || command.compare(0, 2, "pl") == 0 || command.compare(0, 2, "ls") == 0)
		{
			try
			{
				std::string filterType;
				std::string filterValue;

				std::stringstream stream(command);
				std::string element;
				int32_t offset = (command.at(1) == 'l' || command.at(1) == 's') ? 0 : 1;
				int32_t index = 0;
				while(std::getline(stream, element, ' '))
				{
					if(index < 1 + offset)
					{
						index++;
						continue;
					}
					else if(index == 1 + offset)
					{
						if(element == "help")
						{
							index = -1;
							break;
						}
						filterType = BaseLib::HelperFunctions::toLower(element);
					}
					else if(index == 2 + offset)
					{
						filterValue = element;
						if(filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
					}
					index++;
				}
				if(index == -1)
				{
					stringStream << "Description: This command lists information about all peers." << std::endl;
					stringStream << "Usage: peers list [FILTERTYPE] [FILTERVALUE]" << std::endl << std::endl;
					stringStream << "Parameters:" << std::endl;
					stringStream << "  FILTERTYPE:\tSee filter types below." << std::endl;
					stringStream << "  FILTERVALUE:\tDepends on the filter type. If a number is required, it has to be in hexadecimal format." << std::endl << std::endl;
					stringStream << "Filter types:" << std::endl;
					stringStream << "  ID: Filter by id." << std::endl;
					stringStream << "      FILTERVALUE: The id of the peer to filter (e. g. 513)." << std::endl;
					stringStream << "  ADDRESS: Filter by address." << std::endl;
					stringStream << "      FILTERVALUE: The 4 byte address of the peer to filter (e. g. 001DA44D)." << std::endl;
					stringStream << "  SERIAL: Filter by serial number." << std::endl;
					stringStream << "      FILTERVALUE: The serial number of the peer to filter (e. g. JEQ0554309)." << std::endl;
					stringStream << "  NAME: Filter by name." << std::endl;
					stringStream << "      FILTERVALUE: The part of the name to search for (e. g. \"1st floor\")." << std::endl;
					stringStream << "  TYPE: Filter by device type." << std::endl;
					stringStream << "      FILTERVALUE: The 2 byte device type in hexadecimal format." << std::endl;
					stringStream << "  UNREACH: List all unreachable peers." << std::endl;
					stringStream << "      FILTERVALUE: empty" << std::endl;
					return stringStream.str();
				}

				if(_peers.empty())
				{
					stringStream << "No peers are paired to this central." << std::endl;
					return stringStream.str();
				}
				std::string bar(" │ ");
				const int32_t idWidth = 8;
				const int32_t nameWidth = 25;
				const int32_t addressWidth = 7;
				const int32_t serialWidth = 13;
				const int32_t typeWidth1 = 4;
				const int32_t typeWidth2 = 25;
				const int32_t firmwareWidth = 8;
				const int32_t unreachWidth = 7;
				std::string nameHeader("Name");
				nameHeader.resize(nameWidth, ' ');
				std::string typeStringHeader("Type String");
				typeStringHeader.resize(typeWidth2, ' ');
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << "ID" << bar
					<< nameHeader << bar
					<< std::setw(addressWidth) << "Address" << bar
					<< std::setw(serialWidth) << "Serial Number" << bar
					<< std::setw(typeWidth1) << "Type" << bar
					<< typeStringHeader << bar
					<< std::setw(firmwareWidth) << "Firmware" << bar
					<< std::setw(unreachWidth) << "Unreach"
					<< std::endl;
				stringStream << "─────────┼───────────────────────────┼─────────┼───────────────┼──────┼───────────────────────────┼──────────┼────────" << std::endl;
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << " " << bar
					<< std::setw(nameWidth) << " " << bar
					<< std::setw(addressWidth) << " " << bar
					<< std::setw(serialWidth) << " " << bar
					<< std::setw(typeWidth1) << " " << bar
					<< std::setw(typeWidth2) << " " << bar
					<< std::setw(firmwareWidth) << " " << bar
					<< std::setw(unreachWidth) << " "
					<< std::endl;
				_peersMutex.lock();
				for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
				{
					if(filterType == "id")
					{
						uint64_t id = BaseLib::Math::getNumber(filterValue, false);
						if(i->second->getID() != id) continue;
					}
					else if(filterType == "name")
					{
						std::string name = i->second->getName();
						if((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
					}
					else if(filterType == "address")
					{
						int32_t address = BaseLib::Math::getNumber(filterValue, true);
						if(i->second->getAddress() != address) continue;
					}
					else if(filterType == "serial")
					{
						if(i->second->getSerialNumber() != filterValue) continue;
					}
					else if(filterType == "type")
					{
						int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
						if((int32_t)i->second->getDeviceType() != deviceType) continue;
					}
					else if(filterType == "unreach")
					{
						if(i->second->serviceMessages)
						{
							if(!i->second->serviceMessages->getUnreach()) continue;
						}
					}

					stringStream << std::setw(idWidth) << std::setfill(' ') << std::to_string(i->second->getID()) << bar;
					std::string name = i->second->getName();
					size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
					if(nameSize > (unsigned)nameWidth)
					{
						name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameWidth - 3);
						name += "...";
					}
					else name.resize(nameWidth + (name.size() - nameSize), ' ');
					stringStream << name << bar
						<< std::setw(addressWidth) << BaseLib::HelperFunctions::getHexString(i->second->getAddress(), 6) << bar
						<< std::setw(serialWidth) << i->second->getSerialNumber() << bar
						<< std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(i->second->getDeviceType(), 4) << bar;
					if(i->second->getRpcDevice())
					{
						PSupportedDevice type = i->second->getRpcDevice()->getType(i->second->getDeviceType(), i->second->getFirmwareVersion());
						std::string typeID;
						if(type) typeID = type->id;
						if(typeID.size() > (unsigned)typeWidth2)
						{
							typeID.resize(typeWidth2 - 3);
							typeID += "...";
						}
						else typeID.resize(typeWidth2, ' ');
						stringStream << typeID << bar;
					}
					else stringStream << std::setw(typeWidth2) << " " << bar;
					if(i->second->getFirmwareVersion() == 0) stringStream << std::setfill(' ') << std::setw(firmwareWidth) << "?" << bar;
					else if(i->second->firmwareUpdateAvailable())
					{
						stringStream << std::setfill(' ') << std::setw(firmwareWidth) << ("*" + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() >> 4) + "." + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() & 0x0F)) << bar;
					}
					else stringStream << std::setfill(' ') << std::setw(firmwareWidth) << (BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() >> 4) + "." + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() & 0x0F)) << bar;
					if(i->second->serviceMessages)
					{
						std::string unreachable(i->second->serviceMessages->getUnreach() ? "Yes" : "No");
						stringStream << std::setw(unreachWidth) << unreachable;
					}
					stringStream << std::endl << std::dec;
				}
				_peersMutex.unlock();
				stringStream << "─────────┴───────────────────────────┴─────────┴───────────────┴──────┴───────────────────────────┴──────────┴────────" << std::endl;

				return stringStream.str();
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
		else if(command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0)
		{
			uint64_t peerID = 0;
			std::string name;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'n') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					else
					{
						peerID = BaseLib::Math::getNumber(element, false);
						if(peerID == 0) return "Invalid id.\n";
					}
				}
				else if(index == 2 + offset) name = element;
				else name += ' ' + element;
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
				stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
				stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				std::shared_ptr<InsteonPeer> peer = getPeer(peerID);
				peer->setName(name);
				stringStream << "Name set to \"" << name << "\"." << std::endl;
			}
			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers select") == 0 || command.compare(0, 2, "ps") == 0)
		{
			uint64_t id = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 's') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					id = BaseLib::Math::getNumber(element, false);
					if(id == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command selects a peer." << std::endl;
				stringStream << "Usage: peers select PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to select. Example: 513" << std::endl;
				return stringStream.str();
			}

			_currentPeer = getPeer(id);
			if(!_currentPeer) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				stringStream << "Peer with id " << std::hex << std::to_string(id) << " and device type 0x" << (int32_t)_currentPeer->getDeviceType() << " selected." << std::dec << std::endl;
				stringStream << "For information about the peer's commands type: \"help\"" << std::endl;
			}
			return stringStream.str();
		}
		else return "Unknown command.\n";
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return "Error executing command. See log file for more details.\n";
}

bool InsteonCentral::enqueuePendingQueues(int32_t deviceAddress, bool wait)
{
	try
	{
		_enqueuePendingQueuesMutex.lock();
		std::shared_ptr<InsteonPeer> peer = getPeer(deviceAddress);
		if(!peer || !peer->pendingQueues)
		{
			_enqueuePendingQueuesMutex.unlock();
			return true;
		}
		std::shared_ptr<PacketQueue> queue = _queueManager.get(deviceAddress, peer->getPhysicalInterfaceID());
		if(!queue) queue = _queueManager.createQueue(peer->getPhysicalInterface(), PacketQueueType::DEFAULT, deviceAddress);
		if(!queue)
		{
			_enqueuePendingQueuesMutex.unlock();
			return true;
		}
		if(!queue->peer) queue->peer = peer;
		if(queue->pendingQueuesEmpty()) queue->push(peer->pendingQueues);
		_enqueuePendingQueuesMutex.unlock();

		if(wait)
		{
			int32_t waitIndex = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			while(!peer->pendingQueuesEmpty() && waitIndex < 100)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				waitIndex++;
			}

			if(!peer->pendingQueuesEmpty()) return false;
		}

		return true;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _enqueuePendingQueuesMutex.unlock();
    return false;
}

std::shared_ptr<InsteonPeer> InsteonCentral::createPeer(int32_t address, int32_t firmwareVersion, uint32_t deviceType, std::string serialNumber, bool save)
{
	try
	{
		std::shared_ptr<InsteonPeer> peer(new InsteonPeer(_deviceId, this));
		peer->setAddress(address);
		peer->setFirmwareVersion(firmwareVersion);
		peer->setDeviceType(deviceType);
		peer->setSerialNumber(serialNumber);
		peer->setRpcDevice(GD::family->getRpcDevices()->find(deviceType, firmwareVersion, -1));
		if(!peer->getRpcDevice()) return std::shared_ptr<InsteonPeer>();
		if(save) peer->save(true, true, false); //Save and create peerID
		return peer;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<InsteonPeer>();
}

void InsteonCentral::addHomegearFeatures(std::shared_ptr<InsteonPeer> peer)
{
	try
	{
		if(!peer) return;
		//if(peer->getDeviceType().type() == (uint32_t)DeviceType::INTEONBLA) addHomegearFeaturesValveDrive(peer);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void InsteonCentral::createPairingQueue(int32_t address, std::string interfaceID, std::shared_ptr<InsteonPeer> peer)
{
	try
	{
		std::shared_ptr<IInsteonInterface> interface = (GD::physicalInterfaces.find(interfaceID) == GD::physicalInterfaces.end()) ? GD::defaultPhysicalInterface : GD::physicalInterfaces.at(interfaceID);
		_abortPairingModeThread = true;
		disablePairingMode(interface->getID());
		std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(interface, PacketQueueType::PAIRING, address);
		queue->peer = peer;

		std::vector<uint8_t> payload;
		payload.push_back(1); //Read
		payload.push_back(0); //Read
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xFF); //Second byte of link database address
		payload.push_back(0x01);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0); //Needs to be "0"!
		std::shared_ptr<InsteonPacket> configPacket(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, -1, InsteonPacketFlags::Direct, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0xF6); //Always the same checksum, so we don't calculate to safe ressources
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x09, 0x01, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x09, 0x01, InsteonPacketFlags::DirectAck, std::vector<std::pair<uint32_t, int32_t>>()));
		queue->push(_messages->find(0x01, -1, InsteonPacketFlags::Broadcast, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(1); //Read
		payload.push_back(0); //Read
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xFF); //Second byte of link database address
		payload.push_back(0x01);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0); //Needs to be "0"!
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::Direct, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(0); //Write
		payload.push_back(2); //Write
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xFF); //Second byte of link database address
		payload.push_back(0x08); //Entry size?
		payload.push_back(0xA2); //Bit 7 => Record in use, Bit 6 => Controller, Bit 5 => ACK required, Bit 4, 3, 2 => reserved, Bit 1 => Record has been used before, Bit 0 => unused
		payload.push_back(0x00); //Group
		payload.push_back(interface->address() >> 16);
		payload.push_back((interface->address() >> 8) & 0xFF);
		payload.push_back(interface->address() & 0xFF);
		payload.push_back(0xFF); //Data1 => Level in this case
		payload.push_back(0x1F); //Data2 => Ramp rate?
		payload.push_back(0x01); //Data3
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::Direct, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::DirectAck, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(1); //Read
		payload.push_back(0); //Read
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xFF); //Second byte of link database address
		payload.push_back(0x01);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0); //Needs to be "0"!
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::Direct, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(1); //Read
		payload.push_back(0); //Read
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xF7); //Second byte of link database address
		payload.push_back(0x01);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0); //Needs to be "0"!
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::Direct, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(0); //Write
		payload.push_back(2); //Write
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xF7); //Second byte of link database address
		payload.push_back(0x08); //Entry size?
		payload.push_back(0xE2); //Bit 7 => Record in use, Bit 6 => Controller, Bit 5 => ACK required, Bit 4, 3, 2 => reserved, Bit 1 => Record has been used before, Bit 0 => unused
		payload.push_back(0x01); //Group
		payload.push_back(interface->address() >> 16);
		payload.push_back((interface->address() >> 8) & 0xFF);
		payload.push_back(interface->address() & 0xFF);
		payload.push_back(0x03); //Data1
		payload.push_back(0x1F); //Data2
		payload.push_back(0x01); //Data3
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::Direct, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::DirectAck, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();

		payload.push_back(1); //Read
		payload.push_back(0); //Read
		payload.push_back(0x0F); //First byte of link database address
		payload.push_back(0xF7); //Second byte of link database address
		payload.push_back(0x01);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0);
		payload.push_back(0); //Needs to be "0"!
		configPacket = std::shared_ptr<InsteonPacket>(new InsteonPacket(0x2F, 0, address, 3, 3, InsteonPacketFlags::DirectAck, payload));
		queue->push(configPacket);
		queue->push(_messages->find(0x2F, 0, InsteonPacketFlags::Direct, std::vector<std::pair<uint32_t, int32_t>>()));
		payload.clear();
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void InsteonCentral::enablePairingMode(std::string interfaceID)
{
	try
	{
		_manualPairingModeStarted = BaseLib::HelperFunctions::getTime();
		_pairing = true;
		if(interfaceID.empty())
		{
			for(std::map<std::string, std::shared_ptr<IInsteonInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); i++)
			{
				i->second->enablePairingMode();
			}
		}
		else
		{
			if(GD::physicalInterfaces.find(interfaceID) == GD::physicalInterfaces.end())
			{
				GD::defaultPhysicalInterface->enablePairingMode();
			}
			else
			{
				GD::physicalInterfaces.at(interfaceID)->enablePairingMode();
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::disablePairingMode(std::string interfaceID)
{
	try
	{
		if(interfaceID.empty())
		{
			_manualPairingModeStarted = -1;
			_pairing = false;
			for(std::map<std::string, std::shared_ptr<IInsteonInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); i++)
			{
				i->second->disablePairingMode();
			}
		}
		else
		{
			if(GD::physicalInterfaces.find(interfaceID) == GD::physicalInterfaces.end())
			{
				GD::defaultPhysicalInterface->disablePairingMode();
			}
			else
			{
				GD::physicalInterfaces.at(interfaceID)->disablePairingMode();
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

//Packet handlers
void InsteonCentral::handleNak(std::shared_ptr<InsteonPacket> packet)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress(), packet->interfaceID());
		if(!queue) return;
		std::shared_ptr<InsteonPacket> sentPacket(_sentPackets.get(packet->senderAddress()));
		if(queue->getQueueType() == PacketQueueType::PAIRING)
		{
			//Handle Nak in response to first pairing queue packet
			if(_bl->debugLevel >= 5)
			{
				if(sentPacket) GD::out.printDebug("Debug: NACK received from 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + " in response to " + sentPacket->hexString() + ".");
				else GD::out.printDebug("Debug: NACK received from 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6));
			}

			if(sentPacket && sentPacket->messageType() == 0x2F && sentPacket->payload()->size() == 14)
			{
				if(sentPacket->payload()->at(0) == 0x01 && sentPacket->payload()->at(1) == 0x00)
				{
					//First "read"
					enablePairingMode(packet->interfaceID());
				}
			}
			if(!queue->isEmpty() && queue->front()->getType() == QueueEntryType::PACKET) queue->pop(); //Pop sent packet
			queue->pop();
		}
		else if(queue->getQueueType() == PacketQueueType::UNPAIRING)
		{
			if(!queue->isEmpty() && queue->front()->getType() == QueueEntryType::PACKET) queue->pop(); //Pop sent packet
			queue->pop();
			if(queue->isEmpty())
			{
				//Just remove the peer. Probably the link database packet was sent twice, that's why we receive a NAK.
				std::shared_ptr<InsteonPeer> peer = getPeer(packet->senderAddress());
				if(peer)
				{
					uint64_t peerId = peer->getID();
					peer.reset();
					deletePeer(peerId);
				}
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::handleAck(std::shared_ptr<InsteonPacket> packet)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress(), packet->interfaceID());
		if(queue && !queue->isEmpty() && packet->destinationAddress() == _address)
		{
			if(queue->front()->getType() == QueueEntryType::PACKET)
			{
				std::shared_ptr<InsteonPacket> backup = queue->front()->getPacket();
				queue->pop(); //Popping takes place here to be able to process resent messages.
				if(!queue->isEmpty() && queue->front()->getType() == QueueEntryType::MESSAGE)
				{
					if(queue->front()->getMessage()->typeIsEqual(packet))
					{
						queue->pop();
					}
					else
					{
						GD::out.printDebug("Debug: Readding message to queue, because the received packet does not match.");
						queue->pushFront(backup);
						queue->processCurrentQueueEntry(true);
					}
				}
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::addPeer(std::shared_ptr<InsteonPeer> peer)
{
	try
	{
		if(!peer) return;
		try
		{
			_peersMutex.lock();
			_peers[peer->getAddress()] = peer;
			if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
			_peersMutex.unlock();
			peer->save(true, true, false);
			peer->initializeCentralConfig();
			_peersMutex.lock();
			_peersById[peer->getID()] = peer;
			_peersMutex.unlock();
		}
		catch(const std::exception& ex)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}
		catch(BaseLib::Exception& ex)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}
		catch(...)
		{
			_peersMutex.unlock();
			GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
		}
		peer->getPhysicalInterface()->addPeer(peer->getAddress());
		PVariable deviceDescriptions(new Variable(VariableType::tArray));
		deviceDescriptions->arrayValue = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
        std::vector<uint64_t> newIds{ peer->getID() };
		raiseRPCNewDevices(newIds, deviceDescriptions);
		GD::out.printMessage("Added peer 0x" + BaseLib::HelperFunctions::getHexString(peer->getAddress()) + ".");
		addHomegearFeatures(peer);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void InsteonCentral::handleDatabaseOpResponse(std::shared_ptr<InsteonPacket> packet)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress(), packet->interfaceID());
		if(!queue) return;
		std::shared_ptr<InsteonPacket> sentPacket(_sentPackets.get(packet->senderAddress()));

		if(queue->getQueueType() == PacketQueueType::PAIRING && sentPacket && sentPacket->messageType() == 0x2F && sentPacket->payload()->size() == 14)
		{
			if(queue->peer && sentPacket->payload()->at(0) == 1 && packet->payload()->size() == 14 && (packet->payload()->at(5) & 0x80)) //Read and "record is in use"
			{
				int32_t address = (packet->payload()->at(7) << 16) + (packet->payload()->at(8) << 8) + packet->payload()->at(9);
				if(packet->payload()->at(5) & 0x40) //Controller bit?
				{
					if(address == queue->peer->getPhysicalInterface()->address()) //Peer already knows me
					{
						addPeer(queue->peer);
						queue->clear();
						return;
					}
					else
					{
						GD::out.printWarning("Warning: Peer \"0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + "\" is already paired to another device with address \"0x" + BaseLib::HelperFunctions::getHexString(address, 6) + "\".");
					}
				}
				else if(address != queue->peer->getPhysicalInterface()->address()) //Peer paired to another peer
				{
					GD::out.printWarning("Warning: Peer \"0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + "\" is already paired to another device with address \"0x" + BaseLib::HelperFunctions::getHexString(address, 6) + "\".");
				}
			}
			else if(sentPacket->payload()->at(5) == 0xE2)
			{
				if(!peerExists(packet->senderAddress()))
				{
					addPeer(queue->peer);
				}
			}
		}
		else if(queue->getQueueType() == PacketQueueType::UNPAIRING && sentPacket && sentPacket->payload()->size() > 3 && sentPacket->payload()->at(3) == 0xFF)
		{
			std::shared_ptr<InsteonPeer> peer = getPeer(packet->senderAddress());
			if(peer)
			{
				uint64_t peerId = peer->getID();
				peer.reset();
				deletePeer(peerId);
			}
		}

		queue->pop(true); //Messages are not popped by default.

		if(queue->getQueueType() == PacketQueueType::PAIRING && !queue->isEmpty() && queue->front()->getType() == QueueEntryType::PACKET && queue->front()->getPacket()->messageType() == 0x09)
		{
			//Remove packets needed for a NAK as first response
			while(!queue->isEmpty())
			{
				if(queue->front()->getType() == QueueEntryType::PACKET && queue->front()->getPacket()->payload()->size() == 14 && queue->front()->getPacket()->payload()->at(1) == 2 && queue->front()->getPacket()->messageType() == 0x2F)
				{
					queue->processCurrentQueueEntry(false); //Necessary for the packet to be sent after silent popping
					break;
				}

				queue->pop(true);
				continue;
			}
			return;
		}
		else queue->processCurrentQueueEntry(false); //We popped silently before the "if"
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::handleLinkingModeResponse(std::shared_ptr<InsteonPacket> packet)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress(), packet->interfaceID());
		if(queue && queue->getQueueType() == PacketQueueType::PAIRING) queue->pop(); //Messages are not popped by default.
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void InsteonCentral::handlePairingRequest(std::shared_ptr<InsteonPacket> packet)
{
	try
	{
		uint32_t deviceType = packet->destinationAddress() >> 8;

		std::shared_ptr<InsteonPeer> peer(getPeer(packet->senderAddress()));
		if(peer && peer->getDeviceType() != deviceType)
		{
			GD::out.printError("Error: Pairing packet rejected, because a peer with the same address but different device type is already paired to this central.");
			return;
		}

		if(_pairing)
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress(), packet->interfaceID());
			if(queue)
			{
				//Handle pairing packet during pairing process, which is sent in response to "0x0901". Or pairing requests received through "addDevice".

				disablePairingMode(packet->interfaceID());

				if(!queue->peer)
				{
					int32_t firmwareVersion = packet->destinationAddress() & 0xFF;
					std::string serialNumber = BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6);
					//Do not save here
					queue->peer = createPeer(packet->senderAddress(), firmwareVersion, deviceType, serialNumber, false);
					if(!queue->peer)
					{
						queue->clear();
						GD::out.printWarning("Warning: Device type 0x" + GD::bl->hf.getHexString(deviceType, 4) + " with firmware version 0x" + BaseLib::HelperFunctions::getHexString(firmwareVersion, 4) + " not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
						return;
					}

					if(!queue->peer->getRpcDevice())
					{
						queue->clear();
						GD::out.printWarning("Warning: Device type not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
						return;
					}

					queue->peer->setPhysicalInterfaceID(packet->interfaceID());
				}

				if(queue->getQueueType() == PacketQueueType::PAIRING) queue->pop();
				return;
			}

			if(!peer)
			{
				int32_t firmwareVersion = packet->destinationAddress() & 0xFF;
				std::string serialNumber = BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6);
				//Do not save here
				peer = createPeer(packet->senderAddress(), firmwareVersion, deviceType, serialNumber, false);
				if(!peer)
				{
					GD::out.printWarning("Warning: Device type 0x" + GD::bl->hf.getHexString(deviceType, 4) + " with firmware version 0x" + BaseLib::HelperFunctions::getHexString(firmwareVersion, 4) + " not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
					return;
				}
			}

			if(!peer) return;

			if(!peer->getRpcDevice())
			{
				GD::out.printWarning("Warning: Device type not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
				return;
			}

			peer->setPhysicalInterfaceID(packet->interfaceID());

			createPairingQueue(peer->getAddress(), packet->interfaceID(), peer);
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}
//End packet handlers

//RPC functions
PVariable InsteonCentral::addDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Serial number is empty.");
		if(serialNumber.size() != 6  || !BaseLib::Math::isNumber(serialNumber)) return Variable::createError(-2, "Serial number length is not 6 or provided serial number is not a number.");

		_stopPairingModeThread = true;
		BaseLib::HelperFunctions::toUpper(serialNumber);

		std::shared_ptr<InsteonPeer> peer(getPeer(serialNumber));
		if(peer) return peer->getDeviceDescription(clientInfo, -1, std::map<std::string, bool>());

		int32_t address = BaseLib::Math::getNumber(serialNumber, true);
		for(std::map<std::string, std::shared_ptr<IInsteonInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); i++)
		{
			createPairingQueue(address, i->first, nullptr);
		}

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}

PVariable InsteonCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Unknown device.");
		if(serialNumber[0] == '*') return Variable::createError(-2, "Cannot delete virtual device.");

		uint64_t peerId = 0;

		{
			std::shared_ptr<InsteonPeer> peer = getPeer(serialNumber);
			if(!peer) return PVariable(new Variable(VariableType::tVoid));
			peerId = peer->getID();
		}

		return deleteDevice(clientInfo, peerId, flags);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable InsteonCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerId, int32_t flags)
{
	try
	{
		if(peerId == 0) return Variable::createError(-2, "Unknown device.");
		if(peerId & 0x80000000) return Variable::createError(-2, "Cannot delete virtual device.");

		int32_t address;
		std::string physicalInterfaceId;

		{
			std::shared_ptr<InsteonPeer> peer = getPeer(peerId);
			if(!peer) return PVariable(new Variable(VariableType::tVoid));
			address = peer->getAddress();
			physicalInterfaceId = peer->getPhysicalInterfaceID();
		}

		bool defer = flags & 0x04;
		bool force = flags & 0x02;

		{
			std::lock_guard<std::mutex> unpairGuard(_unpairThreadMutex);
			_bl->threadManager.join(_unpairThread);
			_bl->threadManager.start(_unpairThread, false, &InsteonCentral::unpair, this, peerId);
		}

		//Force delete
		if(force) deletePeer(peerId);
		else
		{
			int32_t waitIndex = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			while(_queueManager.get(address, physicalInterfaceId) && peerExists(peerId) && waitIndex < 20)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				waitIndex++;
			}
		}

		if(!defer && !force && peerExists(peerId)) return Variable::createError(-1, "No answer from device.");

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

std::shared_ptr<Variable> InsteonCentral::getInstallMode(BaseLib::PRpcClientInfo clientInfo)
{
	try
	{
		return std::shared_ptr<Variable>(new Variable(_timeLeftInPairingMode));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable InsteonCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t channel, ParameterGroup::Type::Enum type, std::string remoteSerialNumber, int32_t remoteChannel, PVariable paramset)
{
	try
	{
		std::shared_ptr<InsteonPeer> peer(getPeer(serialNumber));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		uint64_t remoteID = 0;
		if(!remoteSerialNumber.empty())
		{
			std::shared_ptr<InsteonPeer> remotePeer(getPeer(remoteSerialNumber));
			if(!remotePeer)
			{
				if(remoteSerialNumber != _serialNumber) return Variable::createError(-3, "Remote peer is unknown.");
			}
			else remoteID = remotePeer->getID();
		}
		PVariable result = peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset, false);
		if(result->errorStruct) return result;
		int32_t waitIndex = 0;
		while(_queueManager.get(peer->getAddress(), peer->getPhysicalInterfaceID()) && waitIndex < 40)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			waitIndex++;
		}
		return result;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable InsteonCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable paramset, bool checkAcls)
{
	try
	{
		std::shared_ptr<InsteonPeer> peer(getPeer(peerID));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		PVariable result = peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset, checkAcls);
		if(result->errorStruct) return result;
		int32_t waitIndex = 0;
		while(_queueManager.get(peer->getAddress(), peer->getPhysicalInterfaceID()) && waitIndex < 40)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			waitIndex++;
		}
		return result;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

void InsteonCentral::pairingModeTimer(int32_t duration, bool debugOutput)
{
	try
	{
		if(debugOutput) GD::out.printInfo("Info: Pairing mode enabled.");
		_timeLeftInPairingMode = duration;
		int64_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		int64_t timePassed = 0;
		while(timePassed < ((int64_t)duration * 1000) && !_stopPairingModeThread && !_abortPairingModeThread)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - startTime;
			_timeLeftInPairingMode = duration - (timePassed / 1000);
		}
		if(!_abortPairingModeThread) disablePairingMode();
		_timeLeftInPairingMode = 0;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode disabled.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<Variable> InsteonCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput)
{
	try
	{
		_pairingModeThreadMutex.lock();
		if(_disposing)
		{
			_pairingModeThreadMutex.unlock();
			return Variable::createError(-32500, "Central is disposing.");
		}
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_stopPairingModeThread = false;
		_abortPairingModeThread = false;
		_timeLeftInPairingMode = 0;
		_manualPairingModeStarted = -1;
		if(on && duration >= 5)
		{
			_timeLeftInPairingMode = duration; //Has to be set here, so getInstallMode is working immediately
			enablePairingMode("");
			_bl->threadManager.start(_pairingModeThread, false, &InsteonCentral::pairingModeTimer, this, duration, debugOutput);
		}
		_pairingModeThreadMutex.unlock();
		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _pairingModeThreadMutex.unlock();
    return Variable::createError(-32500, "Unknown application error.");
}
//End RPC functions
}
