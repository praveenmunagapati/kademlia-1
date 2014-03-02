// Copyright (c) 2013-2014, David Keller
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the University of California, Berkeley nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY DAVID KELLER AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "message_socket.hpp"

#include "helpers/common.hpp"

namespace k = kademlia;
namespace kd = kademlia::detail;

/**
 *
 */
BOOST_AUTO_TEST_SUITE( test_construction )

BOOST_AUTO_TEST_CASE( faulty_address_are_detected )
{
    boost::asio::io_service io_service;

    auto const port = get_temporary_listening_port();
    BOOST_REQUIRE_THROW( kd::resolve_endpoint( io_service, { "error", port } )
                       , std::exception );
}

BOOST_AUTO_TEST_CASE( ipv4_socket_can_be_created )
{
    boost::asio::io_service io_service;

    auto const port = get_temporary_listening_port();
    auto e = kd::resolve_endpoint( io_service, { "127.0.0.1", port } );
    BOOST_REQUIRE_EQUAL( 1, e.size() );

    BOOST_REQUIRE_NO_THROW( k::detail::create_socket( io_service, e.front() ) );
}

BOOST_AUTO_TEST_CASE( ipv6_socket_can_be_created )
{
    boost::asio::io_service io_service;

    auto const port = get_temporary_listening_port();
    auto e = kd::resolve_endpoint( io_service, { "::1", port } );
    BOOST_REQUIRE_EQUAL( 1, e.size() );
    
    BOOST_REQUIRE_NO_THROW( k::detail::create_socket( io_service, e.front() ) );
}

BOOST_AUTO_TEST_SUITE_END()
