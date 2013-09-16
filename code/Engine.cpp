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

#include "Engine.hpp"
#include "BlockingReader.hpp"
#include "BlockingWriter.hpp"
#include "Computation.hpp"
#include "FileReader.hpp"
#include "FileWriter.hpp"
#include "Listener.hpp"
#include "SocketChannel.hpp"

#include "trace.hpp"

namespace cz {

    Engine::Engine (Hub& hub)
        : myHub(hub)
        , myCompletionPort()
        , myThreadPool()
        , myThreadPoolQueue(myThreadPool)
    {
        cz_trace("+engine(0x" << this << ")");
    }

    Engine::~Engine ()
    {
        cz_trace("-engine(0x" << this << ")");
    }

    w32::io::CompletionPort& Engine::completion_port ()
    {
        return (myCompletionPort);
    }

    void Engine::complete_request (Request& request,
                                   void * data, w32::dword size)
    {
        if (myHub.running()) {
            throw (std::exception("Hub can't complete requests!"));
        }

        myCompletionPort.post(size, data, &request.data());
    }

    void Engine::process (w32::io::Notification notification)
    {
        // Let's get paranoid and verify that the engine doesn't receive
        // notifications adressed to another engine.
        if (notification.handler<Engine>() != this) {
            cz_trace("?engine(0x" << this << "): received notification for engine(0x" << notification.handler<Engine>() << ").");
        }

        // Access the asynchronous request object.
        if (!notification.transfer()) {
            cz_trace("?engine(): empty notification!");
            // ... failed transfer, hub exiting, bug!?
        }
        Request *const request = static_cast<Request::Data*>
            (&notification.transfer()->data())->request;
        cz_trace(">request(0x" << request << ")");

        // Store the notification for use by the initiating fiber.
        request->myNotification = notification;

        // The fiber that was waiting for this notification can now resume,
        // reschedule it.
        myHub.schedule(request->mySlave);
    }

    void Engine::wait_for_notification ()
    {
        process(myCompletionPort.next());
    }

    bool Engine::process_notification ()
    {
        if (!myHub.running()) {
            throw (std::exception("Only the hub can process notifications!"));
        }

        // Wait for notification of asynchronous operation completion.
        w32::io::Notification notification = myCompletionPort.peek();
        if (notification.timeout()) {
            return (false);
        }

        process(notification);

        return (true);
    }

    void Engine::process_notifications ()
    {
        // TODO: redesign dispatch loop carefully.  When should we block on the
        //       I/O completion port?  When should we simply execute tasks in
        //       the pool?  How can we make sure that no fibers are starved and
        //       that no I/O streams are starved?

        while (process_notification())
            ;
    }

    BlockingReader * Engine::standard_input ()
    {
        return (new BlockingReader(*this, w32::io::StandardInput()));
    }

    BlockingWriter * Engine::standard_output ()
    {
        return (new BlockingWriter(*this, w32::io::StandardOutput()));
    }

    BlockingWriter * Engine::standard_error ()
    {
        return (new BlockingWriter(*this, w32::io::StandardError()));
    }

    Reader * Engine::file_reader (const w32::string& path)
    {
        return (new FileReader(*this, path));
    }

    Writer * Engine::file_writer (const w32::string& path)
    {
        return (new FileWriter(*this, path));
    }

    Listener * Engine::listen (w32::net::ipv4::EndPoint endpoint)
    {
        return (new Listener(*this, endpoint));
    }

    SocketChannel * Engine::connect (w32::net::ipv4::EndPoint peer)
    {
        return (Engine::connect(w32::net::ipv4::Address::any(), peer));
    }

    SocketChannel * Engine::connect (w32::net::ipv4::Address host,
                                     w32::net::ipv4::EndPoint peer)
    {
        return (Engine::connect(w32::net::ipv4::EndPoint(host, 0), peer));
    }

