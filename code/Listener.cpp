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

#include "Listener.hpp"
#include "Engine.hpp"
#include "Request.hpp"
#include "SocketChannel.hpp"

#include "trace.hpp"

namespace cz {

    Listener::Listener (Engine& engine, w32::net::ipv4::EndPoint endpoint)
        : myEngine(engine)
        , mySocket(endpoint)
    {
        // Make sure we receive I/O completion notifications :-)
        cz_trace("  >> Binding listener to completion port.");
        myEngine.completion_port().bind(mySocket.handle(), &engine);
    }

    Listener::~Listener ()
    {
    }

    w32::net::tcp::Listener& Listener::socket ()
    {
        return (mySocket);
    }

#if 0
    SocketChannel * Listener::accept (void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // Start the asynchronous accept request.
        cz_trace(" >> Starting async accept request.");
        AcceptRequest request(myEngine, mySocket);
        request.start();

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        // Collect results.
        return request.result();
    }
#endif


    AcceptRequest::AcceptRequest (Engine& engine, w32::net::tcp::Listener& listener, SocketChannel& channel, Buffer& buffer, void * context)
        : myRequest(engine, context)
        , myListener(listener)
        , myChannel(channel)
        , myBuffer(buffer)
    {
    }

    w32::dword AcceptRequest::connected_since () const
    {
        ::DWORD data = 0;
        int size = sizeof(data);
        const int result = ::getsockopt(myChannel.socket().handle(),
                                        SOL_SOCKET, SO_CONNECT_TIME,
                                        reinterpret_cast<char*>(&data), &size);
        if (result == SOCKET_ERROR)
        {
            const ::DWORD error = ::GetLastError();
            UNCHECKED_WIN32C_ERROR(getsockopt, error);
        }
        return (data);
    }

    bool AcceptRequest::start ()
    {
        // Make sure we don't include any leftover data.
        myBuffer.pack();

        ::DWORD xferred = 0;
        const ::BOOL result = ::AcceptEx(
            myListener.handle(),
            myChannel.socket().handle(),
            myBuffer.gbase(),
            myBuffer.gsize()-double_endpoint_size, // extra buffer size.
            single_endpoint_size,
            single_endpoint_size,
            &xferred, &myRequest.data());
        if (result == FALSE)
        {
            const ::DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING) {
                return (true);
            }
            UNCHECKED_WIN32C_ERROR(AcceptEx, error);
        }
        return (false);
    }

    bool AcceptRequest::ready () const
    {
        return (myRequest.ready());
    }

    bool AcceptRequest::is (Request * request) const
    {
        return (request == &myRequest);
    }

    bool AcceptRequest::result ()
    {
        // Collect results!
        const w32::io::Notification notification = myRequest.notification();

        // Accept requests can be cancelled :-(
        if (notification.aborted()) {
            cz_trace(" >> Accept request cancelled.");
            return (false);
        }

        // Accept requests can be processed after the peer has already
        // disconnected :-(
        if (notification.disconnected()) {
            cz_trace(" >> Accept request too late.");
            return (false);
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        cz_trace(" >> Connected (got " << notification.size() << " bytes)!");

        // Recover connection end points.
        update_context();
        recover_endpoints(myHost, myPeer);

        // Consume accept data.
        myBuffer.gused(notification.size());

        cz_trace(" >> host: <tcp://" << myHost << ">.");
        cz_trace(" >> peer: <tcp://" << myPeer << ">.");
        cz_trace(" >> data: " << notification.size() << " bytes.");

        return (true);
    }

    const w32::net::ipv4::EndPoint& AcceptRequest::host () const
    {
        return (myHost);
    }

    const w32::net::ipv4::EndPoint& AcceptRequest::peer () const
    {
        return (myPeer);
    }

    Buffer& AcceptRequest::buffer ()
    {
        return (myBuffer);
    }

    void AcceptRequest::reset ()
    {
        myRequest.reset();
    }

    bool AcceptRequest::abort ()
    {
        const ::SOCKET socket = myListener.handle();
        const ::HANDLE handle = reinterpret_cast<::HANDLE>(socket);
        const ::BOOL result = ::CancelIoEx(handle, &myRequest.data());
        if (result == FALSE)
        {
            const ::DWORD error = ::GetLastError();
            // May have already completed.
            if (error == ERROR_NOT_FOUND) {
                return (false);
            }
            UNCHECKED_WIN32C_ERROR(CancelIoEx, error);
        }
        return (true);
    }

    void AcceptRequest::update_context ()
    {
        ::SOCKET parent = myListener.handle();
        const int level = SOL_SOCKET;
        const int option = SO_UPDATE_ACCEPT_CONTEXT;
        const int result = ::setsockopt
            (myChannel.socket().handle(), level, option,
             reinterpret_cast<char*>(&parent), sizeof(parent));
        if (result == SOCKET_ERROR) {
            cz_trace("Could not update context.");
        }
    }

    void AcceptRequest::recover_endpoints (w32::net::ipv4::EndPoint& host,
                                           w32::net::ipv4::EndPoint& peer)
    {
        ::sockaddr * hdata = 0; int hsize = 0;
        ::sockaddr * pdata = 0; int psize = 0;
        ::GetAcceptExSockaddrs(myBuffer.gbase(),
                               myBuffer.gsize()-double_endpoint_size,
                               single_endpoint_size,
                               single_endpoint_size,
                               &hdata, &hsize, &pdata, &psize);
        host = *reinterpret_cast<const ::sockaddr_in*>(hdata);
        peer = *reinterpret_cast<const ::sockaddr_in*>(pdata);
    }


    DisconnectRequest::DisconnectRequest (Engine& engine,
                                          SocketChannel& channel,
                                          void * context)
        : myRequest(engine, context)
        , myChannel(channel)
        , myState(Idle)
    {
    }

    void DisconnectRequest::start (bool reuse)
    {
        cz_trace("*request(0x" << &myRequest << "): disconnect");
        if (myChannel.socket().disconnect(myRequest.data(), true))
        {
            // The request object's contents MUST NOT be used, its contents
            // cannot be trusted because the call completed synchronously.
            myRequest.reset();
        }
        myState = Busy;
    }

    bool DisconnectRequest::ready () const
    {
        return ((myState == Busy) && myRequest.ready());
    }

    bool DisconnectRequest::is (Request * request) const
    {
        return (request == &myRequest);
    }

    void DisconnectRequest::result ()
    {
        // The hub has resumed us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        if (notification.disconnected()) {
            return; // Peer forced disconnection, close connection.
        }

        if (notification.aborted()) {
            return; // Was cancelled by application, close connection.
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();
     }

    void DisconnectRequest::reset ()
    {
        myRequest.reset();
        myState = Idle;
    }

    void DisconnectRequest::reset (void * context)
    {
        myRequest.reset(context);
        myState = Idle;
    }

    void DisconnectRequest::abort ()
    {
        // Will cause I/O completion notification to be posted to the engine's
        // completion port and will be picked up later.
        myChannel.socket().cancel(myRequest.data());

        // TODO: is it useful for the application to know whether an operation
        //       was succesfully canceled or not?  They will receive a
        //       completion notification regardless...
    }

    void * DisconnectRequest::context () const
    {
        return (myRequest.context());
    }

}
