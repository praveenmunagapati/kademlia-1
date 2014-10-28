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

#ifndef KADEMLIA_ENGINE_HPP
#define KADEMLIA_ENGINE_HPP

#ifdef _MSC_VER
#   pragma once
#endif

#include <algorithm>
#include <stdexcept>
#include <queue>
#include <chrono>
#include <random>
#include <memory>
#include <utility>
#include <type_traits>
#include <functional>
#include <boost/asio/io_service.hpp>

#include <kademlia/endpoint.hpp>
#include <kademlia/error.hpp>

#include "kademlia/log.hpp"
#include "kademlia/ip_endpoint.hpp"
#include "kademlia/message_serializer.hpp"
#include "kademlia/response_router.hpp"
#include "kademlia/network.hpp"
#include "kademlia/message.hpp"
#include "kademlia/routing_table.hpp"
#include "kademlia/value_store.hpp"
#include "kademlia/find_value_context.hpp"
#include "kademlia/store_value_context.hpp"
#include "kademlia/core.hpp"
#include "kademlia/notify_peer_context.hpp"

namespace kademlia {
namespace detail {

/**
 *
 */
template< typename KeyType, typename DataType, typename UnderlyingSocketType >
class engine final
{
public:
    ///
    using key_type = KeyType;

    ///
    using data_type = DataType;

    ///
    using endpoint_type = ip_endpoint;

    ///
    using routing_table_type = routing_table< endpoint_type >;

    ///
    using value_store_type = value_store< id, data_type >;

public:
    /**
     *
     */
    engine
        ( boost::asio::io_service & io_service
        , endpoint const& ipv4
        , endpoint const& ipv6 )
            : random_engine_()
            , my_id_( random_engine_ )
            , network_( io_service, ipv4, ipv6
                      , std::bind( &engine::handle_new_message
                                 , this
                                 , std::placeholders::_1
                                 , std::placeholders::_2
                                 , std::placeholders::_3 ) )
            , core_( io_service
                   , my_id_
                   , network_
                   , random_engine_ )
            , routing_table_( my_id_ )
            , value_store_()
            , is_connected_()
            , pending_tasks_()
    { }

    /**
     *
     */
    engine
        ( boost::asio::io_service & io_service
        , endpoint const& initial_peer
        , endpoint const& ipv4
        , endpoint const& ipv6 )
            : random_engine_()
            , my_id_( random_engine_ )
            , network_( io_service, ipv4, ipv6
                      , std::bind( &engine::handle_new_message
                                 , this
                                 , std::placeholders::_1
                                 , std::placeholders::_2
                                 , std::placeholders::_3 ) )
            , core_( io_service
                   , my_id_
                   , network_
                   , random_engine_ )
            , routing_table_( my_id_ )
            , value_store_()
            , is_connected_()
            , pending_tasks_()
    {
        discover_neighbors( initial_peer );

        LOG_DEBUG( engine, this ) << "boostraping using peer '"
                << initial_peer << "'." << std::endl;
    }

    /**
     *
     */
    engine
        ( engine const& )
        = delete;

    /**
     *
     */
    engine &
    operator=
        ( engine const& )
        = delete;

