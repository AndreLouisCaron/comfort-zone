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

#include "SocketChannel.hpp"
#include "Engine.hpp"
#include "Hub.hpp" // TODO: include "Slave.hpp"

#include "FileReader.hpp"
#include "FileWriter.hpp"

#include "trace.hpp"

namespace {

    w32::net::Socket::Handle create_socket ()
    {
        const ::SOCKET result = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if ( result == INVALID_SOCKET ) {
            const int error = ::WSAGetLastError();
            UNCHECKED_WIN32C_ERROR(socket, error);
        }
        return (w32::net::Socket::claim(result));
    }

    // Create a bound, unconnected stream socket.
    w32::net::StreamSocket create_socket (w32::net::ipv4::EndPoint host)
    {
        cz_trace(" >> Creating socket.");

        w32::net::tcp::Stream stream(create_socket());

        cz_trace(" >> Binding to local address.");

        const ::BOOL result = ::bind(stream.handle(), host.raw(), host.size());
        if (result == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            UNCHECKED_WIN32C_ERROR(bind, error);
        }

        return (stream);
    }

}

namespace cz {

    SocketChannel::SocketChannel (Engine& engine)
        : myEngine(engine)
        , myStream(w32::net::tcp::Stream()) // Note: real (unconnected) socket.
        , myHost()
        , myPeer()
    {
        // Make sure we receive I/O completion notifications :-)
        myEngine.completion_port().bind(myStream, &myEngine);
    }

    SocketChannel::SocketChannel (Engine& engine,
                                  w32::net::StreamSocket& stream,
                                  w32::net::ipv4::EndPoint host,
                                  w32::net::ipv4::EndPoint peer)
        : myEngine(engine)
        , myStream(stream)
        , myHost(host)
        , myPeer(peer)
    {
        // Note: stream is already bound to the completion port at this point.
    }

    const w32::net::ipv4::EndPoint& SocketChannel::host () const
    {
        return (myHost);
    }

    const w32::net::ipv4::EndPoint& SocketChannel::peer () const
    {
        return (myPeer);
    }

    w32::net::StreamSocket& SocketChannel::socket ()
    {
        return (myStream);
    }

    void SocketChannel::reset ()
    {
        myStream.linger(0);
        myStream = w32::net::tcp::Stream(); // Note: real (unconnected) socket.
        myHost = w32::net::ipv4::EndPoint();
        myPeer = w32::net::ipv4::EndPoint();
    }

    size_t SocketChannel::get (void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // Start the I/O operation.
        GetRequest request(myEngine, myStream);
        request.start(data, size);

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        return (request.result());
    }

    size_t SocketChannel::put (const void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // Start the I/O operation.
        PutRequest request(myEngine, myStream);
        request.start(data, size);

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        return (request.result());
    }

    ConnectRequest::ConnectRequest (Engine& engine,
                          w32::net::ipv4::EndPoint host,
                          w32::net::ipv4::EndPoint peer)
        : myRequest(engine)
        , myHost(host)
        , myPeer(peer)
        , mySent(0)
        , myStream(::create_socket(myHost))
    {
        Hub::Slave& self = Hub::Slave::self();

        // Make sure we receive I/O completion notifications :-)
        engine.completion_port().bind(myStream, &engine);
    }

    void ConnectRequest::start ()
    {
        w32::net::tcp::Stream::ConnectEx connect_ex =
            w32::net::tcp::Stream(myStream.handle()).connect_ex();

        cz_trace(" >> Starting connect request.");

        // Start the asynchronous connection request.
        const ::BOOL result = connect_ex(myStream.handle(),
                                         myPeer.raw(),
                                         myPeer.size(),
                                         0, 0, // initial payload.
                                         &mySent,
                                         &myRequest.data());
        if (result == FALSE)
        {
            const ::DWORD error = ::GetLastError();
            if (error != ERROR_IO_PENDING) {
                UNCHECKED_WIN32C_ERROR(ConnectEx, error);
            }
        }
    }

    bool ConnectRequest::ready () const
    {
        return (myRequest.ready());
    }

