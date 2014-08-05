// Copyright (c) 2014, Andre Caron (andre.l.caron@gmail.com)
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


#include "comfort-zone.hpp"


namespace {

    class TCPServer
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        const w32::net::ipv4::EndPoint myHost;

    public:
        TCPServer (cz::Engine& engine, const w32::net::ipv4::EndPoint& host)
            : myEngine(engine)
            , myHost(host)
        {
        }

    private:
        virtual void run ()
        {
            w32::net::tcp::Listener listener(myHost);
            w32::net::tcp::Stream stream;
            myEngine.completion_port().bind(listener.handle(), &myEngine);
            myEngine.completion_port().bind(stream.handle(), &myEngine);

            // Wait for the client to connect.
            cz::Promise promise = myEngine.accept(listener, stream);
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Error initiating socket accept!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            myEngine.wait_for(promise);
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Accept promise not fulfilled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }

            // Disconnect from the client.
            promise = myEngine.disconnect(stream);
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Error initiating disconnection!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            myEngine.wait_for(promise);
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Disconnection promise not fulfilled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
        }
    };

    class TCPClient
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        const w32::net::ipv4::EndPoint myPeer;

    public:
        TCPClient (cz::Engine& engine, const w32::net::ipv4::EndPoint& peer)
            : myEngine(engine)
            , myPeer(peer)
        {
        }

    private:
        virtual void run ()
        {
            w32::net::tcp::Stream stream;
            myEngine.completion_port().bind(stream.handle(), &myEngine);

            // Connect to the server.
            cz::Promise promise = myEngine.connect(stream, myPeer);
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Connect promise not issued!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            myEngine.wait_for(promise);
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Connect promise not fulfilled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }

            // Send some data.
            const char data[] = "Hello, world!";
            const w32::size_t size = sizeof(data) - 1;
            promise = myEngine.put(stream, data, size);
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Put promise not issued!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            myEngine.wait_for(promise);
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Put promise not fulfilled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }

            // Disconnect from the server.
            promise = myEngine.disconnect(stream);
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Disconnection promise not issued!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            myEngine.wait_for(promise);
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Disconnection promise not fulfilled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
        }
    };

    int test_socket_accept ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);

        w32::net::ipv4::EndPoint endpoint(127, 0, 0, 1, 666);

        // Start a TCP server.
        TCPServer server(engine, endpoint);
        hub.spawn(server, cz::Hub::StartNow);

        // Start a TCP client.
        TCPClient client(engine, endpoint);
        hub.spawn(client, cz::Hub::StartNow);

        // Unblock both the client and the server.
        engine.wait_for_notification();
        hub.resume_pending_slave();
        engine.wait_for_notification();
        hub.resume_pending_slave();
        engine.wait_for_notification();
        hub.resume_pending_slave();
        engine.wait_for_notification();
        hub.resume_pending_slave();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        // Make sure the hub closes successfully to avoid suprises when
        // destroying it.
        std::cout
            << "Shutting hub down."
            << std::endl;
        hub.shutdown();

        // Eveything should all be well and good by now :-)
        std::cout
            << "Test passed!"
            << std::endl;
        return (EXIT_SUCCESS);
    }

}



#include <w32/app/console-program.hpp>


namespace {

    int run (int, wchar_t **)
    {
        const w32::net::Context _;
        return (::test_socket_accept());
    }

}


#include <w32/app/console-program.cpp>
