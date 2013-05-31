#ifndef _fio_Listener_hpp__
#define _fio_Listener_hpp__

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

#include "Stream.hpp"
#include "Request.hpp"
#include <w32.net.hpp>

namespace fio {

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
        static const int single_endpoint_size = sizeof(::sockaddr_in) + 16;
        static const int double_endpoint_size = 2 * single_endpoint_size;

        /* data. */
    private:
        Request myRequest;

        w32::net::tcp::Listener& myListener;
        w32::net::StreamSocket myStream;

        // TODO: use application-supplied buffer for inbound data.
        char myBuffer[double_endpoint_size];

        /* construction. */
    public:
        AcceptRequest (Engine& engine, w32::net::tcp::Listener& listener);

        /* methods. */
    public:
        void start ();
        bool ready () const;
        SocketChannel * close ();
        void reset (); // call before calling `start()` again.

    private:
        // TODO: figure out how to use this properly.
        void abort();

        void update_context ();
        void recover_endpoints (w32::net::ipv4::EndPoint& host,
                                w32::net::ipv4::EndPoint& peer);
    };

}

#endif /* _fio_Listener_hpp__ */
