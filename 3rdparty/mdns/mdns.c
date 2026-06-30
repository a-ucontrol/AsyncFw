#ifndef __clang_analyzer__

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#define sleep(x) Sleep(x * 1000)
#if defined(__MINGW32__)
const IN_ADDR in4addr_any = {0};
#endif
#else
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/time.h>
#endif

#ifndef EXTEND_MDNS_TRACE
#define printf(...)
#else
#include <errno.h>
#endif

#include "../main/mdns-types.h"

extern int
query_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
			   uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data,
			   size_t size, size_t name_offset, size_t name_length, size_t record_offset,
			   size_t record_length, void* user_data);

mdns_string_t
ipv4_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in* addr,
                       size_t addrlen) {
	char host[NI_MAXHOST] = {0};
	char service[NI_MAXSERV] = {0};
	int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
						  service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);
	int len = 0;
	if (ret == 0) {
		if (addr->sin_port != 0)
			len = snprintf(buffer, capacity, "%s:%s", host, service);
		else
			len = snprintf(buffer, capacity, "%s", host);
	}
	if (len >= (int)capacity)
		len = (int)capacity - 1;
	mdns_string_t str;
	str.str = buffer;
	str.length = len;
	return str;
}

mdns_string_t
ipv6_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in6* addr,
                       size_t addrlen) {
	char host[NI_MAXHOST] = {0};
	char service[NI_MAXSERV] = {0};
	int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
						  service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);
	int len = 0;
	if (ret == 0) {
		if (addr->sin6_port != 0)
			len = snprintf(buffer, capacity, "[%s]:%s", host, service);
		else
			len = snprintf(buffer, capacity, "%s", host);
	}
	if (len >= (int)capacity)
		len = (int)capacity - 1;
	mdns_string_t str;
	str.str = buffer;
	str.length = len;
	return str;
}

mdns_string_t
ip_address_to_string(char* buffer, size_t capacity, const struct sockaddr* addr, size_t addrlen) {
	if (addr->sa_family == AF_INET6)
		return ipv6_address_to_string(buffer, capacity, (const struct sockaddr_in6*)addr, addrlen);
	return ipv4_address_to_string(buffer, capacity, (const struct sockaddr_in*)addr, addrlen);
}

