#ifndef _cz_Listener_hpp__
#define _cz_Listener_hpp__

// Copyright (c) 2012, Andre Caron (andre.l.caron@gmail.com)
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "Buffer.hpp"
#include "Stream.hpp"
#include "Request.hpp"
#include <w32.net.hpp>

namespace cz {

    class Engine;
    class SocketChannel;

    /*!
     * @ingroup resources
     * @brief Accepts incoming TCP connections.
     */
    class Listener
    {
        /* nested types. */
    private:
        class Call;

        /* data. */
    private:
        Engine& myEngine;

        w32::net::tcp::Listener mySocket;

        /* construction. */
    public:
        Listener (Engine& engine, w32::net::ipv4::EndPoint endpoint);
        virtual ~Listener ();

        /* methods. */
    public:
        w32::net::tcp::Listener& socket ();

        SocketChannel * accept (void * data, size_t size);
    };


    /*!
     * @ingroup requests
     * @brief %Request to wait for incoming network connections.
     *
     * @see ConnectRequest
     */
    class AcceptRequest
    {
        /* class data. */
    private:
        static const size_t single_endpoint_size = sizeof(::sockaddr_in) + 16;
        static const size_t double_endpoint_size = 2 * single_endpoint_size;

    public:
        static const size_t minimum_buffer_size = double_endpoint_size;

        static const w32::dword not_connected = MAXDWORD;

        /* data. */
    private:
        Request myRequest;

        w32::net::tcp::Listener& myListener;
        SocketChannel& myChannel;

        // Application buffer for initial payload.
        Buffer& myBuffer;

        // Endpoints, updated once the connection is established.
        w32::net::ipv4::EndPoint myHost;
        w32::net::ipv4::EndPoint myPeer;

        /* construction. */
    public:
        /*!
         * @warning Using the optional @a buffer_size argument may introduce
         *  the risk of a denial of service attack; do not use it naively.  Use
         *  @ref connected_since and @ref drop_connection to protect yourself
         *  against this attack.
         */
        AcceptRequest (Engine& engine,
                       w32::net::tcp::Listener& listener,
                       SocketChannel& channel,
                       Buffer& buffer, void * context=0);

        /* methods. */
    public:
        /*!
         * @brief Check how long since a connection has been established.
         * @return Integer number of seconds a socket has been connected
         *  without sending any data, or `not_connected` if the request is
         *  still in progress.
         *
         * @see DisconnectRequest
         * @see drop_connection
         */
        w32::dword connected_since () const;

        /*!
         * @brief Drop a connection that isn't sending any data.
         * @pre `connected_since() != not_connected`
         *
         * This method is useful to protect your server against a potential
         * denial of service attack from clients that connect but don't send
         * data (if you use a buffer to get the first payload, the accept
         * request will not complete until at least one byte is sent by the
         * peer).
         *
         * @see connected_since
         *
         * @note This method does not support socket reuse, it should only be
         *  used when shutting down the server.  If you want to reuse the
         *  socket, use a `DisconnectRequest`, which will end up causing the
         *  `AcceptRequest` to fail.
         *
         * @todo Figure out which error code will be issued...
         *
         * @todo Implement this!
         *
         * @see DisconnectRequest
         */
        void drop_connection ();

        bool start ();
        bool ready () const;

        bool is (Request * request) const;

        // Note: `close()` is implicit (native async request).

        bool result ();

        const w32::net::ipv4::EndPoint& host () const;
        const w32::net::ipv4::EndPoint& peer () const;

        Buffer& buffer ();

        void reset (); // call before calling `start()` again.

        bool abort();

    private:
        void update_context ();
        void recover_endpoints (w32::net::ipv4::EndPoint& host,
                                w32::net::ipv4::EndPoint& peer);
    };

    /*!
     * @ingroup requests
     * @brief %Request to disconnect (and reuse) a TCP socket.
     */
    class DisconnectRequest
    {
        /* data. */
    private:
        Request myRequest;
        SocketChannel& myChannel;

        // Used to prevent `ready()` from returning the same value after _and
        // before_ the overlapped operation has completed (the default state
        // makes `ready()` return `true`).
        enum State {
            Idle,
            Busy,
        } myState;

        w32::dword myResult;

    public:
        DisconnectRequest (Engine& engine, SocketChannel& channel, void * context=0);

        /* methods. */
    public:
        void start (bool reuse=false);
        bool ready () const;

        bool is (Request * request) const;

        // Note: `close()` is implicit (native async request).

        // Note: no result.
        void result ();

        // call before calling `start()` again.
        void reset ();
        void reset (void * context);

        // Does it even make sense to abort this request (e.g. if we're blocked
        // on a TIME_WAIT socket during server shutdown)?
        void abort ();

        void * context () const;

        template<typename T>
        T * context () const
        {
            return (myRequest.context<T>());
        }
    };

}

#endif /* _cz_Listener_hpp__ */
