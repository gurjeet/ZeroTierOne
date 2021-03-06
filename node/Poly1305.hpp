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

#ifndef _ZT_POLY1305_HPP
#define _ZT_POLY1305_HPP

namespace ZeroTier {

#define ZT_POLY1305_KEY_LEN 32
#define ZT_POLY1305_MAC_LEN 16

/**
 * Poly1305 one-time authentication code
 *
 * This takes a one-time-use 32-byte key and generates a 16-byte message
 * authentication code. The key must never be re-used for a different
 * message. Normally this is done by taking a base key and mangling it
 * using a nonce and possibly other data, as in Packet.
 */
class Poly1305
{
public:
	/**
	 * Compute a one-time authentication code
	 *
	 * @param auth Buffer to receive code -- MUST be 16 bytes in length
	 * @param data Data to authenticate
	 * @param len Length of data to authenticate in bytes
	 * @param key 32-byte one-time use key to authenticate data (must not be reused)
	 */
	static void compute(void *auth,const void *data,unsigned int len,const void *key)
		throw();
};

} // namespace ZeroTier

#endif