// Callback handling questions incoming on service sockets
static int
service_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
                 uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data,
                 size_t size, size_t name_offset, size_t name_length, size_t record_offset,
                 size_t record_length, void* user_data) {
	(void)ttl;
	(void)name_length;
	(void)record_offset;
	(void)record_length;
	if (entry != MDNS_ENTRYTYPE_QUESTION)
		return 0;

	const char dns_sd[] = "_services._dns-sd._udp.local.";
	struct service_data_t* sd_local = (struct service_data_t*)user_data;
	if (!sd_local)
		return 0;
	const service_t* service = &(sd_local->service);

#ifdef EXTEND_MDNS_TRACE
	char addrbuffer[64];
#endif
	char namebuffer[256];
	char sendbuffer[1024];

	// mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from,
	// addrlen);

	size_t offset = name_offset;
	mdns_string_t name = mdns_string_extract(data, size, &offset, namebuffer, sizeof(namebuffer));
#if 0
  const char* record_name = 0;
  if (rtype == MDNS_RECORDTYPE_PTR)
    record_name = "PTR";
  else if (rtype == MDNS_RECORDTYPE_SRV)
    record_name = "SRV";
  else if (rtype == MDNS_RECORDTYPE_A)
    record_name = "A";
  else if (rtype == MDNS_RECORDTYPE_AAAA)
    record_name = "AAAA";
  else if (rtype == MDNS_RECORDTYPE_TXT)
    record_name = "TXT";
  else if (rtype == MDNS_RECORDTYPE_ANY)
    record_name = "ANY";
  else
    return 0;
  printf("Query %s %.*s\n", record_name, MDNS_STRING_FORMAT(name));
#endif
	if ((name.length == (sizeof(dns_sd) - 1)) &&
		(strncmp(name.str, dns_sd, sizeof(dns_sd) - 1) == 0)) {
		if ((rtype == MDNS_RECORDTYPE_PTR) || (rtype == MDNS_RECORDTYPE_ANY)) {
			// The PTR query was for the DNS-SD domain, send answer with a PTR record for the
			// service name we advertise, typically on the "<_service-name>._tcp.local." format

			// Answer PTR record reverse mapping "<_service-name>._tcp.local." to
			// "<hostname>.<_service-name>._tcp.local."
			mdns_record_t answer = {
				.name = name, .type = MDNS_RECORDTYPE_PTR, .data.ptr.name = service->service};

			// Send the answer, unicast or multicast depending on flag in query
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
			printf("  --> answer %.*s (%s)\n", MDNS_STRING_FORMAT(answer.data.ptr.name),
				   (unicast ? "unicast" : "multicast"));

			if (unicast) {
				mdns_query_answer_unicast(sock, from, addrlen, sendbuffer, sizeof(sendbuffer),
										  query_id, rtype, name.str, name.length, answer, 0, 0, 0,
										  0);
			} else {
				mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer), answer, 0, 0, 0,
											0);
			}
		}
	} else if ((name.length == service->service.length) &&
			   (strncmp(name.str, service->service.str, name.length) == 0)) {
		if ((rtype == MDNS_RECORDTYPE_PTR) || (rtype == MDNS_RECORDTYPE_ANY)) {
			// The PTR query was for our service (usually "<_service-name._tcp.local"), answer a PTR
			// record reverse mapping the queried service name to our service instance name
			// (typically on the "<hostname>.<_service-name>._tcp.local." format), and add
			// additional records containing the SRV record mapping the service instance name to our
			// qualified hostname (typically "<hostname>.local.") and port, as well as any IPv4/IPv6
			// address for the hostname as A/AAAA records, and two test TXT records

			// Answer PTR record reverse mapping "<_service-name>._tcp.local." to
			// "<hostname>.<_service-name>._tcp.local."
			mdns_record_t answer = service->record_ptr;

			mdns_record_t additional[6] = {0};
			size_t additional_count = 0;

			// SRV record mapping "<hostname>.<_service-name>._tcp.local." to
			// "<hostname>.local." with port. Set weight & priority to 0.
			additional[additional_count++] = service->record_srv;

			// A/AAAA records mapping "<hostname>.local." to IPv4/IPv6 addresses
			if (service->address_ipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a;
			if (service->address_llipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a_ll;
			if (service->address_ipv6.sin6_family == AF_INET6)
				additional[additional_count++] = service->record_aaaa;

			// Add two test TXT records for our service instance name, will be coalesced into
			// one record with both key-value pair strings by the library
			additional[additional_count++] = service->txt_record[0];
			additional[additional_count++] = service->txt_record[1];

			// Send the answer, unicast or multicast depending on flag in query
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
			printf("  --> answer %.*s (%s)\n",
				   MDNS_STRING_FORMAT(service->record_ptr.data.ptr.name),
				   (unicast ? "unicast" : "multicast"));

			if (unicast) {
				mdns_query_answer_unicast(sock, from, addrlen, sendbuffer, sizeof(sendbuffer),
										  query_id, rtype, name.str, name.length, answer, 0, 0,
										  additional, additional_count);
			} else {
				mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer), answer, 0, 0,
											additional, additional_count);
			}
		}
	} else if ((name.length == service->service_instance.length) &&
			   (strncmp(name.str, service->service_instance.str, name.length) == 0)) {
		if ((rtype == MDNS_RECORDTYPE_SRV) || (rtype == MDNS_RECORDTYPE_ANY)) {
			// The SRV query was for our service instance (usually
			// "<hostname>.<_service-name._tcp.local"), answer a SRV record mapping the service
			// instance name to our qualified hostname (typically "<hostname>.local.") and port, as
			// well as any IPv4/IPv6 address for the hostname as A/AAAA records, and two test TXT
			// records

			// Answer PTR record reverse mapping "<_service-name>._tcp.local." to
			// "<hostname>.<_service-name>._tcp.local."
			mdns_record_t answer = service->record_srv;

			mdns_record_t additional[6] = {0};
			size_t additional_count = 0;

			// A/AAAA records mapping "<hostname>.local." to IPv4/IPv6 addresses
			if (service->address_ipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a;
			if (service->address_llipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a_ll;
			if (service->address_ipv6.sin6_family == AF_INET6)
				additional[additional_count++] = service->record_aaaa;

			// Add two test TXT records for our service instance name, will be coalesced into
			// one record with both key-value pair strings by the library
			additional[additional_count++] = service->txt_record[0];
			additional[additional_count++] = service->txt_record[1];

			// Send the answer, unicast or multicast depending on flag in query
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
			printf("  --> answer %.*s port %d (%s)\n",
				   MDNS_STRING_FORMAT(service->record_srv.data.srv.name), service->port,
				   (unicast ? "unicast" : "multicast"));

			if (unicast) {
				mdns_query_answer_unicast(sock, from, addrlen, sendbuffer, sizeof(sendbuffer),
										  query_id, rtype, name.str, name.length, answer, 0, 0,
										  additional, additional_count);
			} else {
				mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer), answer, 0, 0,
											additional, additional_count);
			}
		}
	} else if ((name.length == service->hostname_qualified.length) &&
			   (strncmp(name.str, service->hostname_qualified.str, name.length) == 0)) {
		if (((rtype == MDNS_RECORDTYPE_A) || (rtype == MDNS_RECORDTYPE_ANY)) &&
			(service->address_ipv4.sin_family == AF_INET)) {
			// The A query was for our qualified hostname (typically "<hostname>.local.") and we
			// have an IPv4 address, answer with an A record mappiing the hostname to an IPv4
			// address, as well as any IPv6 address for the hostname, and two test TXT records

			// Answer A records mapping "<hostname>.local." to IPv4 address
			mdns_record_t answer = service->record_a;

			mdns_record_t additional[6] = {0};
			size_t additional_count = 0;

			if (service->address_llipv4.sin_family == AF_INET) {
				additional[additional_count++] = service->record_a_ll;
			}
			// AAAA record mapping "<hostname>.local." to IPv6 addresses
			if (service->address_ipv6.sin6_family == AF_INET6)
				additional[additional_count++] = service->record_aaaa;

			// Add two test TXT records for our service instance name, will be coalesced into
			// one record with both key-value pair strings by the library
			additional[additional_count++] = service->txt_record[0];
			additional[additional_count++] = service->txt_record[1];

			// Send the answer, unicast or multicast depending on flag in query
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
#ifdef EXTEND_MDNS_TRACE
			mdns_string_t addrstr = ip_address_to_string(
				addrbuffer, sizeof(addrbuffer), (struct sockaddr*)&service->record_a.data.a.addr,
				sizeof(service->record_a.data.a.addr));
			printf("  --> answer %.*s IPv4 %.*s (%s)\n", MDNS_STRING_FORMAT(service->record_a.name),
				   MDNS_STRING_FORMAT(addrstr), (unicast ? "unicast" : "multicast"));
#endif
			if (unicast) {
				mdns_query_answer_unicast(sock, from, addrlen, sendbuffer, sizeof(sendbuffer),
										  query_id, rtype, name.str, name.length, answer, 0, 0,
										  additional, additional_count);
			} else {
				mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer), answer, 0, 0,
											additional, additional_count);
			}
		} else if (((rtype == MDNS_RECORDTYPE_AAAA) || (rtype == MDNS_RECORDTYPE_ANY)) &&
				   (service->address_ipv6.sin6_family == AF_INET6)) {
			// The AAAA query was for our qualified hostname (typically "<hostname>.local.") and we
			// have an IPv6 address, answer with an AAAA record mappiing the hostname to an IPv6
			// address, as well as any IPv4 address for the hostname, and two test TXT records

			// Answer AAAA records mapping "<hostname>.local." to IPv6 address
			mdns_record_t answer = service->record_aaaa;

			mdns_record_t additional[6] = {0};
			size_t additional_count = 0;

			// A record mapping "<hostname>.local." to IPv4 addresses
			if (service->address_ipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a;
			if (service->address_llipv4.sin_family == AF_INET)
				additional[additional_count++] = service->record_a_ll;

			// Add two test TXT records for our service instance name, will be coalesced into
			// one record with both key-value pair strings by the library
			additional[additional_count++] = service->txt_record[0];
			additional[additional_count++] = service->txt_record[1];

			// Send the answer, unicast or multicast depending on flag in query
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
#ifdef EXTEND_MDNS_TRACE
			mdns_string_t addrstr =
				ip_address_to_string(addrbuffer, sizeof(addrbuffer),
									 (struct sockaddr*)&service->record_aaaa.data.aaaa.addr,
									 sizeof(service->record_aaaa.data.aaaa.addr));
			printf("  --> answer %.*s IPv6 %.*s (%s)\n",
				   MDNS_STRING_FORMAT(service->record_aaaa.name), MDNS_STRING_FORMAT(addrstr),
				   (unicast ? "unicast" : "multicast"));
#endif
			if (unicast) {
				mdns_query_answer_unicast(sock, from, addrlen, sendbuffer, sizeof(sendbuffer),
										  query_id, rtype, name.str, name.length, answer, 0, 0,
										  additional, additional_count);
			} else {
				mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer), answer, 0, 0,
											additional, additional_count);
			}
		}
	}
	return 0;
}
#if 0
// Callback handling questions and answers dump
static int
dump_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
              uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data,
              size_t size, size_t name_offset, size_t name_length, size_t record_offset,
              size_t record_length, void* user_data) {
  mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from, addrlen);

  size_t offset = name_offset;
  mdns_string_t name = mdns_string_extract(data, size, &offset, namebuffer, sizeof(namebuffer));

  const char* record_name = 0;
  if (rtype == MDNS_RECORDTYPE_PTR)
    record_name = "PTR";
  else if (rtype == MDNS_RECORDTYPE_SRV)
    record_name = "SRV";
  else if (rtype == MDNS_RECORDTYPE_A)
    record_name = "A";
  else if (rtype == MDNS_RECORDTYPE_AAAA)
    record_name = "AAAA";
  else if (rtype == MDNS_RECORDTYPE_TXT)
    record_name = "TXT";
  else if (rtype == MDNS_RECORDTYPE_ANY)
    record_name = "ANY";
  else
    record_name = "<UNKNOWN>";

  const char* entry_type = "Question";
  if (entry == MDNS_ENTRYTYPE_ANSWER)
    entry_type = "Answer";
  else if (entry == MDNS_ENTRYTYPE_AUTHORITY)
    entry_type = "Authority";
  else if (entry == MDNS_ENTRYTYPE_ADDITIONAL)
    entry_type = "Additional";

  printf("%.*s: %s %s %.*s rclass 0x%x ttl %u\n", MDNS_STRING_FORMAT(fromaddrstr), entry_type,
         record_name, MDNS_STRING_FORMAT(name), (unsigned int)rclass, ttl);

  return 0;
}
#endif
// Open sockets for sending one-shot multicast queries from an ephemeral port
static int
open_client_sockets(int* sockets, int max_sockets, int port, struct sockaddr_in* out_llipv4,
					struct sockaddr_in* out_ipv4, struct sockaddr_in6* out_ipv6,
					char* out_llip_str) {
	// When sending, each socket can only send to one network interface
	// Thus we need to open one socket for each interface and address family
	int num_sockets = 0;

#ifdef _WIN32

	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	ULONG address_size = 8000;
	unsigned int ret;
	unsigned int num_retries = 4;
	do {
		adapter_address = (IP_ADAPTER_ADDRESSES*)malloc(address_size);
		ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
								   adapter_address, &address_size);
		if (ret == ERROR_BUFFER_OVERFLOW) {
			free(adapter_address);
			adapter_address = 0;
			address_size *= 2;
		} else {
			break;
		}
	} while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		free(adapter_address);
		printf("Failed to get network adapter addresses\n");
		return num_sockets;
	}

	int first_llipv4 = 1;
	int first_ipv4 = 1;
	int first_ipv6 = 1;
	for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
			continue;
		if (adapter->OperStatus != IfOperStatusUp)
			continue;

		for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
			 unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
				if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
					(saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
					(saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
					(saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
					if ((ntohl(saddr->sin_addr.s_addr) & 0xFFFF0000) == 0xA9FE0000) {
						if (!first_llipv4)
							continue;
						first_llipv4 = 0;
						*out_llipv4 = *saddr;
						ipv4_address_to_string(out_llip_str, 16, saddr, sizeof(struct sockaddr_in));
						printf("LinkLocal IPv4 address: %s\n", out_llip_str);
						continue;
					}
#ifdef EXTEND_MDNS_TRACE
					int log_addr = 0;
#endif
					if (first_ipv4) {
						*out_ipv4 = *saddr;
						first_ipv4 = 0;
#ifdef EXTEND_MDNS_TRACE
						log_addr = 1;
#endif
					}
					if (num_sockets < max_sockets) {
						saddr->sin_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv4(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
#ifdef EXTEND_MDNS_TRACE
							log_addr = 1;
						} else {
							log_addr = 0;
#endif
						}
					}
#ifdef EXTEND_MDNS_TRACE
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
																	sizeof(struct sockaddr_in));
						printf("Local IPv4 address: %.*s\n", MDNS_STRING_FORMAT(addr));
					}
#endif
				}
			} else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
				// Ignore link-local addresses
				if (saddr->sin6_scope_id)
					continue;
				static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
														  0, 0, 0, 0, 0, 0, 0, 1};
				static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
																 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
				if ((unicast->DadState == NldsPreferred) &&
					memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
					memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
#ifdef EXTEND_MDNS_TRACE
					int log_addr = 0;
#endif
					if (first_ipv6) {
						*out_ipv6 = *saddr;
						first_ipv6 = 0;
#ifdef EXTEND_MDNS_TRACE
						log_addr = 1;
#endif
					}
					if (num_sockets < max_sockets) {
						saddr->sin6_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv6(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
#ifdef EXTEND_MDNS_TRACE
							log_addr = 1;
						} else {
							log_addr = 0;
#endif
						}
					}
#ifdef EXTEND_MDNS_TRACE
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
																	sizeof(struct sockaddr_in6));
						printf("Local IPv6 address: %.*s\n", MDNS_STRING_FORMAT(addr));
					}
