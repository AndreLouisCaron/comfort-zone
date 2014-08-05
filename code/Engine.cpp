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

#include "Engine.hpp"
#include "Computation.hpp"
#include "FileReader.hpp"
#include "FileWriter.hpp"
#include "Listener.hpp"
#include "SocketChannel.hpp"
#include "Promise.hpp"

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

    Hub& Engine::hub ()
    {
        return (myHub);
    }

    w32::io::CompletionPort& Engine::completion_port ()
    {
        return (myCompletionPort);
    }

    w32::tp::Queue& Engine::thread_pool_queue ()
    {
        return (myThreadPoolQueue);
    }

    void Engine::process (w32::io::Notification notification)
    {
        // Let's get paranoid and verify that the engine doesn't receive
        // notifications adressed to another engine.
        if (notification.handler<Engine>() != this) {
            cz_trace("?engine(0x" << this << "): received notification for engine(0x" << notification.handler<Engine>() << ").");

            ::DebugBreak();
            std::cerr
                << "WARNING: notification received by wrong engine!"
                << std::endl;
        }

        // Access the asynchronous request object.
        if (!notification.transfer()) {
            cz_trace("?engine(): empty notification!");

            ::DebugBreak();
            std::cerr
                << "WARNING: empty notification!"
                << std::endl;
            return;
        }
        Promise::Data * promise = static_cast<Promise::Data*>
            (&notification.transfer()->data());
        cz_trace("<promise(0x" << promise << ")");

        if (promise == 0) {
            cz_trace("?engine(): no promise reference!");

            ::DebugBreak();
            std::cerr
                << "WARNING: no promise reference!"
                << std::endl;
            return;
        }

        cz_debug_when(promise->state != Promise::Busy);

        // Whoever initiated the promise will want to recover these.
        promise->completion_result = notification.size();
        promise->completion_status = notification.status();

        // Post-process the asynchronous operation results, if necessary.
        if (promise->post_process) {
            (promise->post_process)(promise);
        }

        // OK, the promise has now offially been fulfilled.
        promise->state = Promise::Done;

        // If all promise objects for this asynchronous operation have gone out
        // of scope, the operation was holding the last reference to the
        // promise data, which now needs to be released.
        if (--(promise->references) == 0) {
            promise->cleanup(promise), promise = 0;  // "Virtual destructor".
        }
        else {
            // The fiber that was waiting for this notification can now resume,
            // reschedule it.
            myHub.schedule(*(promise->task->mySlave), promise);
        }
    }

    void Engine::wait_for_notification ()
    {
        cz_trace("$port(" << myCompletionPort.handle() << "): take");
        const w32::io::Notification notification = myCompletionPort.next();
        cz_trace("$port(" << myCompletionPort.handle() << "): took");
        process(notification);
    }

    bool Engine::process_notification ()
    {
        if (!myHub.running()) {
            throw (std::exception("Only the hub can process notifications!"));
        }

        // Wait for notification of asynchronous operation completion.
        w32::io::Notification notification = myCompletionPort.peek();

        // TODO: make a distinction between a timeout on the completion port
        //       wait operation and a timeout in a real asynchronous operation
        //       dispatched to an I/O device (... but timeout doesn't apply to
        //       asynchronous I/O operations!)
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

    struct ConnectPromise :
        public Promise::Data
    {
        w32::net::tcp::Stream socket;
        w32::net::tcp::Stream::ConnectEx connect_ex;
        w32::net::ipv4::EndPoint peer;
        w32::net::ipv4::EndPoint host;
        w32::dword sent;

        ConnectPromise (Engine& engine, Task& task,
                        w32::net::tcp::Stream socket,
                        const w32::net::ipv4::EndPoint& peer,
                        const w32::net::ipv4::EndPoint& host)
            : Promise::Data(engine, task)
            , socket(socket)
            , connect_ex(socket.connect_ex())
            , peer(peer)
            , host(host)
            , sent(0)
        {
            this->post_process = &ConnectPromise::recover_endpoints;
            this->cleanup = &destroy<ConnectPromise>;

            // For some strnage reason, the socket MUST be bound before using
            // `ConnectEx()`.  Bind it to whatever address was supplied by the
            // caller.
            const ::BOOL result = ::bind(
                this->socket.handle(),
                this->host.raw(),
                this->host.size()
            );
            if (result == SOCKET_ERROR) {
                const int error = ::WSAGetLastError();
                UNCHECKED_WIN32C_ERROR(bind, error);
            }
        }

        void start ()
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            ++(this->references);

            std::cout
                << peer << std::endl
                << host << std::endl;
            // Start the asynchronous connection request.
            const ::BOOL result = connect_ex(this->socket.handle(),
                                             this->peer.raw(),
                                             this->peer.size(),
                                             0, 0, // initial payload.
                                             &(this->sent),
                                             this);
            if (result == FALSE)
            {
                const ::DWORD error = ::GetLastError();
                if (error != ERROR_IO_PENDING) {
                    UNCHECKED_WIN32C_ERROR(ConnectEx, error);
                }
            }

            this->state = Promise::Busy;
        }

        bool abort ()
        {
            if (this->socket.cancel(*this)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<ConnectPromise*>(context)->abort());
        }

        // Called by hub when processing the completion notification.
        void recover_endpoints ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }

            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }

            // Patch socket for shutdown() and others to work as expected.
            const int result = ::setsockopt(this->socket.handle(), SOL_SOCKET,
                                            SO_UPDATE_CONNECT_CONTEXT, 0, 0);
            if (result == SOCKET_ERROR)
            {
                const int error = ::WSAGetLastError();
                UNCHECKED_WIN32C_ERROR(setsockopt, error);
            }
        }

        static void recover_endpoints (void * context) {
            static_cast<ConnectPromise*>(context)->recover_endpoints();
        }
    };

    Promise Engine::connect (w32::net::tcp::Stream stream,
                             w32::net::ipv4::EndPoint peer)
    {
        return (connect(stream, peer, w32::net::ipv4::Address::any()));
    }

    Promise Engine::connect (w32::net::tcp::Stream stream,
                             w32::net::ipv4::EndPoint peer,
                             w32::net::ipv4::Address host)
    {
        return (connect(stream, peer, w32::net::ipv4::EndPoint(host, 0)));
    }

    Promise Engine::connect (w32::net::tcp::Stream stream,
                             w32::net::ipv4::EndPoint peer,
                             w32::net::ipv4::EndPoint host)
    {
        Task& task = Task::current();
        ConnectPromise * promise
            = new ConnectPromise(*this, task, stream, peer, host);
        promise->start();
        return (Promise(promise));
    }

    struct AcceptPromise :
        public Promise::Data
    {
        static const size_t single_endpoint_size = sizeof(::sockaddr_in) + 16;
        static const size_t double_endpoint_size = 2 * single_endpoint_size;

        w32::net::tcp::Listener listener;
        w32::net::tcp::Stream stream;
        w32::net::tcp::Listener::AcceptEx accept_ex;
        w32::net::tcp::Listener::GetAcceptExSockAddrs get_accept_ex_sock_addrs;
        w32::net::ipv4::EndPoint host;
        w32::net::ipv4::EndPoint peer;
        w32::dword xferred;
        Buffer buffer;

        AcceptPromise (Engine& engine, Task& task,
                       w32::net::tcp::Listener listener,
                       w32::net::tcp::Stream stream)
            : Promise::Data(engine, task)
            , listener(listener)
            , stream(stream)
            , accept_ex(listener.accept_ex())
            , get_accept_ex_sock_addrs(listener.get_accept_ex_sock_addrs())
            , xferred(0)
            , buffer(double_endpoint_size)
        {
            this->post_process = &AcceptPromise::recover_endpoints;
            this->cleanup = &destroy<AcceptPromise>;
        }

        void start ()
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            ++(this->references);

            // Start the asynchronous connection request.
            const ::BOOL result = accept_ex(
                this->listener.handle(),
                this->stream.handle(),
                buffer.gbase(),
                buffer.gsize()-double_endpoint_size,  // Application data.
                single_endpoint_size,
                single_endpoint_size,
                &(this->xferred),
                this
            );
            if (result == FALSE)
            {
                const ::DWORD error = ::GetLastError();
                if (error != ERROR_IO_PENDING) {
                    UNCHECKED_WIN32C_ERROR(AcceptEx, error);
                }
            }
            // NOTE: even if AcceptEx() accepts a connection synchronously,
            //       we'll eventually receive a completion notification, so
            //       we'll wait for it to arrive before processing the results.
            this->state = Promise::Busy;
        }

        bool abort ()
        {
            if (this->listener.cancel(*this)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<AcceptPromise*>(context)->abort());
        }

        // Called by hub when processing the completion notification.
        void recover_endpoints ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }

            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }

            // Patch socket for shutdown() and others to work as expected.
            ::SOCKET parent = this->listener.handle();
            const int result = ::setsockopt(
                this->stream.handle(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<char*>(&parent), sizeof(parent)
            );
            if (result == SOCKET_ERROR)
            {
                const int error = ::WSAGetLastError();
                if (error == WSAENOTCONN) {
                    // This case seems to happen when the remote peer resets
                    // the connection before we have time to recover the
                    // endpoints.
                    //
                    // TODO: handle disconnection!
                }
                UNCHECKED_WIN32C_ERROR(setsockopt, error);
            }

            // Recover endpoints for established connection.
            //
            // NOTE: it might be difficult to remove the last
            //       `double_endpoint_size` bytes from the *end* of the array.
            //       maybe we'll need a `gtrim()` operation or something.
            //
            // NOTE: should be using WSAIoctl with
            //       SIO_GET_EXTENSION_FUNCTION_POINTER to obtain the pointer
            //       to this function.
            ::sockaddr * hdata = 0; int hsize = 0;
            ::sockaddr * pdata = 0; int psize = 0;
            get_accept_ex_sock_addrs(
                this->buffer.gbase(),
                this->buffer.gsize()-double_endpoint_size,
                single_endpoint_size,
                single_endpoint_size,
                &hdata, &hsize, &pdata, &psize
            );
            this->host = *reinterpret_cast<const ::sockaddr_in*>(hdata);
            this->peer = *reinterpret_cast<const ::sockaddr_in*>(pdata);
        }

        static void recover_endpoints (void * context) {
            static_cast<AcceptPromise*>(context)->recover_endpoints();
        }
    };

    Promise Engine::accept (w32::net::tcp::Listener listener,
                            w32::net::tcp::Stream stream)
    {
        Task& task = Task::current();
        AcceptPromise * promise =
            new AcceptPromise(*this, task, listener, stream);
        promise->start();
        return (Promise(promise));
    }

    struct GetPromise :
        public Promise::Data
    {
        w32::net::StreamSocket socket;
        w32::dword result;

        GetPromise (Engine& engine, Task& task,
                    w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
            , result(0)
        {
            this->post_process = &GetPromise::check_status;
            this->cleanup = &destroy<GetPromise>;
        }

        void start (void * data, size_t size)
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            ++(this->references);

            if (this->socket.get(data, size, *this, this->result)) {
                // The request object's contents MUST NOT be used, its contents
                // cannot be trusted because the call completed synchronously.
                //
                // NOTE: because a completion notification will be issued
                //       anyway, we must still wait for it to arrive before
                //       processing the result.
                ::ZeroMemory(this, sizeof(::OVERLAPPED));
            }
            this->state = Promise::Busy;
        }

        bool abort ()
        {
            if (this->socket.cancel(*this)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<GetPromise*>(context)->abort());
        }

        // Called by hub when processing the completion notification.
        void check_status ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }
            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }
            // Unless the call completed synchronously, use the transfer size
            // specified in the completion notification.
            if (this->result == 0) {
                this->result = this->completion_result;
            }
        }

        static void check_status (void * context) {
            return (static_cast<GetPromise*>(context)->check_status());
        }
    };

    Promise Engine::get (w32::net::StreamSocket stream,
                         void * data, size_t size)
    {
        Task& task = Task::current();
        GetPromise * promise = new GetPromise(*this, task, stream);
        promise->start(data, size);
        return (Promise(promise));
    }

    struct PutPromise :
        public Promise::Data
    {
        w32::net::StreamSocket socket;
        w32::dword result;

        PutPromise (Engine& engine, Task& task,
                    w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
            , result(0)
        {
            this->post_process = &PutPromise::check_status;
            this->cleanup = &destroy<PutPromise>;
        }

        void start (const void * data, size_t size)
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            ++(this->references);

            if (this->socket.put(data, size, *this, this->result)) {
                // The request object's contents MUST NOT be used, its contents
                // cannot be trusted because the call completed synchronously.
                //
                // NOTE: because a completion notification will be issued
                //       anyway, we must still wait for it to arrive before
                //       processing the result.
                ::ZeroMemory(this, sizeof(::OVERLAPPED));
            }
            this->state = Promise::Busy;
        }

        bool abort ()
        {
            if (this->socket.cancel(*this)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<PutPromise*>(context)->abort());
        }

        // Called by hub when processing the completion notification.
        void check_status ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }
            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }
            // Unless the call completed synchronously, use the transfer size
            // specified in the completion notification.
            if (this->result == 0) {
                this->result = this->completion_result;
            }
        }

        static void check_status (void * context) {
            return (static_cast<PutPromise*>(context)->check_status());
        }
    };

    Promise Engine::put (w32::net::StreamSocket stream,
                         const void * data, size_t size)
    {
        Task& task = Task::current();
        PutPromise * promise = new PutPromise(*this, task, stream);
        promise->start(data, size);
        return (Promise(promise));
    }

    struct DisconnectPromise :
        public Promise::Data
    {
        w32::net::StreamSocket socket;

        DisconnectPromise (Engine& engine, Task& task,
                           w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
        {
            this->post_process = &DisconnectPromise::check_status;
            this->cleanup = &destroy<DisconnectPromise>;
        }

        void start ()
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            ++(this->references);

            if (this->socket.disconnect(*this, true)) {
                // The request object's contents MUST NOT be used, its contents
                // cannot be trusted because the call completed synchronously.
                //
                // NOTE: because a completion notification will be issued
                //       anyway, we must still wait for it to arrive before
                //       processing the result.
                ::ZeroMemory(this, sizeof(::OVERLAPPED));
            }
            this->state = Promise::Busy;
        }

        bool abort ()
        {
            if (this->socket.cancel(*this)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<DisconnectPromise*>(context)->abort());
        }

        // Called by hub when processing the completion notification.
        void check_status ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }
            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }
        }

        static void check_status (void * context)
        {
            static_cast<DisconnectPromise*>(context)->check_status();
        }
    };

    Promise Engine::disconnect (w32::net::StreamSocket stream)
    {
        Task& task = Task::current();
        DisconnectPromise * data = new DisconnectPromise(*this, task, stream);
        data->start();
        return (Promise(data));
    }

    /*!
     * @internal
     * @brief Bookkeeping for promise to acquire kernel waitable object.
     *
     * Implements the opreation required to:
     * - acquire a mutex;
     * - decrement a semaphore;
     * - wait for an event to get signaled;
     * - wait for a timer to elapse;
     * - wait for a file change notification;
     * - join a thread;
     * - join a process;
     * - join a job; and
     * - wait for input on some streams (e.g. anonymous pipes).
     */
    struct WaitPromise :
        public Promise::Data
    {
        w32::Waitable topic;
        w32::tp::Wait watch;

        WaitPromise (Engine& engine, Task& task, w32::Waitable topic)
            : Promise::Data(engine, task)
            , topic(topic)
            , watch(engine.thread_pool_queue(), this,
                    w32::tp::Wait::function<&WaitPromise::unblock_hub>())
        {
            // TODO: use our own callback.

            // TODO: stop using abstraction that forces using "this" pointer in
            //       initialization list.  Use these directly instead:
            //       - CreateThreadpoolWait
            //       - SetThreadpoolWait
            this->cleanup = &destroy<WaitPromise>;
        }

        // NOTE: used to avoid the risk of having the callback execute prior to
        //       the object's constructor completing execution.
        void start ()
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            //
            // NOTE: in case of a successful synchronous cancellation attempt,
            //       we'll need to reduce this ourselves because no completion
            //       notification will be received.
            ++(this->references);

            // The request is now about to start!
            this->state = Promise::Busy;

            // Schedule wait in thread pool.  When the waitable object is
            // signaled, the `Request` object's `wait_callback` will post a
            // completion notification to the I/O completion port and the hub
            // will resume us.
            this->watch.watch(this->topic.handle());

            // NOTE: even if the system can technically preempt the current
            //       thread at this point and execute the entire computation
            //       before returning control to us, the computation ends up
            //       posting a notification into the completion port for the
            //       hub to process.
        }

        static void unblock_hub (w32::tp::Hints& hints, void * context)
        {
            // NOTE: this callback executes in a thread from the thread pool.
            //       Avoid manipulating any shared promise data to avoid
            //       synchronization issues.

            // Send hub a completion notification.  This will unblock the hub
            // and allow it to fulfill the promise and resume the task that
            // initiated the asynchronous operations.
            WaitPromise& data = *static_cast<WaitPromise*>(context);
            data.engine->completion_port().post(0, data.engine, &data);
        }
    };

    Promise Engine::acquire (w32::mt::Mutex mutex)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (mutex.acquire(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_result = 0;
            promise.myData->completion_status = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data = new WaitPromise(*this, task, mutex);
        data->start();
        return (Promise(data));
    }

    Promise Engine::acquire (w32::mt::Semaphore semaphore)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (semaphore.acquire(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_result = 0;
            promise.myData->completion_status = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data = new WaitPromise(*this, task, semaphore);
        data->start();
        return (Promise(data));
    }

    Promise Engine::join (w32::mt::Thread thread)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (thread.join(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = thread.status();
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), thread);
        data->start();
        return (Promise(data));
    }

    Promise Engine::join (w32::ipc::Process process)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (process.join(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = process.status();
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), process);
        data->start();
        return (Promise(data));
    }

    Promise Engine::join (w32::ipc::Job job)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (job.join(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), job);
        data->start();
        return (Promise(data));
    }

    Promise Engine::await (w32::mt::Timer timer)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (timer.elapsed()) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), timer);
        data->start();
        return (Promise(data));
    }

    Promise Engine::await (w32::mt::ManualResetEvent event)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (event.test()) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), event);
        data->start();
        return (Promise(data));
    }

    Promise Engine::await (w32::mt::AutoResetEvent event)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (event.test()) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), event);
        data->start();
        return (Promise(data));
    }

    Promise Engine::await (w32::fs::Changes changes)
    {
        Task& task = Task::current();

        // Try to fulfill the wait synchronously.
        if (changes.wait(w32::Timespan(0))) {
            Promise promise(*this, task);
            promise.myData->completion_status = 0;
            promise.myData->completion_result = 0;
            promise.myData->state = Promise::Done;
            return (promise);
        }

        // Operation would block, make it asynchronous.
        WaitPromise * data =
            new WaitPromise(*this, Task::current(), changes);
        data->start();
        return (Promise(data));
    }

    void Engine::wait_for (Promise& promise)
    {
        if (promise.myData == 0) {
            throw (std::exception("Invalid promise!"));
        }
        promise.myData->task = &Task::current();
        promise.myData->hint = 0;
        myHub.resume();
        cz_debug_when(promise.myData->state != Promise::Done);
    }

    void Engine::wait_for_all (Promise::Set& promises)
    {
        while (promises.done() < promises.size()) {
            wait_for_any(promises);
        }
    }

    void Engine::wait_for_any (Promise::Set& promises)
    {
        Task& task = Task::current();

        // Nothing to wait for!
        if (promises.done() == promises.size()) {
            return;
        }

        // Suspend the task until the next completion notification arrives.
        myHub.resume();

        cz_debug_when(task.mySlave == 0);
        cz_debug_when(task.mySlave->myLastPromise == 0);
        cz_debug_when(task.mySlave->myLastPromise->state != Promise::Done);

        // Let the watcher know that one promise has been fulfilled!
        promises.mark_as_fulfilled(task.mySlave->myLastPromise);
    }

    struct BackgroundWorkPromise :
        public Promise::Data
    {
        w32::tp::Work job;
        w32::dword(*execute)(w32::tp::Hints&, void*);

        BackgroundWorkPromise (Engine& engine, Task& task)
            : Promise::Data(engine, task)
            , job(engine.thread_pool_queue(), this,
                  w32::tp::Work::function<&BackgroundWorkPromise::entry_point>())
        {
            // TODO: stop using abstraction that forces using "this" pointer in
            //       initialization list.  Use these directly instead:
            //       - CreateThreadpoolWait
            //       - SetThreadpoolWait
            this->cleanup = &destroy<BackgroundWorkPromise>;
        }

        // NOTE: used to avoid the risk of having the callback execute prior to
        //       the object's constructor completing execution.
        void start ()
        {
            // Increase the reference count to prevent the promise data from
            // being deleted while the asynchronous operation is in progress.
            // Once the completion notification is received, the engine will
            // automatically reduce this reference count.
            //
            // NOTE: in case of a successful synchronous cancellation attempt,
            //       we'll need to reduce this ourselves because no completion
            //       notification will be received.
            ++(this->references);

            // The request is now about to start!
            this->state = Promise::Busy;

            // Schedule wait in thread pool.  When the waitable object is
            // signaled, the `Request` object's `wait_callback` will post a
            // completion notification to the I/O completion port and the hub
            // will resume us.
            this->job.submit();

            // NOTE: even if the system can technically preempt the current
            //       thread at this point and execute the entire computation
            //       before returning control to us, the computation ends up
            //       posting a notification into the completion port for the
            //       hub to process.
        }

        static void entry_point (w32::tp::Hints& hints, void * context)
        {
            // NOTE: this callback executes in a thread from the thread pool.
            //       Avoid manipulating any shared promise data to avoid
            //       synchronization issues.

            BackgroundWorkPromise& data =
                *static_cast<BackgroundWorkPromise*>(context);

            // NOTE: the computation is require to guard against concurrent
            //       access to shared resources and cannot interact with the
            //       hub, so there is no additional synchronization to perform.
            w32::dword result = MAXDWORD;
            w32::dword status = MAXDWORD;
            try {
                result = (*data.execute)(hints, context);
                status = 0;
            }
            catch (const w32::Error& error) {
                status = error.code();
            }
            catch (...) {
                cz_debug_when(true);
            }

            // Send hub a completion notification.  This will unblock the hub
            // and allow it to fulfill the promise and resume the task that
            // initiated the asynchronous operations.
            data.engine->completion_port().post(result, data.engine, &data);
        }
    };

    struct WorkPromise :
        public BackgroundWorkPromise
    {
        Computation& computation;

        WorkPromise (Engine& engine, Task& task, Computation& computation)
            : BackgroundWorkPromise(engine, task)
            , computation(computation)
        {
            this->cleanup = &destroy<WorkPromise>;
            this->execute = &WorkPromise::execute_computation;
        }

        static w32::dword execute_computation (w32::tp::Hints& hints,
                                               void * context)
        {
            WorkPromise& data = *static_cast<WorkPromise*>(context);

            // Let the system know if we intend to block on this for a while.
            if (!data.computation.bounded()) {
                hints.may_run_long();
            }

            // Do whatever we have to do in the background thread!
            //
            // TODO: forward return value!
            data.computation.execute();

            return (0);
        }
    };

    Promise Engine::execute (Computation& computation)
    {
        if (myHub.running()) {
            throw (std::exception("Can't start jobs from hub!"));
        }

        Task& task = Task::current();

        // Start the work asynchronously.
        WorkPromise * data = new WorkPromise(*this, task, computation);
        data->start();
        return (Promise(data));
    }

    struct BlockingGetPromise :
        public BackgroundWorkPromise
    {
        w32::io::InputStream stream;
        void * data;
        size_t size;

        BlockingGetPromise (Engine& engine, Task& task,
                            w32::io::InputStream stream,
                            void * data, size_t size)
            : BackgroundWorkPromise(engine, task)
            , stream(stream)
            , data(data)
            , size(size)
        {
            this->cleanup = &destroy<BlockingGetPromise>;
            this->execute = &BlockingGetPromise::perform_io;
        }

        bool abort ()
        {
            // NOTE: since we get no confirmation about whether the background
            //       thread was still blocked on the stream when we tried to
            //       cancel the operation, we cannot prevent receiving a
            //       completion request.  The easiest way to handle this is to
            //       let have the background thread send a completion in all
            //       cases, even when errors occur.  This shouldn't be a
            //       problem since cancellation is asynchronous for real async
            // I/O streams as well.
            this->stream.cancel();
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<BlockingGetPromise*>(context)->abort());
        }

        static w32::dword perform_io (w32::tp::Hints& hints, void * context)
        {
            BlockingGetPromise& promise =
                *static_cast<BlockingGetPromise*>(context);

            // Let the system know that we may block on this for a while.
            hints.may_run_long();

            // Do whatever we have to do in the background thread!
            //
            // TODO: handle cancellation?
            return (promise.stream.get(promise.data, promise.size));
        }
    };

    Promise Engine::get (w32::io::InputStream stream, void * data, size_t size)
    {
        Task& task = Task::current();
        BlockingGetPromise *const promise =
            new BlockingGetPromise(*this, task, stream, data, size);
        promise->start();
        return (Promise(promise));
    }

    struct BlockingPutPromise :
        public BackgroundWorkPromise
    {
        w32::io::OutputStream stream;
        const void * data;
        size_t size;

        BlockingPutPromise (Engine& engine, Task& task,
                            w32::io::OutputStream stream,
                            const void * data, size_t size)
            : BackgroundWorkPromise(engine, task)
            , stream(stream)
            , size(size)
            , data(data)
        {
            this->cleanup = &destroy<BlockingPutPromise>;
            this->execute = &BlockingPutPromise::perform_io;
        }

        bool abort ()
        {
            // NOTE: since we get no confirmation about whether the background
            //       thread was still blocked on the stream when we tried to
            //       cancel the operation, we cannot prevent receiving a
            //       completion request.  The easiest way to handle this is to
            //       let have the background thread send a completion in all
            //       cases, even when errors occur.  This shouldn't be a
            //       problem since cancellation is asynchronous for real async
            // I/O streams as well.
            this->stream.cancel();
            return (false);
        }

        static bool abort (void * context) {
            return (static_cast<BlockingGetPromise*>(context)->abort());
        }

        static w32::dword perform_io (w32::tp::Hints& hints, void * context)
        {
            BlockingPutPromise& promise =
                *static_cast<BlockingPutPromise*>(context);

            // Let the system know that we may block on this for a while.
            hints.may_run_long();

            // Do whatever we have to do in the background thread!
            //
            // TODO: handle cancellation?
            return (promise.stream.put(promise.data, promise.size));
        }
    };

    Promise Engine::put (w32::io::OutputStream stream,
                         const void * data, size_t size)
    {
        Task& task = Task::current();
        BlockingPutPromise *const promise =
            new BlockingPutPromise(*this, task, stream, data, size);
        promise->start();
        return (Promise(promise));
    }

}
