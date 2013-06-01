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

}

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

    SocketChannel * Listener::accept (void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // TODO: finish wrapping this in a class for applications to issue multiple calls in the same task.

        // Start the asynchronous accept request.
        cz_trace(" >> Starting async accept request.");
        AcceptRequest request(myEngine, mySocket);
        request.start();

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        // Collect results.
        return request.close();
    }

    AcceptRequest::AcceptRequest (Engine& engine, w32::net::tcp::Listener& listener)
        : myRequest(engine)
        , myListener(listener)
        , myStream(::create_socket())
    {
    }

    void AcceptRequest::start ()
    {
        ::DWORD size = 0;
        const ::BOOL result = ::AcceptEx(
            myListener.handle(), myStream.handle(), myBuffer,
            0, // extra buffer size.
            single_endpoint_size, single_endpoint_size,
            &size, &myRequest.data());
        if (result == FALSE)
        {
            const ::DWORD error = ::GetLastError();
            if (error != ERROR_IO_PENDING) {
                UNCHECKED_WIN32C_ERROR(AcceptEx, error);
            }
        }
    }

    bool AcceptRequest::ready () const
    {
        return (myRequest.ready());
    }

    SocketChannel * AcceptRequest::close ()
    {
        // Collect results!
        const w32::io::Notification notification = myRequest.notification();

        // Accept requests can be cancelled :-(
        if (notification.aborted()) {
            cz_trace(" >> Accept request cancelled.");
            return (0);
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        cz_trace(" >> Connected!");

        // Recover connection end points.
        update_context();
        w32::net::ipv4::EndPoint host;
        w32::net::ipv4::EndPoint peer;
        recover_endpoints(host, peer);

        cz_trace(" >> Host: " << host);
        cz_trace(" >> Peer: " << peer);

        // Make sure we receive I/O completion notifications :-)
        myRequest.engine().completion_port().bind(myStream, &myRequest.engine());

        // Wrap the stream!
        return (new SocketChannel(myRequest.engine(), myStream, host, peer));
    }

    void AcceptRequest::abort ()
    {
        const ::SOCKET socket = myListener.handle();
        const ::HANDLE handle = reinterpret_cast<::HANDLE>(socket);
        const ::BOOL result = ::CancelIoEx(handle, &myRequest.data());
        if (result == FALSE)
        {
            const ::DWORD error = ::GetLastError();
            // Ignore request if transfer has already completed.
            if (error != ERROR_NOT_FOUND) {
                UNCHECKED_WIN32C_ERROR(CancelIoEx, error);
            }
        }
    }

    void AcceptRequest::update_context ()
    {
        ::SOCKET parent = myListener.handle();
        const int level = SOL_SOCKET;
        const int option = SO_UPDATE_ACCEPT_CONTEXT;
        const int result = ::setsockopt
            (myStream.handle(), level, option,
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
        ::GetAcceptExSockaddrs(myBuffer,
                               0, // extra buffer size.
                               single_endpoint_size,
                               single_endpoint_size,
                               &hdata, &hsize, &pdata, &psize);
        host = *reinterpret_cast<const ::sockaddr_in*>(hdata);
        peer = *reinterpret_cast<const ::sockaddr_in*>(pdata);
    }

}