    /**
     *
     */
    template< typename HandlerType >
    void
    async_save
        ( key_type const& key
        , data_type const& data
        , HandlerType && handler )
    {
        // If the routing table is empty, save the
        // current request for processing when
        // the routing table will be filled.
        if ( ! is_connected_ )
        {
            LOG_DEBUG( engine, this ) << "delaying async save of key '"
                    << to_string( key ) << "'." << std::endl;

            auto t = [ this, key, data, handler ] ( void ) mutable
            { async_save( key, data, std::move( handler ) ); };

            pending_tasks_.push( std::move( t ) );
        }
        else
        {
            LOG_DEBUG( engine, this ) << "executing async save of key '"
                    << to_string( key ) << "'." << std::endl;

            auto i = routing_table_.find( id( key ) )
               , e = routing_table_.end();
            auto c = create_store_value_context( id( key )
                                               , data
                                               , i, e
                                               , std::forward< HandlerType >( handler ) );
            store_value( c );
        }
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    async_load
        ( key_type const& key
        , HandlerType && handler )
    {
        // If the routing table is empty, save the
        // current request for processing when
        // the routing table will be filled.
        if ( ! is_connected_ )
        {
            LOG_DEBUG( engine, this ) << "delaying async load of key '"
                    << to_string( key ) << "'." << std::endl;

            auto t = [ this, key, handler ] ( void ) mutable
            { async_load( key, std::move( handler ) ); };

            pending_tasks_.push( std::move( t ) );
        }
        else
        {
            LOG_DEBUG( engine, this ) << "executing async load of key '"
                    << to_string( key ) << "'." << std::endl;

            auto i = routing_table_.find( id( key ) )
               , e = routing_table_.end();
            auto c = create_find_value_context< data_type >( id( key )
                                                           , i, e
                                                           , std::forward< HandlerType >( handler ) );
            find_value( c );
        }
    }

private:
    ///
    using pending_task_type = std::function< void ( void ) >;

    ///
    using network_type = network< UnderlyingSocketType >;

    ///
    using core_type = core< std::default_random_engine, network_type >;

private:
    /**
     *
     */
    void
    process_new_message
        ( ip_endpoint const& sender
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e )
    {
        switch ( h.type_ )
        {
            case header::PING_REQUEST:
                handle_ping_request( sender, h );
                break;
            case header::STORE_REQUEST:
                handle_store_request( sender, h, i, e );
                break;
            case header::FIND_PEER_REQUEST:
                handle_find_peer_request( sender, h, i, e );
                break;
            case header::FIND_VALUE_REQUEST:
                handle_find_value_request( sender, h, i, e );
                break;
            default:
                core_.handle_new_response( sender, h, i, e );
                break;
        }
    }

    /**
     *
     */
    void
    handle_ping_request
        ( ip_endpoint const& sender
        , header const& h )
    {
        LOG_DEBUG( engine, this ) << "handling ping request." << std::endl;

        core_.send_response( h.random_token_
                           , header::PING_RESPONSE
                           , sender );
    }

    /**
     *
     */
    void
    handle_store_request
        ( ip_endpoint const& sender
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e )
    {
        LOG_DEBUG( engine, this ) << "handling store request."
                << std::endl;

        store_value_request_body request;
        if ( auto failure = deserialize( i, e, request ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize store value request ("
                    << failure.message() << ")." << std::endl;

            return;
        }

        value_store_[ request.data_key_hash_ ]
                = std::move( request.data_value_ );
    }

    /**
     *
     */
    void
    handle_find_peer_request
        ( ip_endpoint const& sender
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e )
    {
        LOG_DEBUG( engine, this ) << "handling find peer request."
                << std::endl;

        // Ensure the request is valid.
        find_peer_request_body request;
        if ( auto failure = deserialize( i, e, request ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize find peer request ("
                    << failure.message() << ")" << std::endl;

            return;
        }

        send_find_peer_response( sender
                               , h.random_token_
                               , request.peer_to_find_id_ );
    }

    /**
     *
     */
    void
    send_find_peer_response
        ( ip_endpoint const& sender
        , id const& random_token
        , id const& peer_to_find_id )
    {
        // Find X closest peers and save
        // their location into the response..
        find_peer_response_body response;

        auto remaining_peer = ROUTING_TABLE_BUCKET_SIZE;
        for ( auto i = routing_table_.find( peer_to_find_id )
                 , e = routing_table_.end()
            ; i != e && remaining_peer > 0
            ; ++i, -- remaining_peer )
            response.peers_.push_back( { i->first, i->second } );

        // Now send the response.
        core_.send_response( random_token, response, sender );
    }

    /**
     *
     */
    void
    handle_find_value_request
        ( ip_endpoint const& sender
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e )
    {
        LOG_DEBUG( engine, this ) << "handling find value request."
                << std::endl;

        find_value_request_body request;
        if ( auto failure = deserialize( i, e, request ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize find value request ("
                    << failure.message() << ")" << std::endl;

            return;
        }

        auto found = value_store_.find( request.value_to_find_ );
        if ( found == value_store_.end() )
            send_find_peer_response( sender
                                   , h.random_token_
                                   , request.value_to_find_ );
        else
        {
            find_value_response_body const response{ found->second };
            core_.send_response( h.random_token_
                               , response
                               , sender );
        }
    }

    /**
     *
     */
    void
    discover_neighbors
        ( endpoint const& initial_peer )
    {
        // Initial peer should know our neighbors, hence ask
        // him which peers are close to our own id.
        auto endoints_to_query = network_.resolve_endpoint( initial_peer );

        search_ourselves( std::move( endoints_to_query ) );
    }

    /**
     *
     */
    template< typename ResolvedEndpointType >
    void
    search_ourselves
        ( ResolvedEndpointType endpoints_to_query )
    {
        if ( endpoints_to_query.empty() )
            throw std::system_error
                    { make_error_code( INITIAL_PEER_FAILED_TO_RESPOND ) };

        // Retrieve the next endpoint to query.
        auto const endpoint_to_query = endpoints_to_query.back();
        endpoints_to_query.pop_back();

        // On message received, process it.
        auto on_message_received = [ this ]
            ( ip_endpoint const& s
            , header const& h
            , buffer::const_iterator i
            , buffer::const_iterator e )
        { handle_initial_contact_response( s, h, i, e ); };

        // On error, retry with another endpoint.
        auto on_error = [ this, endpoints_to_query ]
            ( std::error_code const& )
        { search_ourselves( endpoints_to_query ); };

        core_.send_request( find_peer_request_body{ my_id_ }
                          , endpoint_to_query
                          , INITIAL_CONTACT_RECEIVE_TIMEOUT
                          , on_message_received
                          , on_error );
    }

    /**
     *
     */
    void
    handle_initial_contact_response
        ( ip_endpoint const& s
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e )
    {
        LOG_DEBUG( engine, this ) << "handling init contact response."
                << std::endl;

        if ( h.type_ != header::FIND_PEER_RESPONSE )
            return ;

        find_peer_response_body response;
        if ( auto failure = deserialize( i, e, response ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize find peer response ("
                    << failure.message() << ")" << std::endl;

            return;
        }

        // Add discovered peers.
        for ( auto const& peer : response.peers_ )
            routing_table_.push( peer.id_, peer.endpoint_ );

        notify_neighbors();

        LOG_DEBUG( engine, this ) << "added '" << response.peers_.size()
                << "' initial peer(s)." << std::endl;
    }

    /**
     *  Refresh each bucket.
     */
    void
    notify_neighbors
        ( void )
    {
        id refresh_id = my_id_;

        for ( std::size_t j = id::BIT_SIZE; j > 0; -- j)
        {
            // Flip bit to select find peers in the current k_bucket.
            id::reference bit = refresh_id[ j - 1 ];
            bit = ! bit;

            start_notify_peer_task( refresh_id, core_, routing_table_ );
        }
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    store_value
        ( std::shared_ptr< store_value_context< HandlerType, data_type > > context
        , std::size_t concurrent_requests_count = CONCURRENT_FIND_PEER_REQUESTS_COUNT )
    {
        LOG_DEBUG( engine, this ) << "sending find peer to store '"
                << context->get_key() << "' value." << std::endl;

        find_peer_request_body const request{ context->get_key() };

        auto const closest_candidates = context->select_new_closest_candidates
                ( concurrent_requests_count );

        assert( "at least one candidate exists" && ! closest_candidates.empty() );

        for ( auto const& c : closest_candidates )
            send_find_peer_to_store_request( request, c, context );
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    send_find_peer_to_store_request
        ( find_peer_request_body const& request
        , peer const& current_candidate
        , std::shared_ptr< store_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "sending find peer request to store to '"
                << current_candidate << "'." << std::endl;

        // On message received, process it.
        auto on_message_received = [ this, context, current_candidate ]
            ( ip_endpoint const& s
            , header const& h
            , buffer::const_iterator i
            , buffer::const_iterator e )
        {
            context->flag_candidate_as_valid( current_candidate.id_ );

            handle_find_peer_to_store_response( s, h, i, e, context );
        };

        // On error, retry with another endpoint.
        auto on_error = [ this, context, current_candidate ]
            ( std::error_code const& )
        {
            // XXX: Can also flag candidate as invalid is
            // present in routing table.
            context->flag_candidate_as_invalid( current_candidate.id_ );

            // If no more requests are in flight
            // we know the closest peers hence ask
            // them to store the value.
            if ( context->have_all_requests_completed() )
                send_store_requests( context );
        };

        core_.send_request( request
                          , current_candidate.endpoint_
                          , PEER_LOOKUP_TIMEOUT
                          , on_message_received
                          , on_error );
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    find_value
        ( std::shared_ptr< find_value_context< HandlerType, data_type > > context )
    {
        find_value_request_body const request{ context->get_key() };

        auto const closest_candidates = context->select_new_closest_candidates
                ( CONCURRENT_FIND_PEER_REQUESTS_COUNT );

        assert( "at least one candidate exists" && ! closest_candidates.empty() );

        for ( auto const& c : closest_candidates )
            send_find_value_request( request, c, context );
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    send_find_value_request
        ( find_value_request_body const& request
        , peer const& current_candidate
        , std::shared_ptr< find_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "sending find '" << context->get_key()
                << "' value request to '"
                << current_candidate << "'." << std::endl;

        // On message received, process it.
        auto on_message_received = [ this, context, current_candidate ]
            ( ip_endpoint const& s
            , header const& h
            , buffer::const_iterator i
            , buffer::const_iterator e )
        {
            if ( context->is_caller_notified() )
                return;

            context->flag_candidate_as_valid( current_candidate.id_ );
            handle_find_value_response( s, h, i, e, context );
        };

        // On error, retry with another endpoint.
        auto on_error = [ this, context, current_candidate ]
            ( std::error_code const& )
        {
            if ( context->is_caller_notified() )
                return;

            // XXX: Current current_candidate must be flagged as stale.
            context->flag_candidate_as_invalid( current_candidate.id_ );
            find_value( context );
        };

        core_.send_request( request
                          , current_candidate.endpoint_
                          , PEER_LOOKUP_TIMEOUT
                          , on_message_received
                          , on_error );
    }

    /**
     *  @brief This method is called while searching for
     *         the peer owner of the value.
     */
    template< typename HandlerType >
    void
    handle_find_value_response
        ( ip_endpoint const& s
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e
        , std::shared_ptr< find_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "handling response to find '"
                << context->get_key() << "' value." << std::endl;

        if ( h.type_ == header::FIND_PEER_RESPONSE )
            // The current peer didn't know the value
            // but provided closest peers.
            send_find_value_requests_on_closer_peers( i, e, context );
        else if ( h.type_ == header::FIND_VALUE_RESPONSE )
            // The current peer knows the value.
            process_found_value( i, e, context );
    }

    /**
     *  @brief This method is called when closest peers
     *         to the value we are looking are discovered.
     *         It recursively query new discovered peers
     *         or report an error to the use handler if
     *         all peers have been tried.
     */
    template< typename HandlerType >
    void
    send_find_value_requests_on_closer_peers
        ( buffer::const_iterator i
        , buffer::const_iterator e
        , std::shared_ptr< find_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "checking if found closest peers to '"
                << context->get_key() << "' value from closer peers."
                << std::endl;

        find_peer_response_body response;
        if ( auto failure = deserialize( i, e, response ) )
        {
            LOG_DEBUG( engine, this ) << "failed to deserialize find peer response '"
                    << context->get_key() << "' because ("
                    << failure.message() << ")." << std::endl;

            return;
        }

        if ( context->are_these_candidates_closest( response.peers_ ) )
            find_value( context );

        if ( context->have_all_requests_completed() )
            context->notify_caller( make_error_code( VALUE_NOT_FOUND ) );
    }

    /**
     *  @brief This method is called once the searched value
     *         has been found. It forwards the value to
     *         the user handler.
     */
    template< typename HandlerType >
    void
    process_found_value
        ( buffer::const_iterator i
        , buffer::const_iterator e
        , std::shared_ptr< find_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "found '" << context->get_key()
                << "' value." << std::endl;

        find_value_response_body response;
        if ( auto failure = deserialize( i, e, response ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize find value response ("
                    << failure.message() << ")" << std::endl;
            return;
        }

        context->notify_caller( response.data_ );
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    handle_find_peer_to_store_response
        ( ip_endpoint const& s
        , header const& h
        , buffer::const_iterator i
        , buffer::const_iterator e
        , std::shared_ptr< store_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "handle find peer to store response from '"
                << s << "'." << std::endl;

        assert( h.type_ == header::FIND_PEER_RESPONSE );
        find_peer_response_body response;
        if ( auto failure = deserialize( i, e, response ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize find peer response ("
                    << failure.message() << ")" << std::endl;
            return;
        }

        // If new candidate have been discovered, ask them.
        if ( context->are_these_candidates_closest( response.peers_ ) )
            store_value( context );
        else
        {
            LOG_DEBUG( engine, this ) << "'" << s
                    << "' did'nt provided closer peer to '"
                    << context->get_key() << "' value." << std::endl;

            // Else if all candidates have responded,
            // we know the closest peers hence ask them
            // to store the value.
            if ( context->have_all_requests_completed() )
                send_store_requests( context );
            else
                LOG_DEBUG( engine, this )
                        << "waiting for other peer(s) response to find '"
                        << context->get_key() << "' value." << std::endl;
        }
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    send_store_requests
        ( std::shared_ptr< store_value_context< HandlerType, data_type > > context )
    {
        auto const & candidates
                = context->select_closest_valid_candidates( REDUNDANT_SAVE_COUNT );

        assert( "at least one candidate exists" && ! candidates.empty() );

        for ( auto c : candidates )
            send_store_request( c, context );

        context->notify_caller( std::error_code{} );
    }

    /**
     *
     */
    template< typename HandlerType >
    void
    send_store_request
        ( peer const& current_candidate
        , std::shared_ptr< store_value_context< HandlerType, data_type > > context )
    {
        LOG_DEBUG( engine, this ) << "send store request of '"
                << context->get_key() << "' to '"
                << current_candidate << "'." << std::endl;

        store_value_request_body const request{ context->get_key()
                                              , context->get_data() };
        core_.send_request( request, current_candidate.endpoint_ );
    }

    /**
     *
     */
    void
    handle_new_message
        ( ip_endpoint const& sender
        , buffer::const_iterator i
        , buffer::const_iterator e  )
    {
        LOG_DEBUG( engine, this ) << "received new message from '"
                << sender << "'." << std::endl;

        detail::header h;
        // Try to deserialize header.
        if ( auto failure = deserialize( i, e, h ) )
        {
            LOG_DEBUG( engine, this )
                    << "failed to deserialize message header ("
                    << failure.message() << ")" << std::endl;
            return;
        }

        routing_table_.push( h.source_id_, sender );

        process_new_message( sender, h, i, e );

        // A message has been received, hence the connection
        // is up. Check if it was down before.
        if ( ! is_connected_ )
        {
            is_connected_ = true;
            execute_pending_tasks();
        }
    }

    /**
     *
     */
    void
    execute_pending_tasks
        ( void )
    {
        LOG_DEBUG( engine, this ) << "execute '" << pending_tasks_.size()
                << "' pending task(s)." << std::endl;

        // Some store/find requests may be pending
        // while the initial peer was contacted.
        while ( ! pending_tasks_.empty() )
        {
            pending_tasks_.front()();
            pending_tasks_.pop();
        }
    }

private:
    ///
    std::default_random_engine random_engine_;
    ///
    id my_id_;
    ///
    network_type network_;
    ///
    core_type core_;
    ///
    routing_table_type routing_table_;
    ///
    value_store_type value_store_;
    ///
    bool is_connected_;
    ///
    std::queue< pending_task_type > pending_tasks_;
};

} // namespace detail
} // namespace kademlia

#endif