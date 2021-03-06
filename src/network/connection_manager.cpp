/*
 * Copyright (C) 2013  Maxim Noah Khailo
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
 */

#include "network/connection_manager.hpp"

#include "util/dbc.hpp"
#include "util/log.hpp"

#include <boost/lexical_cast.hpp>

namespace u = fire::util;

namespace fire
{
    namespace network
    {
        connection_manager::connection_manager(size_t size, const std::string& local_port) :
            _pool(size),
            _local_port{local_port},
            _next_available{0},
            _rstate{receive_state::IN_UDP1}
        {
            //create listen socket
#ifdef __APPLE__
            auto listen_address = make_tcp_address("*", _local_port);

            queue_options qo = { 
                {"bnd", "1"},
                {"block", "0"},
                {"track_incoming", "1"}};

            _in = create_tcp_queue(listen_address, qo);
#endif

            //create outgoing params
            asio_params p = {
                asio_params::tcp, 
                asio_params::delayed_connect, 
                "", //uri
                "", //host
                "", //port
                _local_port,
                false, //block;
                0, // wait;
                false, //track_incoming;
            };

            //create socket pool
            for(size_t i = 0; i < size; ++i)
                _pool[i] = std::make_shared<tcp_queue>(p);

#ifndef __APPLE__
            //create listen socket
            auto listen_address = make_tcp_address("*", _local_port);

            queue_options qo = { 
                {"bnd", "1"},
                {"block", "0"},
                {"track_incoming", "1"}};

            _in = create_tcp_queue(listen_address, qo);
#endif

            asio_params udp_p = {
                asio_params::udp, 
                asio_params::bind, 
                "", //uri
                "", //host
                "", //port
                _local_port,
                false, //block;
                0, // wait;
                true, //track_incoming;
            };
            _udp_con = create_udp_queue(udp_p);

            INVARIANT(_in);
        }

        tcp_queue_ptr connection_manager::connect(const std::string& address)
        try
        {
            REQUIRE_RANGE(_next_available, 0, _pool.size());

            //return if we are already assigned a socket to that address
            auto p = _out.find(address);
            if(p != _out.end()) return _pool[p->second];

            //parse address to host, port
            auto a = parse_address(address);

            auto i = _next_available;
            _next_available++;

            //assign address to socket and connect
            _out[address] = i;
            _pool[i]->connect(a.host, a.port);

            ENSURE_BETWEEN(_next_available, 0, _pool.size());
            return _pool[i];
        }
        catch(std::exception& e)
        {
            LOG << "error connecting to `" << address << "'. " << e.what() << std::endl; 
        }
        catch(...)
        {
            LOG << "unknown error connecting to `" << address << "'." << std::endl; 
        }

        bool connection_manager::send(const std::string& to, const u::bytes& b)
        try
        {
            u::mutex_scoped_lock l(_mutex);

            auto type = determine_type(to);

            if(type == asio_params::tcp)
            {

                //first check in tcp_connections for matching address and use that to send
                //otherwise use outgoing tcp_connection.
                auto inp = _in_connections.find(to);
                if(inp != _in_connections.end())
                {
                    auto o = inp->second;
                    CHECK(o);
                    return o->send(b);
                }

                auto o = connect(to);
                CHECK(o);
                return o->send(b);
            }

            CHECK(type == asio_params::udp);

            auto a = parse_address(to);
            endpoint ep { UDP, a.host, a.port};
            endpoint_message em{ep, b}; 
            return _udp_con->send(em);
        }
        catch(std::exception& e)
        {
            LOG << "error sending message to `" << to << "' (" << b.size() << " bytes). " << e.what() << std::endl; 
        }
        catch(...)
        {
            LOG << "unknown error sending message to `" << to << "' (" << b.size() << " bytes)." << std::endl; 
        }

        void connection_manager::transition_udp_state()
        {
            auto ps = _rstate; 
            switch(_rstate)
            {
                case receive_state::IN_UDP1: _rstate = receive_state::IN_UDP2; break;
                case receive_state::IN_UDP2: _rstate = receive_state::IN_UDP3; break;
                case receive_state::IN_UDP3: _rstate = receive_state::IN_UDP4; break;
                case receive_state::IN_UDP4: _rstate = receive_state::IN_UDP5; break;
                case receive_state::IN_UDP5: _rstate = receive_state::IN_UDP6; break;
                case receive_state::IN_UDP6: _rstate = receive_state::IN_UDP7; break;
                case receive_state::IN_UDP7: _rstate = receive_state::IN_UDP8; break;
                case receive_state::IN_UDP8: _rstate = receive_state::IN_TCP; break;
            }

            ENSURE(ps != receive_state::IN_UDP8 || _rstate == receive_state::IN_TCP) 
            ENSURE(_rstate != ps);
        }

        bool connection_manager::receive(endpoint& ep, u::bytes& b)
        {
            u::mutex_scoped_lock l(_mutex);
            INVARIANT(_in);

            //if we quite in prior call and got to done state
            //or returned with a message in OUT_TCP case, we
            //reset to IN_UPD and start over
            if(_rstate == receive_state::DONE) 
                _rstate = receive_state::IN_UDP1;

            while(_rstate != receive_state::DONE)
            {
                switch(_rstate)
                {
                    case receive_state::IN_UDP1:
                    case receive_state::IN_UDP2:
                    case receive_state::IN_UDP3:
                    case receive_state::IN_UDP4:
                    case receive_state::IN_UDP5:
                    case receive_state::IN_UDP6:
                    case receive_state::IN_UDP7:
                    case receive_state::IN_UDP8:
                        {
                            endpoint_message um;
                            if(_udp_con->receive(um))
                            {
                                transition_udp_state();

                                ep = um.ep;
                                b = std::move(um.data);
                                return true;
                            }
                            _rstate = receive_state::IN_TCP;
                        }
                        break;

                    case receive_state::IN_TCP:
                        {
                            _rstate = receive_state::OUT_TCP;
                            if(_in->receive(b)) 
                            {
                                auto s = _in->get_socket();
                                CHECK(s);
                                ep = s->get_endpoint();
                                _in_connections[make_address_str(ep)] = s;
                                return true;
                            }
                        }
                        break;

                    case receive_state::OUT_TCP:
                        {
                            _rstate = receive_state::DONE;
                            for(auto p : _out)
                            {
                                auto i = p.second;
                                CHECK_RANGE(i, 0, _pool.size());

                                auto c = _pool[i];
                                CHECK(c);
                                if(c->receive(b)) 
                                {
                                    auto s = c->get_socket();
                                    CHECK(s);
                                    ep = s->get_endpoint();
                                    return true;
                                }
                            }
                        }
                        break;
                }
            }

            ENSURE(_rstate == receive_state::DONE);
            return false;
        }

        bool connection_manager::is_disconnected(const std::string& addr)
        {
            auto s = _in_connections[addr];
            if(!s) return true;
            return s->is_disconnected();
        }
    }
}