    SocketChannel * Engine::connect (w32::net::ipv4::EndPoint host,
                                     w32::net::ipv4::EndPoint peer)
    {
        ConnectRequest request(*this, host, peer);
        request.start();

        // Switch to master, it will resume us when it receives a completion
        // notification for the asynchronous I/O operation.
        myHub.resume();

        if (!request.ready()) {
            // TODO: log error.
        }

        // Collect results.
        return (request.close());
    }

    void Engine::join (w32::ipc::Process process)
    {
        wait(process);
    }

    void Engine::join (w32::ipc::Job job)
    {
        wait(job);
    }

    void Engine::join (w32::mt::Thread thread)
    {
        wait(thread);
    }

    void Engine::acquire (w32::mt::Mutex mutex)
    {
        wait(mutex);
    }

    void Engine::acquire (w32::mt::Semaphore semaphore)
    {
        wait(semaphore);
    }

    void Engine::await (w32::mt::Timer timer)
    {
        wait(timer);
    }

    void Engine::await (w32::mt::ManualResetEvent event)
    {
        wait(event);
    }

    void Engine::await (w32::mt::AutoResetEvent event)
    {
        wait(event);
    }

    void Engine::await (w32::fs::Changes changes)
    {
        wait(changes);
    }

    void Engine::wait (w32::Waitable waitable)
    {
        if (myHub.running()) {
            throw (std::exception("Can't start jobs from hub!"));
        }

        // Register for notification and schedule wait in thread pool.
        WaitRequest request(*this, waitable);
        request.start();

        // Switch to master, it will resume us when it receives a completion
        // notification from the worker thread.
        myHub.resume();

        // Process results & clean up.
        request.close();
    }

    void Engine::compute (Computation& computation)
    {
        if (myHub.running()) {
            throw (std::exception("Can't start jobs from hub!"));
        }

        // Register for notification and schedule work in thread pool.
        WorkRequest request(*this, computation);
        request.start();

        // Switch to master, it will resume us when it receives a completion
        // notification from the worker thread.
        myHub.resume();

        // Process results & clean up.
        request.close();
    }


    WorkRequest::WorkRequest (Engine& engine, Computation& computation)
        : myRequest(engine, &computation)
        , myComputation(computation)
        , myJob(engine.myThreadPoolQueue, &myRequest, Request::work_callback())
    {
    }

    void WorkRequest::start ()
    {
        // Schedule work in thread pool.  When the computation completes,
        // the `Request` object's `work_callback` will post a completion
        // notification to the I/O completion port and the hub will resume us.
        myJob.submit();

        // Mark the request as "in progress".
        myRequest.start();

        // Note: even if the system can technically preempt the current thread
        //       at this point and execute the entire computation before
        //       returning control to us, the computation ends up posting a
        //       notification into the completion port for the hub to process.
    }

    bool WorkRequest::ready () const
    {
        return (myRequest.ready());
    }

    void WorkRequest::close ()
    {
        if (!myRequest.ready())
        {
            cz_trace("?request(0x" << &myRequest << ") request incomplete.");
            // TODO: log possible bug.
            myJob.wait();
        }
        myRequest.reset();
    }


    WaitRequest::WaitRequest (Engine& engine, w32::Waitable waitable)
        : myRequest(engine)
        , myWaitable(waitable)
        , myJob(engine.myThreadPoolQueue, &myRequest, Request::wait_callback())
    {
    }

    void WaitRequest::start ()
    {
        // Schedule wait in thread pool.  When the waitable object is signaled,
        // the `Request` object's `wait_callback` will post a completion
        // notification to the I/O completion port and the hub will resume us.
        myJob.watch(myWaitable.handle());

        // Mark the request as "in progress".
        myRequest.start();

        // Note: even if the system can technically preempt the current thread
        //       at this point and execute the entire computation before
        //       returning control to us, the computation ends up posting a
        //       notification into the completion port for the hub to process.
    }

    bool WaitRequest::ready () const
    {
        return (myRequest.ready());
    }

    void WaitRequest::close ()
    {
        if (!myRequest.ready())
        {
            cz_trace("?request(0x" << &myRequest << ") request incomplete.");
            // TODO: log possible bug.
            myJob.wait();
        }
        myRequest.reset();
    }

}