#endif
				}
			}
		}
	}
	if (first_ipv4 && !first_llipv4) {
		if (num_sockets < max_sockets) {
			out_llipv4->sin_port = htons(port);
			int sock = mdns_socket_open_ipv4(out_llipv4);
			if (sock >= 0) {
				sockets[num_sockets++] = sock;
				printf("Local IPv4 address is empty");
			}
		}
	}

	free(adapter_address);

#else

	struct ifaddrs* ifaddr = 0;
	struct ifaddrs* ifa = 0;

	if (getifaddrs(&ifaddr) < 0) {
		printf("Unable to get interface addresses\n");
	}

	int first_llipv4 = 1;
	int first_ipv4 = 1;
	int first_ipv6 = 1;
	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;
		if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_MULTICAST))
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) || (ifa->ifa_flags & IFF_POINTOPOINT))
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in* saddr = (struct sockaddr_in*)ifa->ifa_addr;
			if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
				if ((ntohl(saddr->sin_addr.s_addr) & 0xFFFF0000) == 0xA9FE0000) {
					if (!first_llipv4)
						continue;
					first_llipv4 = 0;
					*out_llipv4 = *saddr;
					ipv4_address_to_string(out_llip_str, 16, saddr, sizeof(struct sockaddr_in));
					printf("LinkLocal IPv4 address: %s\n", out_llip_str);
					continue;
				}
