/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <netinet/in.h>
#endif

#include "3rdparty/mdns/mdns.h"

typedef struct service_t {
  mdns_string_t service;
  mdns_string_t hostname;
  mdns_string_t service_instance;
  mdns_string_t hostname_qualified;
  struct sockaddr_in address_ipv4;
  struct sockaddr_in6 address_ipv6;
  int port;
  mdns_record_t record_ptr;
  mdns_record_t record_srv;
  mdns_record_t record_a;
  mdns_record_t record_aaaa;
  mdns_record_t txt_record[2];
} service_t;

typedef struct service_data_t {
  int sockets[256];
  int num_sockets;
  void *buffer;
  char *service_name_buffer;
  size_t capacity;
  service_t service;
  char hostname_buffer[256];
  char service_instance_buffer[256];
  char qualified_hostname_buffer[256];
  char txt_misc_buffer[128];
  char txt_llip_buffer[16];
  struct sockaddr_in service_address_llipv4;
  struct sockaddr_in service_address_ipv4;
  struct sockaddr_in6 service_address_ipv6;
} service_data_t;

typedef struct querier_data_t {
  int sockets[256];
  int query_id[256];
  int num_sockets;
  size_t capacity;
  void *buffer;
} querier_data_t;

typedef struct QueryContext {
  void *instance;
  void *resultHost;
} QueryContext;
