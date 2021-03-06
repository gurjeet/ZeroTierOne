/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2012-2013  ZeroTier Networks LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#ifndef _ZT_PACKETDECODER_HPP
#define _ZT_PACKETDECODER_HPP

#include <stdexcept>

#include "Packet.hpp"
#include "Demarc.hpp"
#include "InetAddress.hpp"
#include "Utils.hpp"
#include "SharedPtr.hpp"
#include "AtomicCounter.hpp"
#include "Peer.hpp"

/*
 * The big picture:
 *
 * tryDecode gets called for a given fully-assembled packet until it returns
 * true or the packet's time to live has been exceeded, in which case it is
 * discarded as failed decode. Any exception thrown by tryDecode also causes
 * the packet to be discarded.
 *
 * Thus a return of false from tryDecode() indicates that it should be called
 * again. Logic is very simple as to when, and it's in doAnythingWaitingForPeer
 * in Switch. This might be expanded to be more fine grained in the future.
 *
 * A return value of true indicates that the packet is done. tryDecode must
 * never be called again after that.
 */

namespace ZeroTier {

class RuntimeEnvironment;

/**
 * Subclass of packet that handles the decoding of it
 */
class PacketDecoder : public Packet
{
	friend class SharedPtr<PacketDecoder>;

public:
	/**
	 * Create a new packet-in-decode
	 *
	 * @param b Source buffer with raw packet data
	 * @param localPort Local port on which packet was received
	 * @param remoteAddress Address from which packet came
	 * @throws std::out_of_range Range error processing packet
	 */
	template<unsigned int C2>
	PacketDecoder(const Buffer<C2> &b,Demarc::Port localPort,const InetAddress &remoteAddress)
 		throw(std::out_of_range) :
 		Packet(b),
 		_receiveTime(Utils::now()),
 		_localPort(localPort),
 		_remoteAddress(remoteAddress),
 		_step(DECODE_WAITING_FOR_SENDER_LOOKUP),
 		__refCount()
	{
	}

	/**
	 * Attempt to decode this packet
	 *
	 * Note that this returns 'true' if processing is complete. This says nothing
	 * about whether the packet was valid. A rejection is 'complete.'
	 *
	 * Once true is returned, this must not be called again. The packet's state
	 * may no longer be valid.
	 *
	 * @param _r Runtime environment
	 * @return True if decoding and processing is complete, false if caller should try again
	 * @throws std::out_of_range Range error processing packet (should be discarded)
	 * @throws std::runtime_error Other error processing packet (should be discarded)
	 */
	bool tryDecode(const RuntimeEnvironment *_r);

	/**
	 * @return Time of packet receipt / start of decode
	 */
	inline uint64_t receiveTime() const throw() { return _receiveTime; }

private:
	// These are called internally to handle packet contents once it has
	// been authenticated, decrypted, decompressed, and classified.
	bool _doERROR(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doHELLO(const RuntimeEnvironment *_r);
	bool _doOK(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doWHOIS(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doRENDEZVOUS(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doFRAME(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doBRIDGED_FRAME(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doMULTICAST_FRAME(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doMULTICAST_LIKE(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doNETWORK_MEMBERSHIP_CERTIFICATE(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doNETWORK_CONFIG_REQUEST(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);
	bool _doNETWORK_CONFIG_REFRESH(const RuntimeEnvironment *_r,const SharedPtr<Peer> &peer);

	uint64_t _receiveTime;
	Demarc::Port _localPort;
	InetAddress _remoteAddress;

	enum {
		DECODE_WAITING_FOR_SENDER_LOOKUP, // on initial receipt, we need peer's identity
		DECODE_WAITING_FOR_MULTICAST_FRAME_ORIGINAL_SENDER_LOOKUP,
		DECODE_WAITING_FOR_NETWORK_MEMBERSHIP_CERTIFICATE_SIGNER_LOOKUP
	} _step;

	AtomicCounter __refCount;
};

} // namespace ZeroTier

#endif