#ifdef EXTEND_MDNS_TRACE
				int log_addr = 0;
#endif
				if (first_ipv4) {
					*out_ipv4 = *saddr;
					first_ipv4 = 0;
#ifdef EXTEND_MDNS_TRACE
					log_addr = 1;
#endif
				}
				if (num_sockets < max_sockets) {
					saddr->sin_port = htons(port);
					int sock = mdns_socket_open_ipv4(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
#ifdef EXTEND_MDNS_TRACE
						log_addr = 1;
					} else {
						log_addr = 0;
#endif
					}
				}
#ifdef EXTEND_MDNS_TRACE
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
																sizeof(struct sockaddr_in));
					printf("Local IPv4 address: %.*s\n", MDNS_STRING_FORMAT(addr));
				}
#endif
			} else {
				printf("INADDR_LOOPBACK\n");
			}
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6* saddr = (struct sockaddr_in6*)ifa->ifa_addr;
			// Ignore link-local addresses
			if (saddr->sin6_scope_id)
				continue;
			static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
													  0, 0, 0, 0, 0, 0, 0, 1};
			static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
															 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
			if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
				memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
#ifdef EXTEND_MDNS_TRACE
				int log_addr = 0;
#endif
				if (first_ipv6) {
					*out_ipv6 = *saddr;
					first_ipv6 = 0;
#ifdef EXTEND_MDNS_TRACE
					log_addr = 1;
#endif
				}
				if (num_sockets < max_sockets) {
					saddr->sin6_port = htons(port);
					int sock = mdns_socket_open_ipv6(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
#ifdef EXTEND_MDNS_TRACE
						log_addr = 1;
					} else {
						log_addr = 0;
#endif
					}
				}
