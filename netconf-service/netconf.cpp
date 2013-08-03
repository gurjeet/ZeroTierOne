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

/*
 * This is the netconf service. It's currently used only by netconf nodes that
 * are run by ZeroTier itself. There is nothing to prevent you from running
 * your own if you wanted to create your own networks outside our system.
 *
 * That being said, we'd like to charge for private networks to support
 * ZeroTier One and future development efforts. So while this software is
 * open source and we're not going to stop you from sidestepping this, we
 * do ask -- honor system here -- that you pay for private networks if you
 * are going to use them for any commercial purpose such as a business VPN
 * alternative.
 *
 * This will at the moment only build on Linux and requires the mysql++
 * library, which is available here:
 *
 * http://tangentsoft.net/mysql++/
 *
 * (Packages are available for CentOS via EPEL and for any Debian distro.)
 *
 * This program must be built and installed in the services.d subfolder of
 * the ZeroTier One home folder of the node designated to act as a master
 * for networks. Doing so will enable the NETWORK_CONFIG_REQUEST protocol
 * verb.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <algorithm>

#include <mysql++/mysql++.h>

#include "../node/Dictionary.hpp"
#include "../node/Identity.hpp"
#include "../node/Utils.hpp"
#include "../node/Mutex.hpp"

using namespace ZeroTier;
using namespace mysqlpp;

static Mutex stdoutWriteLock;
static Connection *dbCon = (Connection *)0;
static char mysqlHost[64],mysqlPort[64],mysqlDatabase[64],mysqlUser[64],mysqlPassword[64];

static void connectOrReconnect()
{
	for(;;) {
		delete dbCon;
		try {
			dbCon = new Connection(mysqlDatabase,mysqlHost,mysqlUser,mysqlPassword,(unsigned int)strtol(mysqlPort,(char **)0,10));
			if (dbCon->connected()) {
				fprintf(stderr,"(re?)-connected to mysql server successfully\n");
				break;
			} else {
				fprintf(stderr,"unable to connect to database server (connection closed), trying again in 1s...\n");
				usleep(1000000);
			}
		} catch (std::exception &exc) {
			fprintf(stderr,"unable to connect to database server (%s), trying again in 1s...\n",exc.what());
			usleep(1000000);
		} catch ( ... ) {
			fprintf(stderr,"unable to connect to database server (unknown exception), trying again in 1s...\n");
			usleep(1000000);
		}
	}
}

int main(int argc,char **argv)
{
	{
		char *ee = getenv("ZT_NETCONF_MYSQL_HOST");
		if (!ee) {
			fprintf(stderr,"missing environment variable: ZT_NETCONF_MYSQL_HOST\n");
			return -1;
		}
		strcpy(mysqlHost,ee);
		ee = getenv("ZT_NETCONF_MYSQL_PORT");
		if (!ee)
			strcpy(mysqlPort,"3306");
		else strcpy(mysqlPort,ee);
		ee = getenv("ZT_NETCONF_MYSQL_DATABASE");
		if (!ee) {
			fprintf(stderr,"missing environment variable: ZT_NETCONF_MYSQL_DATABASE\n");
			return -1;
		}
		strcpy(mysqlDatabase,ee);
		ee = getenv("ZT_NETCONF_MYSQL_USER");
		if (!ee) {
			fprintf(stderr,"missing environment variable: ZT_NETCONF_MYSQL_USER\n");
			return -1;
		}
		strcpy(mysqlUser,ee);
		ee = getenv("ZT_NETCONF_MYSQL_PASSWORD");
		if (!ee) {
			fprintf(stderr,"missing environment variable: ZT_NETCONF_MYSQL_PASSWORD\n");
			return -1;
		}
		strcpy(mysqlPassword,ee);
	}

	char buf[131072];
	std::string dictBuf;

	connectOrReconnect();
	for(;;) {
		for(int l=0;l<4;) {
			int n = (int)read(STDIN_FILENO,buf + l,4 - l);
			if (n < 0) {
				fprintf(stderr,"error reading frame size from stdin: %s\n",strerror(errno));
				return -1;
			}
			l += n;
		}
		unsigned int fsize = (unsigned int)ntohl(*((const uint32_t *)buf));

		while (dictBuf.length() < fsize) {
			int n = (int)read(STDIN_FILENO,buf,std::min((int)sizeof(buf),(int)(fsize - dictBuf.length())));
			if (n < 0) {
				fprintf(stderr,"error reading frame from stdin: %s\n",strerror(errno));
				return -1;
			}
			for(int i=0;i<n;++i)
				dictBuf.push_back(buf[i]);
		}
		Dictionary request(dictBuf);
		dictBuf = "";

		if (!dbCon->connected())
			connectOrReconnect();

		try {
			const std::string &reqType = request.get("type");
			if (reqType == "netconf-request") { // NETWORK_CONFIG_REQUEST packet
				Identity peerIdentity(request.get("peerId"));
				uint64_t nwid = strtoull(request.get("nwid").c_str(),(char **)0,16);
				Dictionary meta;
				if (request.contains("meta"))
					meta.fromString(request.get("meta"));

				// Do quick signature check / sanity check
				if (!peerIdentity.locallyValidate(false)) {
					fprintf(stderr,"identity failed signature check: %s\n",peerIdentity.toString(false).c_str());
					continue;
				}

				// Save identity if unknown
				{
					Query q = dbCon->query();
					q << "SELECT identity,identityValidated FROM Node WHERE id = " << peerIdentity.address().toInt();
					StoreQueryResult rs = q.store();
					if (rs.num_rows() > 0) {
						if (rs[0]["identity"] != peerIdentity.toString(false)) {
							// TODO: handle collisions...
							continue;
						} else if ((int)rs[0]["identityValidated"] == 0) {
							// TODO: launch background validation
						}
					} else {
						q = dbCon->query();
						q << "INSERT INTO Node (id,creationTime,lastSeen,identity) VALUES (" << peerIdentity.address().toInt() << "," << Utils::now() << ",0," << quote << peerIdentity.toString(false) << ")";
						if (!q.exec()) {
							fprintf(stderr,"error inserting Node row for peer %s, aborting netconf request\n",peerIdentity.address().toString().c_str());
							continue;
						}
						// TODO: launch background validation
					}
				}

				// Update lastSeen
				{
					Query q = dbCon->query();
					q << "UPDATE Node SET lastSeen = " << Utils::now() << " WHERE id = " << peerIdentity.address().toInt();
					q.exec();
				}

				bool isOpen = false;
				{
					Query q = dbCon->query();
					q << "SELECT isOpen FROM Network WHERE id = " << nwid;
					StoreQueryResult rs = q.store();
					if (rs.num_rows() > 0)
						isOpen = ((int)rs[0]["isOpen"] > 0);
					else {
						Dictionary response;
						response["peer"] = peerIdentity.address().toString();
						response["nwid"] = request.get("nwid");
						response["type"] = "netconf-response";
						response["requestId"] = request.get("requestId");
						response["error"] = "NOT_FOUND";
						std::string respm = response.toString();
						uint32_t respml = (uint32_t)htonl((uint32_t)respm.length());

						stdoutWriteLock.lock();
						write(STDOUT_FILENO,&respml,4);
						write(STDOUT_FILENO,respm.data(),respm.length());
						stdoutWriteLock.unlock();
						continue;
					}
				}

				Dictionary netconf;

				netconf["peer"] = peerIdentity.address().toString();
				sprintf(buf,"%.16llx",(unsigned long long)nwid);
				netconf["nwid"] = buf;
				netconf["isOpen"] = (isOpen ? "1" : "0");

				if (!isOpen) {
					// TODO: handle closed networks, look up private membership,
					// generate signed cert.
				}

				std::string ipv4Static,ipv6Static;

				{
					// Check for IPv4 static assignments
					Query q = dbCon->query();
					q << "SELECT INET_NTOA(ip) AS ip,netmaskBits FROM IPv4Static WHERE Node_id = " << peerIdentity.address().toInt() << " AND Network_id = " << nwid;
					StoreQueryResult rs = q.store();
					if (rs.num_rows() > 0) {
						for(int i=0;i<rs.num_rows();++i) {
							if (ipv4Static.length())
								ipv4Static.push_back(',');
							ipv4Static.append(rs[i]["ip"].c_str());
							ipv4Static.push_back('/');
							ipv4Static.append(rs[i]["netmaskBits"].c_str());
						}
					}

					// Try to auto-assign if there's any auto-assign networks with space
					// available.
					if (!ipv4Static.length()) {
						unsigned char addressBytes[5];
						peerIdentity.address().copyTo(addressBytes,5);

						q = dbCon->query();
						q << "SELECT ipNet,netmaskBits FROM IPv4AutoAssign WHERE Network_id = " << nwid;
						rs = q.store();
						if (rs.num_rows() > 0) {
							for(int aaRow=0;aaRow<rs.num_rows();++aaRow) {
								uint32_t ipNet = (uint32_t)((unsigned long)rs[aaRow]["ipNet"]);
								unsigned int netmaskBits = (unsigned int)rs[aaRow]["netmaskBits"];

								uint32_t tryIp = (((uint32_t)addressBytes[1]) << 24) |
								                 (((uint32_t)addressBytes[2]) << 16) |
								                 (((uint32_t)addressBytes[3]) << 8) |
								                 ((((uint32_t)addressBytes[4]) % 254) + 1);
								tryIp &= (0xffffffff >> netmaskBits);
								tryIp |= ipNet;

								for(int k=0;k<100000;++k) {
									Query q2 = dbCon->query();
									q2 << "INSERT INTO IPv4Static (Network_id,Node_id,ip,netmaskBits) VALUES (" << nwid << "," << peerIdentity.address().toInt() << "," << tryIp << "," << netmaskBits << ")";
									if (q2.exec()) {
										sprintf(buf,"%u.%u.%u.%u",(unsigned int)((tryIp >> 24) & 0xff),(unsigned int)((tryIp >> 16) & 0xff),(unsigned int)((tryIp >> 8) & 0xff),(unsigned int)(tryIp & 0xff));
										if (ipv4Static.length())
											ipv4Static.push_back(',');
										ipv4Static.append(buf);
										ipv4Static.push_back('/');
										sprintf(buf,"%u",netmaskBits);
										ipv4Static.append(buf);
										break;
									} else { // insert will fail if IP is in use due to uniqueness constraints in DB
										++tryIp;
										if ((tryIp & 0xff) == 0)
											tryIp |= 1;
										tryIp &= (0xffffffff >> netmaskBits);
										tryIp |= ipNet;
									}
								}

								if (ipv4Static.length())
									break;
							}
						}
					}
				}

				if (ipv4Static.length())
					netconf["ipv4Static"] = ipv4Static;
				if (ipv6Static.length())
					netconf["ipv6Static"] = ipv6Static;

				{
					Dictionary response;
					response["peer"] = peerIdentity.address().toString();
					response["nwid"] = request.get("nwid");
					response["type"] = "netconf-response";
					response["requestId"] = request.get("requestId");
					response["netconf"] = netconf.toString();
					std::string respm = response.toString();
					uint32_t respml = (uint32_t)htonl((uint32_t)respm.length());

					stdoutWriteLock.lock();
					write(STDOUT_FILENO,&respml,4);
					write(STDOUT_FILENO,respm.data(),respm.length());
					stdoutWriteLock.unlock();
				}
			}
		} catch (std::exception &exc) {
			fprintf(stderr,"unexpected exception handling message: %s\n",exc.what());
		} catch ( ... ) {
			fprintf(stderr,"unexpected exception handling message: unknown exception\n");
		}
	}
}