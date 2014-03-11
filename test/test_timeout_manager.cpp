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

#include "helpers/common.hpp"

#include <vector>
#include <kademlia/error.hpp>

#include "timeout_manager.hpp"

namespace k = kademlia;
namespace kd = k::detail;

/**
 */
BOOST_AUTO_TEST_SUITE( test_construction )

BOOST_AUTO_TEST_CASE( can_be_constructed_using_a_reactor )
{
    boost::asio::io_service io_service;
    BOOST_REQUIRE_NO_THROW( kd::timeout_manager{ io_service } );
}

BOOST_AUTO_TEST_SUITE_END()

struct fixture
{
    fixture()
        : io_service_{}
        , work_{ io_service_ }
        , manager_{ io_service_ }
        , timeouts_received_{}
    { }

    boost::asio::io_service io_service_;
    boost::asio::io_service::work work_;
    kd::timeout_manager manager_;
    std::size_t timeouts_received_;
};

/**
 *
 */
BOOST_AUTO_TEST_SUITE( test_usage )

BOOST_FIXTURE_TEST_CASE( multiple_associations_can_be_added, fixture )
{
    BOOST_REQUIRE_EQUAL( 0, io_service_.poll() );
    BOOST_REQUIRE_EQUAL( 0, timeouts_received_ );

    // Create the association.
    auto on_expiration = [ this ] ( void )
    { ++ timeouts_received_; };

    auto const infinite = std::chrono::hours( 1 );
    manager_.expires_from_now( infinite, on_expiration );
    BOOST_REQUIRE_EQUAL( 0, io_service_.poll() );
    BOOST_REQUIRE_EQUAL( 0, timeouts_received_ );

    // This new expiration should trigger a cancel of the current 
    // timeout (infinite), hence one task execution +
    // the execution of the new timeout (immediate).
    auto const immediate = kd::timeout_manager::duration::zero();
    manager_.expires_from_now( immediate, on_expiration );
    BOOST_REQUIRE_EQUAL( 2, io_service_.poll() );
    BOOST_REQUIRE_EQUAL( 1, timeouts_received_ );

    // A timeout (infinite) is still in flight atm.
}

BOOST_FIXTURE_TEST_CASE( close_timeouts_can_be_added, fixture )
{
    BOOST_REQUIRE_EQUAL( 0, io_service_.poll() );
    BOOST_REQUIRE_EQUAL( 0, timeouts_received_ );

    // Create the association.
    auto on_expiration = [ this ] ( void )
    { ++ timeouts_received_; };

    auto const immediate = kd::timeout_manager::duration::zero();
    manager_.expires_from_now( immediate, on_expiration );
    manager_.expires_from_now( immediate, on_expiration );
    std::size_t execute_tasks_count = io_service_.poll();
    if ( execute_tasks_count == 1 )
        execute_tasks_count += io_service_.run();

    BOOST_REQUIRE_EQUAL( 2, execute_tasks_count );
    BOOST_REQUIRE_EQUAL( 2, timeouts_received_ );
}

BOOST_AUTO_TEST_SUITE_END()