#ifdef EXTEND_MDNS_TRACE
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
																sizeof(struct sockaddr_in6));
					printf("Local IPv6 address: %.*s\n", MDNS_STRING_FORMAT(addr));
				}
#endif
			}
		}
	}
	if (first_ipv4 && !first_llipv4) {
		if (num_sockets < max_sockets) {
			out_llipv4->sin_port = htons(port);
			int sock = mdns_socket_open_ipv4(out_llipv4);
			if (sock >= 0) {
				sockets[num_sockets++] = sock;
				printf("Local IPv4 address is empty");
			}
		}
	}

	freeifaddrs(ifaddr);

#endif

	return num_sockets;
}

// Open sockets to listen to incoming mDNS queries on port 5353
static int
open_service_sockets(int* sockets, int max_sockets, service_data_t* sd) {
	// When recieving, each socket can recieve data from all network interfaces
	// Thus we only need to open one socket for each address family
	int num_sockets = 0;

	// Call the client socket function to enumerate and get local addresses,
	// but not open the actual sockets
	num_sockets = open_client_sockets(sockets, max_sockets, 5353, &(sd->service_address_llipv4),
									  &(sd->service_address_ipv4), &(sd->service_address_ipv6),
									  sd->txt_llip_buffer);

	if (num_sockets < max_sockets) {
		struct sockaddr_in sock_addr;
		memset(&sock_addr, 0, sizeof(struct sockaddr_in));
		sock_addr.sin_family = AF_INET;
#ifdef _WIN32
		sock_addr.sin_addr = in4addr_any;
#else
		sock_addr.sin_addr.s_addr = INADDR_ANY;
#endif
		sock_addr.sin_port = htons(MDNS_PORT);
#ifdef __APPLE__
		sock_addr.sin_len = sizeof(struct sockaddr_in);
#endif
		int sock = mdns_socket_open_ipv4(&sock_addr);
		if (sock >= 0)
			sockets[num_sockets++] = sock;
	}

	if (num_sockets < max_sockets) {
		struct sockaddr_in6 sock_addr;
		memset(&sock_addr, 0, sizeof(struct sockaddr_in6));
		sock_addr.sin6_family = AF_INET6;
		sock_addr.sin6_addr = in6addr_any;
		sock_addr.sin6_port = htons(MDNS_PORT);
#ifdef __APPLE__
		sock_addr.sin6_len = sizeof(struct sockaddr_in6);
#endif
		int sock = mdns_socket_open_ipv6(&sock_addr);
		if (sock >= 0)
			sockets[num_sockets++] = sock;
	}

	return num_sockets;
}
#if 0
// Send a DNS-SD query
int
send_dns_sd(void) {
  int sockets[32];
  int num_sockets = open_client_sockets(sockets, sizeof(sockets) / sizeof(sockets[0]), MDNS_REQUEST_PORT);
  if (num_sockets <= 0) {
    printf("Failed to open any client sockets\n");
    return -1;
  }
  printf("Opened %d socket%s for DNS-SD\n", num_sockets, num_sockets > 1 ? "s" : "");

  printf("Sending DNS-SD discovery\n");
  for (int isock = 0; isock < num_sockets; ++isock) {
    if (mdns_discovery_send(sockets[isock]))
      printf("Failed to send DNS-DS discovery: %s\n", strerror(errno));
  }

  size_t capacity = 2048;
  void* buffer = malloc(capacity);
  void* user_data = 0;
  size_t records;

  // This is a simple implementation that loops for 5 seconds or as long as we get replies
  int res;
  printf("Reading DNS-SD replies\n");
  do {
    struct timeval timeout;
    timeout.tv_sec = dns_sd_timeout;
    timeout.tv_usec = 0;

    int nfds = 0;
    fd_set readfs;
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds)
        nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    records = 0;
    res = select(nfds, &readfs, 0, 0, &timeout);
    if (res > 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          records += mdns_discovery_recv(sockets[isock], buffer, capacity, query_callback,
                                         user_data);
        }
      }
    }
  } while (res > 0);

  free(buffer);

  for (int isock = 0; isock < num_sockets; ++isock)
    mdns_socket_close(sockets[isock]);
  printf("Closed socket%s\n", num_sockets ? "s" : "");

  return 0;
}
#endif

