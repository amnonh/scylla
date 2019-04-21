/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Modified by ScyllaDB
 * Copyright (C) 2016 ScyllaDB
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <seastar/net/inet_address.hh>
#include <seastar/core/print.hh>
#include <seastar/core/future.hh>
#include "inet_address.hh"
#include <sys/types.h>
#include <ifaddrs.h>

using namespace seastar;

gms::inet_address::inet_address(const net::inet_address& in)
    : inet_address(in.as_ipv4_address())
{}

future<gms::inet_address> gms::inet_address::lookup(sstring name) {
    return seastar::net::inet_address::find(name, seastar::net::inet_address::family::INET).then([](seastar::net::inet_address&& a) {
        return make_ready_future<gms::inet_address>(a);
    });
}

future<gms::inet_address> gms::inet_address::iflookup(sstring interface) {
    uint32_t from_ip = 0;
    uint32_t to_ip = 0;
    auto slash_position = interface.find('/');

    if (slash_position != sstring::npos) {
        // for the ip/mask notation we are going to calculate the ip range
        // if the interface ip address is in that range, we will return it
        gms::inet_address net_address(interface.substr(0, slash_position));
        uint32_t ip = net_address.addr().ip;
        uint32_t mask_len = std::stoul(interface.substr(slash_position + 1));
        uint64_t mask = 0xffffffff & (0xffffffff << mask_len);
        from_ip = ip & mask;
        to_ip = from_ip + (1 << mask_len);
    }
    auto interfaces = engine().net().get_interfaces();
    for (auto i : interfaces) {
        std::cout << "interface "<< i.name << std::endl;
        if (interface == i.name ||
                (to_ip != 0 && i.host_address.ip >= from_ip && i.host_address.ip <= to_ip) ) {
            return make_ready_future<gms::inet_address>(i.host_address.ip);
        }
    }
    throw std::runtime_error("Failed getting interface '" + interface + "' information");
}
