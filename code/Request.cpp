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

namespace cz {

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

    w32::tp::Timer::Callback Request::time_callback ()
    {
        return (w32::tp::Timer::method<Request,
                                       &Request::release_wait_client>());
    }

    Request::Request (Engine& engine, void * context)
        : myData()
        , myEngine(engine)
        , myNotification()
        , mySlave(&Hub::Slave::self())
        , myContext(context)
    {
        ::ZeroMemory(&myData, sizeof(myData));
        myData.request = this;

        cz_trace("+request(0x" << this << ")");
    }

    Request::~Request ()
    {
        cz_trace("-request(0x" << this << ")");

        // Make sure to clean this in case it gets used after it's been freed.
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
        cz_trace("*request(0x" << this << "): start");
        myData.Internal = STATUS_PENDING;

        // Re-assign slave in case the request is pooled for use by different
        // slaves at different points in its life cycle.
        mySlave = &Hub::Slave::self();
    }

    void Request::close ()
    {
        cz_trace("*request(0x" << this << "): close");
        myData.Internal = STATUS_WAIT_0;
    }

    bool Request::ready () const
    {
        cz_trace("*request(0x" << this << "): ready");
        return (HasOverlappedIoCompleted(&myData));
    }

    void Request::reset ()
    {
        cz_trace("*request(0x" << this << "): reset");
        ::ZeroMemory(&myData, sizeof(myData));
        myData.request = this;
    }

    void Request::reset (void * context)
    {
        cz_trace("*request(0x" << this << "): reset");
        ::ZeroMemory(&myData, sizeof(myData));
        myData.request = this;
        myContext = context;
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
        cz_trace(">request(0x" << this << ")");
        myEngine.complete_request(*this, &myEngine, size);
    }

    void Request::execute_computation (w32::tp::Hints& hints)
    {
        // Note: this callback executes in a thread from the thread pool.
        //       Since there is only one thread is writing to the `Request`
        //       object and the reading task is suspended, synchronization is
        //       implicit.

        // Note: the computation is require to guard against concurrent access
        //       to shared resources and cannot interact with the hub, so there
        //       is no additional synchronization to perform.

        cz_trace(" >> in work request callback.");

        Computation& computation = *static_cast<Computation*>(myContext);

        // Let the system know if we intend to block on this for a while.
        if (!computation.bounded()) {
            hints.may_run_long();
        }

        // Run computation.
        try {
            cz_trace(" >> executing work.");
            computation.execute();
        }
        catch (...) {
            cz_trace("?request(0x" << this << "): error during computaion.");
            // TODO: report error in completion notification!
        }

        // Mark the request as completed.  This ensures that the slave can
        // correctly use the `ready()` method to determine if this operation
        // (among others) has completed.
        close();

        // Send hub a completion notification.  This will unblock the hub and
        // allow it to resume the slave that initiated the wait request.
        report_completion();
    }

    void Request::release_wait_client (w32::tp::Hints& hints)
    {
        // Note: this callback executes in a thread from the thread pool.
        //       Since there is only one thread is writing to the `Request`
        //       object and the reading task is suspended, synchronization is
        //       implicit.

        cz_trace(" >> in wait request callback.");

        // Mark the request as completed.  This ensures that the slave can
        // correctly use the `ready()` method to determine if this operation
        // (among others) has completed.
        close();

        // Send hub a completion notification.  This will unblock the hub and
        // allow it to resume the slave that initiated the wait request.
        report_completion();
    }

}