// Send a mDNS query
int
start_mdns_querier(void* qd_void_ptr) {
	struct querier_data_t* qd = (struct querier_data_t*)qd_void_ptr;
	if (!qd)
		return -1;

	struct sockaddr_in dummy_llipv4;
	struct sockaddr_in dummy_ipv4;
	struct sockaddr_in6 dummy_ipv6;
	char dummy_llip_str[16] = {0};

	qd->num_sockets = open_client_sockets(qd->sockets, 256, qd->mode == 2 ? 0 : 5353, &dummy_llipv4,
										  &dummy_ipv4, &dummy_ipv6, dummy_llip_str);

	if (qd->num_sockets <= 0) {
		printf("Failed to open any client sockets\n");
		return -1;
	}
	printf("Opened %d socket%s for mDNS query\n", qd->num_sockets, qd->num_sockets ? "s" : "");

	qd->capacity = 2048;
	qd->buffer = malloc(qd->capacity);
	return 0;
}
static int
mdns_multiquery_send_(int sock, const mdns_query_t* query, size_t count, void* buffer,
					  size_t capacity, uint16_t query_id, uint8_t unicast) {
	if (!count || (capacity < (sizeof(struct mdns_header_t) + (6 * count))))
		return -1;

	// Ask for a unicast response since it's a one-shot query
	uint16_t rclass = MDNS_CLASS_IN;
	if (unicast)
		rclass |= MDNS_UNICAST_RESPONSE;

	struct mdns_header_t* header = (struct mdns_header_t*)buffer;
	// Query ID
	header->query_id = htons((unsigned short)query_id);
	// Flags
	header->flags = 0;
	// Questions
	header->questions = htons((unsigned short)count);
	// No answer, authority or additional RRs
	header->answer_rrs = 0;
	header->authority_rrs = 0;
	header->additional_rrs = 0;
	// Fill in questions
	void* data = MDNS_POINTER_OFFSET(buffer, sizeof(struct mdns_header_t));
	for (size_t iq = 0; iq < count; ++iq) {
		// Name string
		data = mdns_string_make(buffer, capacity, data, query[iq].name, query[iq].length, 0);
		if (!data)
			return -1;
		size_t remain = capacity - MDNS_POINTER_DIFF(data, buffer);
		if (remain < 4)
			return -1;
		// Record type
		data = mdns_htons(data, query[iq].type);
		//! Optional unicast response based on local port, class IN
		data = mdns_htons(data, rclass);
	}

	size_t tosend = MDNS_POINTER_DIFF(data, buffer);
	if (mdns_multicast_send(sock, buffer, (size_t)tosend))
		return -1;
	return query_id;
}
int
send_mdns_query(void* qd_void_ptr, mdns_query_t* query, size_t count) {
#if 0
  printf("Sending mDNS query");
  for (size_t iq = 0; iq < count; ++iq) {
    const char* record_name = "PTR";
    if (query[iq].type == MDNS_RECORDTYPE_SRV)
      record_name = "SRV";
    else if (query[iq].type == MDNS_RECORDTYPE_A)
      record_name = "A";
    else if (query[iq].type == MDNS_RECORDTYPE_AAAA)
      record_name = "AAAA";
    else if (query[iq].type == MDNS_RECORDTYPE_TXT)
      record_name = "TXT";
    else if (query[iq].type == MDNS_RECORDTYPE_ANY)
      record_name = "ANY";
    else
      query[iq].type = MDNS_RECORDTYPE_PTR;
    printf(" : %s %s", query[iq].name, record_name);
  }
  printf("\n");
#endif
	struct querier_data_t* qd = (struct querier_data_t*)qd_void_ptr;
	if (!qd)
		return -1;

	int ret = 0;
	for (int isock = 0; isock < qd->num_sockets; ++isock) {
		int* query_ids = qd->query_id;
		int* sockets = qd->sockets;

		query_ids[isock] = mdns_multiquery_send_(sockets[isock], query, count, qd->buffer,
												 qd->capacity, 0, qd->mode);
		if (query_ids[isock] < 0) {
			printf("Failed to send mDNS query: %s\n", strerror(errno));
		} else
			ret++;
	}
	return ret;
}
void
mdns_querier_event(int fd, void* qd_void_ptr, void* ctx_void_ptr) {
	struct querier_data_t* qd = (struct querier_data_t*)qd_void_ptr;
	if (!qd)
		return;

	int query_id = -1;
	for (int isock = 0; isock < qd->num_sockets; ++isock) {
		if (qd->sockets[isock] == fd)
			query_id = qd->query_id[isock];
	}

	// Передаем Си-библиотеке контекст, который пришел из C++
	mdns_query_recv(fd, qd->buffer, qd->capacity, query_callback, ctx_void_ptr, query_id);
}
void
stop_mdns_querier(void* qd_void_ptr) {
	struct querier_data_t* qd = (struct querier_data_t*)qd_void_ptr;
	if (!qd || !qd->buffer)
		return;

	free(qd->buffer);
	qd->buffer = NULL;

	int* sockets = qd->sockets;
	for (int isock = 0; isock < qd->num_sockets; ++isock) {
		mdns_socket_close(sockets[isock]);
	}
	printf("Closed socket%s\n", qd->num_sockets ? "s" : "");
}
// Provide a mDNS service, answering incoming DNS-SD and mDNS queries
int
start_mdns_service(const char* _hostname, const char* _service_name, int service_port,
				   void* sd_void_ptr) {
	service_data_t* sd = (service_data_t*)sd_void_ptr;
	if (!sd)
		return -1;

	char sendbuffer[1024];

	memset(&(sd->service_address_ipv4), 0, sizeof(struct sockaddr_in));
	memset(&(sd->service_address_ipv6), 0, sizeof(struct sockaddr_in6));

	/* Передаем адрес массива sockets, который теперь физически лежит в C++ */
	sd->num_sockets = open_service_sockets(sd->sockets, 256, sd);
	if (sd->num_sockets <= 0) {
		printf("Failed to open any service sockets\n");
		return -1;
	}
	printf("Opened %d socket%s for mDNS service\n", sd->num_sockets, sd->num_sockets ? "s" : "");

	size_t service_name_length = strlen(_service_name);
	if (!service_name_length) {
		printf("Invalid service name\n");
		return -1;
	}

	sd->service_name_buffer = (char*)malloc(service_name_length + 2);
	memcpy(sd->service_name_buffer, _service_name, service_name_length);
	if (sd->service_name_buffer[service_name_length - 1] != '.')
		sd->service_name_buffer[service_name_length++] = '.';
	sd->service_name_buffer[service_name_length] = 0;

	snprintf(sd->hostname_buffer, sizeof(sd->hostname_buffer) - 1, "%s", _hostname);

	printf("Service mDNS: %s:%d\n", sd->service_name_buffer, service_port);
	printf("Hostname: %s\n", sd->hostname_buffer);

	sd->capacity = 2048;
	sd->buffer = malloc(sd->capacity);

	mdns_string_t service_string =
		(mdns_string_t){sd->service_name_buffer, strlen(sd->service_name_buffer)};
	mdns_string_t hostname_string =
		(mdns_string_t){sd->hostname_buffer, strlen(sd->hostname_buffer)};

	snprintf(sd->service_instance_buffer, sizeof(sd->service_instance_buffer) - 1, "%.*s.%.*s",
			 MDNS_STRING_FORMAT(hostname_string), MDNS_STRING_FORMAT(service_string));
	mdns_string_t service_instance_string =
		(mdns_string_t){sd->service_instance_buffer, strlen(sd->service_instance_buffer)};

	snprintf(sd->qualified_hostname_buffer, sizeof(sd->qualified_hostname_buffer) - 1,
			 "%.*s.local.", MDNS_STRING_FORMAT(hostname_string));
	mdns_string_t hostname_qualified_string =
		(mdns_string_t){sd->qualified_hostname_buffer, strlen(sd->qualified_hostname_buffer)};

	sd->service.service = service_string;
	sd->service.hostname = hostname_string;
	sd->service.service_instance = service_instance_string;
	sd->service.hostname_qualified = hostname_qualified_string;
	sd->service.address_llipv4 = sd->service_address_llipv4;
	sd->service.address_ipv4 = sd->service_address_ipv4;
	sd->service.address_ipv6 = sd->service_address_ipv6;
	sd->service.port = service_port;

	sd->service.record_ptr = (mdns_record_t){.name = sd->service.service,
											 .type = MDNS_RECORDTYPE_PTR,
											 .data.ptr.name = sd->service.service_instance,
											 .rclass = 0,
											 .ttl = 0};

	sd->service.record_srv = (mdns_record_t){.name = sd->service.service_instance,
											 .type = MDNS_RECORDTYPE_SRV,
											 .data.srv.name = sd->service.hostname_qualified,
											 .data.srv.port = sd->service.port,
											 .data.srv.priority = 0,
											 .data.srv.weight = 0,
											 .rclass = 0,
											 .ttl = 0};

	sd->service.record_a = (mdns_record_t){.name = sd->service.hostname_qualified,
										   .type = MDNS_RECORDTYPE_A,
										   .data.a.addr = sd->service.address_ipv4,
										   .rclass = 0,
										   .ttl = 0};

	sd->service.record_a_ll = (mdns_record_t){.name = sd->service.hostname_qualified,
											  .type = MDNS_RECORDTYPE_A,
											  .data.a.addr = sd->service_address_llipv4,
											  .rclass = 0,
											  .ttl = 0};

	sd->service.record_aaaa = (mdns_record_t){.name = sd->service.hostname_qualified,
											  .type = MDNS_RECORDTYPE_AAAA,
											  .data.aaaa.addr = sd->service.address_ipv6,
											  .rclass = 0,
											  .ttl = 0};

	sd->service.txt_record[0] =
		(mdns_record_t){.name = sd->service.service_instance,
						.type = MDNS_RECORDTYPE_TXT,
						.data.txt.key = {MDNS_STRING_CONST("llip")},
						.data.txt.value = {sd->txt_llip_buffer, strlen(sd->txt_llip_buffer)},
						.rclass = 0,
						.ttl = 0};

	sd->service.txt_record[1] =
		(mdns_record_t){.name = sd->service.service_instance,
						.type = MDNS_RECORDTYPE_TXT,
						.data.txt.key = {MDNS_STRING_CONST("misc")},
						.data.txt.value = {sd->txt_misc_buffer, strlen(sd->txt_misc_buffer)},
						.rclass = 0,
						.ttl = 0};

	{
		printf("Sending announce\n");
		mdns_record_t additional[6] = {0};
		size_t additional_count = 0;
		additional[additional_count++] = sd->service.record_srv;
		if (sd->service.address_ipv4.sin_family == AF_INET)
			additional[additional_count++] = sd->service.record_a;
		if (sd->service_address_llipv4.sin_family == AF_INET)
			additional[additional_count++] = sd->service.record_a_ll;
		if (sd->service.address_ipv6.sin6_family == AF_INET6)
			additional[additional_count++] = sd->service.record_aaaa;
		additional[additional_count++] = sd->service.txt_record[0];
		additional[additional_count++] = sd->service.txt_record[1];

		int* sockets = sd->sockets;
		for (int isock = 0; isock < sd->num_sockets; ++isock) {
			mdns_query_answer_multicast(sockets[isock], sendbuffer, sizeof(sendbuffer),
										sd->service.record_ptr, 0, 0, additional, additional_count);
		}
	}
	return 0;
}
#define MDNS_HEADER_FLAG_RESPONSE 0x8000U
static inline size_t
mdns_socket_listen_(int sock, void* buffer, size_t capacity, mdns_record_callback_fn callback,
					void* user_data) {
	struct sockaddr_in6 addr;
	struct sockaddr* saddr = (struct sockaddr*)&addr;
	socklen_t addrlen = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
	saddr->sa_len = sizeof(addr);
#endif

	uint16_t peek_header[2];
	mdns_ssize_t peek_ret =
		recvfrom(sock, (char*)peek_header, sizeof(peek_header), MSG_PEEK, saddr, &addrlen);
	if (peek_ret >= 4) {
		uint16_t flags = mdns_ntohs(&peek_header[1]);
		if (flags & MDNS_HEADER_FLAG_RESPONSE)
			return (size_t)-2;  // Возвращаем специальный маркер для C++ слоя
	} else
		return -1;

	mdns_ssize_t ret = recvfrom(sock, (char*)buffer, (mdns_size_t)capacity, 0, saddr, &addrlen);

	size_t data_size = (size_t)ret;
	const uint16_t* data = (const uint16_t*)buffer;

	uint16_t query_id = mdns_ntohs(data++);
	uint16_t flags = mdns_ntohs(data++);
	uint16_t questions = mdns_ntohs(data++);
	uint16_t answer_rrs = mdns_ntohs(data++);
	uint16_t authority_rrs = mdns_ntohs(data++);
	uint16_t additional_rrs = mdns_ntohs(data++);

	size_t records;
	size_t total_records = 0;
	for (int iquestion = 0; iquestion < questions; ++iquestion) {
		size_t question_offset = MDNS_POINTER_DIFF(data, buffer);
		size_t offset = question_offset;
		size_t verify_offset = 12;
		int dns_sd = 0;
		if (mdns_string_equal(buffer, data_size, &offset, mdns_services_query,
							  sizeof(mdns_services_query), &verify_offset)) {
			dns_sd = 1;
		} else if (!mdns_string_skip(buffer, data_size, &offset)) {
			break;
		}
		size_t length = offset - question_offset;
		data = (const uint16_t*)MDNS_POINTER_OFFSET_CONST(buffer, offset);

		uint16_t rtype = mdns_ntohs(data++);
		uint16_t rclass = mdns_ntohs(data++);
		uint16_t class_without_flushbit = rclass & ~MDNS_CACHE_FLUSH;

		// Make sure we get a question of class IN or ANY
		if (!((class_without_flushbit == MDNS_CLASS_IN) ||
			  (class_without_flushbit == MDNS_CLASS_ANY))) {
			break;
		}

		if (dns_sd && flags)
			continue;

		++total_records;
		if (callback && callback(sock, saddr, addrlen, MDNS_ENTRYTYPE_QUESTION, query_id, rtype,
								 rclass, 0, buffer, data_size, question_offset, length,
								 question_offset, length, user_data))
			return total_records;
	}

	size_t offset = MDNS_POINTER_DIFF(data, buffer);
	records = mdns_records_parse(sock, saddr, addrlen, buffer, data_size, &offset,
								 MDNS_ENTRYTYPE_ANSWER, query_id, answer_rrs, callback, user_data);
	total_records += records;
	if (records != answer_rrs)
		return total_records;

	records =
		mdns_records_parse(sock, saddr, addrlen, buffer, data_size, &offset,
						   MDNS_ENTRYTYPE_AUTHORITY, query_id, authority_rrs, callback, user_data);
	total_records += records;
	if (records != authority_rrs)
		return total_records;

	records = mdns_records_parse(sock, saddr, addrlen, buffer, data_size, &offset,
								 MDNS_ENTRYTYPE_ADDITIONAL, query_id, additional_rrs, callback,
								 user_data);

	return total_records;
}
int
mdns_service_event(int fd, void* sd_void_ptr) {
	service_data_t* sd = (service_data_t*)sd_void_ptr;
	if (!sd)
		return -1;
	return mdns_socket_listen_(fd, sd->buffer, sd->capacity, service_callback, sd);
}

