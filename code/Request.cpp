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

#include "Request.hpp"
#include "Engine.hpp"
#include "Computation.hpp"

#include "trace.hpp"

namespace fio {

    w32::tp::Work::Callback Request::work_callback ()
    {
        return (w32::tp::Work::method<Request,
                                      &Request::execute_computation>());
    }

    w32::tp::Wait::Callback Request::wait_callback ()
    {
        return (w32::tp::Wait::method<Request,
                                      &Request::release_wait_client>());
    }

    Request::Request (Engine& engine, void * context)
        : myData()
        , myEngine(engine)
        , myNotification()
        , mySlave(Hub::Slave::self())
        , myContext(context)
    {
        ::ZeroMemory(&myData, sizeof(myData));
        myData.request = this;

        fio_trace("+request(0x" << this << ")");
    }

    Request::~Request ()
    {
        fio_trace("-request(0x" << this << ")");

        myData.request = 0;
        ::ZeroMemory(&myData, sizeof(myData));
    }

    Engine& Request::engine ()
    {
        return (myEngine);
    }

    Request::Data& Request::data ()
    {
        return (myData);
    }

    void Request::start ()
    {
        fio_trace("*request(0x" << this << "): start");
        myData.Internal = STATUS_PENDING;
    }

    void Request::close ()
    {
        fio_trace("*request(0x" << this << "): close");
        myData.Internal = STATUS_WAIT_0;
    }

    bool Request::ready () const
    {
        fio_trace("*request(0x" << this << "): ready");
        return (HasOverlappedIoCompleted(&myData));
    }

    void Request::reset ()
    {
        fio_trace("*request(0x" << this << "): reset");
        ::ZeroMemory(&myData, sizeof(myData));
        myData.request = this;
    }

    const w32::io::Notification& Request::notification () const
    {
        return (myNotification);
    }

    void * Request::context () const
    {
        return (myContext);
    }

    void Request::report_completion (w32::dword size)
    {
        fio_trace("!request(0x" << this << ")");
        myEngine.complete_request(*this, &myEngine, size);
    }

    void Request::execute_computation (w32::tp::Hints& hints)
    {
        Computation& computation = *static_cast<Computation*>(myContext);

        // Let the system know if we intend to block on this for a while.
        if (!computation.bounded()) {
            hints.may_run_long();
        }

        // Run computation...
        try {
            computation.execute();
        }
        catch (...) {
            // TODO: report error in completion notification!
        }

        // Mark the request as completed.
        close();

        // Send hub a completion notification!
        report_completion();
    }

    void Request::release_wait_client (w32::tp::Hints& hints)
    {
        fio_trace("*request(0x" << this << ") completing request.");

        // Mark the request as completed.
        close();

        // Send hub a completion notification!
        report_completion();
    }

}
