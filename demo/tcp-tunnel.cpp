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

#include <comfort-zone.hpp>

#include <iostream>
#include <memory>

namespace {

    class Tunnel
        : public cz::Task
    {
        /* data. */
    private:
        cz::Engine& myEngine;
        w32::net::ipv4::EndPoint myPeer;

        /* construction. */
    public:
        Tunnel (cz::Engine& engine, const w32::net::ipv4::EndPoint& peer)
            : myEngine(engine)
            , myPeer(peer)
        {}

        /* overrides. */
    protected:
        virtual void run ()
        {
            std::cerr
                << "+task()"
                << std::endl;

            // Reserve memory.
            char host_buffer[8*1024];
            char peer_buffer[8*1024];

            // Establish connection.
            std::auto_ptr<cz::SocketChannel> peer_stream(myEngine.connect(myPeer));
            std::auto_ptr<cz::BlockingReader> host_stream(myEngine.standard_input());

            // Prepare async I/O operations.
            cz::BlockingGet read_stdin(host_stream->stream(), host_buffer, sizeof(host_buffer));
            cz::PutRequest peer_get(myEngine, peer_stream->socket());
            cz::WorkRequest host_get(myEngine, read_stdin);

            // Start I/O operations.
            host_get.start();
            peer_get.start(peer_buffer, sizeof(peer_buffer));

            // TODO: ask hub to block on both requests.
            // TODO: implement O(1) dispatch...

            //host_get.close(); // use number of bytes read from standard input.
            //peer_get.close(); // use number of bytes read from socket.

            std::cerr
                << "-task()"
                << std::endl;
        }
    };

}

#include <w32/app/console-program.hpp>

namespace {

    int run (int argc, wchar_t ** argv)
    {
        std::cerr << "+program()" << std::endl;

        const w32::net::Context _;

        // Start an I/O application!
        cz::Hub hub;
        cz::Engine engine(hub);

        const w32::net::ipv4::EndPoint peer(
            w32::net::ipv4::Address::local(), 8000);
        { ::Tunnel task(engine, peer);
            // Process notifications until all tasks are done.
            while (hub.active_slaves() > 0) {
                engine.wait_for_notification(), hub.resume_pending_slave();
            }
        }

        std::cerr << "-program()" << std::endl;
        return (EXIT_SUCCESS);
    }
}

#include <w32/app/console-program.cpp>