void
stop_mdns_service(void* sd_void_ptr, int send_goodbye) {
	service_data_t* sd = (service_data_t*)sd_void_ptr;
	if (!sd)
		return;

	if (send_goodbye) {
		printf("Sending goodbye\n");
		mdns_record_t additional[6] = {0};
		size_t additional_count = 0;
		additional[additional_count++] = sd->service.record_srv;
		if (sd->service.address_ipv4.sin_family == AF_INET)
			additional[additional_count++] = sd->service.record_a;
		if (sd->service.address_llipv4.sin_family == AF_INET)
			additional[additional_count++] = sd->service.record_a_ll;
		if (sd->service.address_ipv6.sin6_family == AF_INET6)
			additional[additional_count++] = sd->service.record_aaaa;
		additional[additional_count++] = sd->service.txt_record[0];
		additional[additional_count++] = sd->service.txt_record[1];

		int* sockets = sd->sockets;
		for (int isock = 0; isock < sd->num_sockets; ++isock) {
			mdns_goodbye_multicast(sockets[isock], sd->buffer, sd->capacity, sd->service.record_ptr,
								   0, 0, additional, additional_count);
		}
	}

	if (sd->buffer) {
		free(sd->buffer);
		sd->buffer = NULL;
	}
	if (sd->service_name_buffer) {
		free(sd->service_name_buffer);
		sd->service_name_buffer = NULL;
	}

	int* sockets = sd->sockets;
	for (int isock = 0; isock < sd->num_sockets; ++isock) {
		mdns_socket_close(sockets[isock]);
	}
	printf("Closed socket%s\n", sd->num_sockets ? "s" : "");
}
#endif
