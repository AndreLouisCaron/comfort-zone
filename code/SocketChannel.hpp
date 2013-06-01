#ifndef _cz_SocketChannel_hpp__
#define _cz_SocketChannel_hpp__

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

#include <w32.hpp>
#include <w32.io.hpp>
#include <w32.net.hpp>

#include "Request.hpp"
#include "Stream.hpp"

namespace cz {

    class Engine;

    /*!
     * @ingroup resources
     * @brief Channel that reads from, and writes to, a TCP socket.
     */
    class SocketChannel :
        public Channel
    {
        /* data. */
    private:
        Engine& myEngine;
        w32::net::StreamSocket myStream;

        w32::net::ipv4::EndPoint myHost;
        w32::net::ipv4::EndPoint myPeer;

        /* constructor. */
    public:
        explicit SocketChannel (Engine& engine,
                                w32::net::StreamSocket& stream,
                                w32::net::ipv4::EndPoint host,
                                w32::net::ipv4::EndPoint peer);

        /* methods. */
    public:
        const w32::net::ipv4::EndPoint& host () const;
        const w32::net::ipv4::EndPoint& peer () const;

        w32::net::StreamSocket& socket ();

        /* overrides. */
    public:
        virtual size_t get (void * data, size_t size);
        virtual size_t put (const void * data, size_t size);
    };


    /*!
     * @ingroup requests
     * @brief %Request to establish outbound network connections.
     *
     * @see AcceptRequest
     */
    class ConnectRequest
    {
        /* data. */
    private:
        Request myRequest;

        w32::net::ipv4::EndPoint myHost;
        w32::net::ipv4::EndPoint myPeer;

        w32::dword mySent;
        w32::net::StreamSocket myStream;

        // TODO: use application-supplied buffer for outbound data.

        /* constructor. */
    public:
        explicit ConnectRequest (Engine& engine,
                                 w32::net::ipv4::EndPoint host,
                                 w32::net::ipv4::EndPoint peer);

        /* methods. */
    public:
        // TODO: accept application-supplied buffer.
        void start ();
        bool ready () const;
        SocketChannel * close (); // TODO: return size of transferred.
        void reset (); // call before calling `start()` again.

    private:
        // TODO: figure out how to use this properly.
        void abort();
    };

}

#endif /* _cz_SocketChannel_hpp__ */
