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

#include "FileReader.hpp"
#include "Engine.hpp"
#include "Hub.hpp" // TODO: include "Slave.hpp"

namespace fio {

    FileReader::FileReader (Engine& engine, const w32::string& path)
        : myEngine(engine)
        , myStream(w32::io::Stream::Builder()
                    .open_existing()
                    .generic_read()
                    .share_read()
                    .overlapped()
                    .sequential_scan()
                    .open(path))
    {
        // Make sure we receive I/O completion notifications :-)
        myEngine.completion_port().bind(myStream, &engine);
    }

    size_t FileReader::get (void * data, size_t size)
    {
        Hub::Slave& self = Hub::Slave::self();

        // TODO: set and update the file offset (overlapped operations don't
        //       have a built-in "file pointer" to track the current position).

        // Start the I/O operation.
        GetRequest request(myEngine, myStream);
        request.start(data, size);

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        self.hub().resume();

        return (request.close());
    }


    GetRequest::GetRequest (Engine& engine, w32::io::InputStream stream)
        : myRequest(engine)
        , myStream(stream)
    {
    }

    void GetRequest::start (void * data, size_t size)
    {
        if (!myStream.get(data, size, myRequest.data()))
        {
            // Failed!?
        }
    }

    bool GetRequest::ready () const
    {
        return (myRequest.ready());
    }

    size_t GetRequest::close ()
    {
        // The hub has resume us, collect results!
        const w32::io::Notification notification = myRequest.notification();

        // Propagate I/O exception to the caller if necessary.
        notification.report_error();

        // Let the caller know just how much data we received.
        return (notification.size());
    }

    void GetRequest::reset ()
    {
        myRequest.reset();
    }

}