    SocketChannel * ConnectRequest::result()
    {
        // The hub has resume us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        // Connect requests can be cancelled :-(
        if (notification.aborted()) {
            cz_trace(" >> Connect request cancelled.");
            return (0);
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        cz_trace(" >> Connected!");
        cz_trace(" >> Host: " << myHost);
        cz_trace(" >> Peer: " << myPeer);

        // Patch socket for shutdown() and others to work as expected.
        const int result = ::setsockopt(myStream.handle(), SOL_SOCKET,
                                        SO_UPDATE_CONNECT_CONTEXT, 0, 0);
        if (result == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            UNCHECKED_WIN32C_ERROR(setsockopt, error);
        }

        return (new SocketChannel(myRequest.engine(), myStream, myHost, myPeer));
    }


    SocketGetRequest::SocketGetRequest (Engine& engine, SocketChannel& channel, void * context)
        : myRequest(engine, context)
        , myChannel(channel)
        , myState(Idle)
        , myResult(0)
    {
    }

    void SocketGetRequest::start (void * data, size_t size)
    {
        cz_trace("*request(0x" << &myRequest << "): start");
        if (myChannel.socket().get(data, size, myRequest.data(), myResult))
        {
            // The request object's contents MUST NOT be used, its contents
            // cannot be trusted because the call completed synchronously.
            myRequest.reset();

            if (myResult == 0) {
                myState = Dead;
                return;
            }

            // NOTE: after this, `.ready()` will return true and the
            //       application will be able to collect the results
            //       immediately.
        }
        myState = Busy;
    }

    bool SocketGetRequest::ready () const
    {
        return ((myState != Idle) && myRequest.ready());
    }

    bool SocketGetRequest::is (Request * request) const
    {
        return (request == &myRequest);
    }

    size_t SocketGetRequest::result ()
    {
        // The hub has resumed us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        if (notification.disconnected()) {
            // Peer forced disconnection, close connection.
            myState = Dead;
            return (0);
        }

        if (notification.aborted()) {
            // Was cancelled by application, close connection.
            myState = Dead;
            return (0);
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        // Let the caller know just how much data we received.
        return ((myResult > 0)? myResult : notification.size());
     }

    void SocketGetRequest::reset ()
    {
        myRequest.reset();
        myState = Idle;
        myResult = 0;
    }

    void SocketGetRequest::reset (void * context)
    {
        myRequest.reset(context);
        myState = Idle;
        myResult = 0;
    }

    void SocketGetRequest::abort ()
    {
        // Will cause I/O completion notification to be posted to the engine's
        // completion port and will be picked up later.
        myChannel.socket().cancel(myRequest.data());

        // TODO: is it useful for the application to know whether an operation
        //       was succesfully canceled or not?  They will receive a
        //       completion notification regardless...
    }

    bool SocketGetRequest::eof () const
    {
        return (myState == Dead);
    }

    void * SocketGetRequest::context () const
    {
        return (myRequest.context());
    }


    SocketPutRequest::SocketPutRequest (Engine& engine, SocketChannel& channel, void * context)
        : myRequest(engine, context)
        , myChannel(channel)
        , myState(Idle)
        , myResult(0)
    {
    }

    void SocketPutRequest::start (const void * data, size_t size)
    {
        cz_trace("*request(0x" << &myRequest << "): start");
        if (myChannel.socket().put(data, size, myRequest.data(), myResult))
        {
            // NOTE: when the send operation is successful and completes
            //       synchronously, it still emits a completion notification,
            //       so we should ignore the result even if it's already
            //       available.

            // The request object's contents MUST NOT be used, its contents
            // cannot be trusted because the call completed synchronously.
            myRequest.reset();

            // NOTE: after this, `.ready()` will return true and the
            //       application will be able to collect the results
            //       immediately.
        }
        myState = Busy;
    }

    bool SocketPutRequest::ready () const
    {
        return ((myState == Busy) && myRequest.ready());
    }

    bool SocketPutRequest::is (Request * request) const
    {
        return (request == &myRequest);
    }

    size_t SocketPutRequest::result ()
    {
        // The hub has resumed us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        if (notification.disconnected()) {
            // Peer forced disconnection, socke is unusable.
            myState = Dead;
            return (0);
        }

        if (notification.aborted()) {
            // Was cancelled by application, socket is unusable.
            myState = Dead;
            return (0);
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        // Let the caller know just how much data we received.
        return ((myResult > 0)? myResult : notification.size());
     }

    void SocketPutRequest::reset ()
    {
        myRequest.reset();
        myState = Idle;
        myResult = 0;
    }

    void SocketPutRequest::reset (void * context)
    {
        myRequest.reset(context);
        myState = Idle;
        myResult = 0;
    }

    void SocketPutRequest::abort ()
    {
        // Will cause I/O completion notification to be posted to the engine's
        // completion port and will be picked up later.
        myChannel.socket().cancel(myRequest.data());

        // TODO: is it useful for the application to know whether an operation
        //       was succesfully canceled or not?  They will receive a
        //       completion notification regardless...
    }

    bool SocketPutRequest::eof () const
    {
        return (myState == Dead);
    }

    void * SocketPutRequest::context () const
    {
        return (myRequest.context());
    }

}
