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

#include "FileWriter.hpp"
#include "Engine.hpp"
#include "Hub.hpp" // TODO: include "Slave.hpp"
#include "trace.hpp"

namespace cz {

    FileWriter::FileWriter (Engine& engine, const w32::string& path)
        : myEngine(engine)
        , myStream(w32::io::Stream::Builder()
                   .create_always()
                   .generic_write()
                   .overlapped()
                   .open(path))
    {
        // Make sure we receive I/O completion notifications :-)
        myEngine.completion_port().bind(myStream, &engine);
    }

    size_t FileWriter::put (const void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // TODO: set and update the file offset (overlapped operations don't
        //       have a built-in "file pointer" to track the current position).

        // Start the I/O operation.
        PutRequest request(myEngine, myStream);
        request.start(data, size);

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        return request.result();
    }


    PutRequest::PutRequest (Engine& engine, w32::io::OutputStream stream, void * context)
        : myRequest(engine, context)
        , myStream(stream)
        , myState(Idle)
        , myResult(0)
    {
    }

    void PutRequest::start (const void * data, size_t size)
    {
        cz_trace("*request(0x" << &myRequest << "): start");
        if (myStream.put(data, size, myRequest.data(), myResult))
        {
            // The request object's contents MUST NOT be used, its contents
            // cannot be trusted because the call completed synchronously.
            myRequest.reset();
        }
        myState = Busy;
    }

    bool PutRequest::ready () const
    {
        return ((myState == Busy) && myRequest.ready());
    }

    bool PutRequest::is (Request * request) const
    {
        return (request == &myRequest);
    }

    size_t PutRequest::result ()
    {
        // The hub has resume us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        if (notification.disconnected()) {
            return (0); // Peer forced disconnection, close connection.
        }

        if (notification.aborted()) {
            return (0); // Was cancelled by application, close connection.
        }

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        // Let the caller know just how much data we send.
        return ((myResult > 0)? myResult : notification.size());
 }

    void PutRequest::reset ()
    {
        myRequest.reset();
        myResult = 0;
        myState = Idle;
    }

    void PutRequest::reset (void * context)
    {
        myRequest.reset(context);
        myResult = 0;
        myState = Idle;
    }

    void * PutRequest::context () const
    {
        return (myRequest.context());
    }

}
