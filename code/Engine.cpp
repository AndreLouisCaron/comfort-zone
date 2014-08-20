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
#include "Buffer.hpp"
#include "Computation.hpp"
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

    void Engine::bind (w32::net::tcp::Listener& listener)
    {
        myCompletionPort.bind(listener.handle(), this);
    }

    void Engine::bind (w32::net::tcp::Stream& stream)
    {
        myCompletionPort.bind(stream.handle(), this);
    }

    void Engine::bind (w32::io::InputFile& stream)
    {
        myCompletionPort.bind(stream.handle(), this);
    }

    void Engine::bind (w32::io::OutputFile& stream)
    {
        myCompletionPort.bind(stream.handle(), this);
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to establish an outbound TCP connection.
     *
     * @see Engine::connect
     * @see AcceptPromise
     *
     * @todo Support sending immediately after establishing the connection.
     */
    struct ConnectPromise :
        public Promise::Data
    {
        /// @brief Socket that should be completed.
        w32::net::tcp::Stream socket;

        /// @brief Pointer to the @c ConnectEx(...) function.
        ///
        /// The @c ConnectEx(...) function must be obtained dynamically.
        w32::net::tcp::Stream::ConnectEx connect_ex;

        /// @brief IP endpoint to which @c socket should be connected.
        w32::net::ipv4::EndPoint peer;

        /// @brief IP endpoint to which @c socket should be bound.
        w32::net::ipv4::EndPoint host;

        /// @brief Number of bytes sent as the initial payload.
        ///
        /// @attention Value is undefined until the promise is settled.
        w32::dword sent;

        /*!
         * @brief Prepare an async socket @c connect(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] socket
         * @param[in] peer
         * @param[in] host
         */
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
            this->cancel = &Promise::Data::abort<ConnectPromise>;

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

        /*!
         * @brief Start the asynchronous operation.
         */
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

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (even if the cancel is successful and synchronous,
         *  an I/O completion notification will be posted to the I/O completion
         *  port, and it's simpler to deal with that notification in the usual
         *  manner).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (ConnectPromise& promise)
        {
            if (promise.socket.cancel(promise)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        /*!
         * @brief Recover the host and peer IP endpoints.
         *
         * When using the asynchronous socket connect operation, the endpoints
         * are not automatically recovered, which causes the @c getsockname()
         * and @c getpeername() to return undefined results.  This
         * post-processing fixes them in case they are used by the application.
         *
         * @note This method is called by hub when processing the completion
         *  notification.
         *
         * @see recover_endpoints(void*)
         */
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

        /*!
         * @internal
         * @brief Trampoline function for C-style "virtual method".
         *
         * @param[in,out] context Pointer to the @c ConnectPromise.
         *
         * @see recover_endpoints()
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to establish an inbound TCP connection.
     *
     * @see Engine::accept
     * @see AcceptPromise
     *
     * @todo Support receiving while establishing the connection.  Requires
     *  the possibility to measure how long the connection has been
     *  established, etc. in order to be able to cancel it (to protect against
     *  an obvious D.o.S. attack).
     */
    struct AcceptPromise :
        public Promise::Data
    {
        /// @internal
        /// @brief Size of the application buffer reserved for a single IP
        ///  endpoint.
        ///
        /// @see double_endpoint_size
        static const size_t single_endpoint_size = sizeof(::sockaddr_in) + 16;

        /// @internal
        /// @brief Size of the application buffer reserved for both IP
        ///  endpoints.
        ///
        /// When using the @c AcceptEx(...) call, the IP endpoints are written
        /// into the application buffer.  This amount space needs to be
        /// reserved for use by the system.
        ///
        /// @see single_endpoint_size
        static const size_t double_endpoint_size = 2 * single_endpoint_size;

        /// @brief TCP listener that is listening for incoming TCP connections.
        w32::net::tcp::Listener listener;

        /// @brief Socket that will be used to exchange data with the client.
        w32::net::tcp::Stream stream;

        /// @brief Pointer to the @c AcceptEx(...) function.
        ///
        /// The @c AcceptEx(...) function must be obtained dynamically.
        w32::net::tcp::Listener::AcceptEx accept_ex;

        /// @brief Pointer to the @c GetAcceptExSockAddrs(...) function.
        ///
        /// The @c GetAcceptExSockAddrs(...) function must be obtained
        /// dynamically.
        w32::net::tcp::Listener::GetAcceptExSockAddrs get_accept_ex_sock_addrs;

        /// @brief IP endpoint to which @c stream should be bound.
        w32::net::ipv4::EndPoint host;

        /// @brief IP endpoint to which @c stream will be connected.
        w32::net::ipv4::EndPoint peer;

        /// @brief Amount of data received in the initial payload.
        w32::dword xferred;

        /// @brief Buffer into which the initial payload will be written.
        Buffer buffer;

        /*!
         * @brief Prepare an asynchronous socket @c accept(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] listener
         * @param[in,out] stream
         */
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
            this->cancel = &Promise::Data::abort<AcceptPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         */
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

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (even if the cancel is successful and synchronous,
         *  an I/O completion notification will be posted to the I/O completion
         *  port, and it's simpler to deal with that notification in the usual
         *  manner).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (AcceptPromise& promise)
        {
            if (promise.listener.cancel(promise)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        /*!
         * @brief Recover the host and peer IP endpoints.
         *
         * When using the asynchronous socket accept operation, the endpoints
         * are not automatically recovered, which causes the @c getsockname()
         * and @c getpeername() to return undefined results.  This
         * post-processing fixes them in case they are used by the application.
         *
         * @note This method is called by hub when processing the completion
         *  notification.
         *
         * @see recover_endpoints(void*)
         */
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

        /*!
         * @internal
         * @brief Trampoline function for C-style "virtual method".
         *
         * @param[in,out] context Pointer to the @c AcceptPromise.
         *
         * @see recover_endpoints()
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to read from a TCP socket.
     *
     * @see Engine::get
     * @see AcceptPromise
     * @see ConnectPromise
     * @see PutPromise
     * @see DisconnectPromise
     */
    struct GetPromise :
        public Promise::Data
    {
        /// @brief Socket from which data should be read.
        w32::net::StreamSocket socket;

        /// @brief Number of bytes read from @c socket.
        ///
        /// @attention Value is undefined until the promise is settled.
        w32::dword result;

        /*!
         * @brief Prepare an async socket @c recv(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] socket
         */
        GetPromise (Engine& engine, Task& task,
                    w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
            , result(0)
        {
            this->post_process = &GetPromise::check_status;
            this->cleanup = &destroy<GetPromise>;
            this->cancel = &Promise::Data::abort<GetPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         */
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

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (even if the cancel is successful and synchronous,
         *  an I/O completion notification will be posted to the I/O completion
         *  port, and it's simpler to deal with that notification in the usual
         *  manner).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (GetPromise& promise)
        {
            if (promise.socket.cancel(promise)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        /*!
         * @brief Check for error code in I/O completion notification.
         *
         * When using the asynchronous I/O operations, it's possible that the
         * I/O operation fails asynchronously, in which case an error code will
         * be reported in the I/O completion notification' status.
         *
         * @note This method is called by hub when processing the completion
         *  notification.
         *
         * @see check_status(void*)
         */
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

        /*!
         * @internal
         * @brief Trampoline function for C-style "virtual method".
         *
         * @param[in,out] context Pointer to the @c PutPromise.
         *
         * @see check_status()
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to write to a TCP socket.
     *
     * @see Engine::put
     * @see AcceptPromise
     * @see ConnectPromise
     * @see GetPromise
     * @see DisconnectPromise
     */
    struct PutPromise :
        public Promise::Data
    {
        /// @brief Socket to which data should be written.
        w32::net::StreamSocket socket;

        /// @brief Number of bytes read from @c socket.
        ///
        /// @attention Value is undefined until the promise is settled.
        w32::dword result;

        /*!
         * @brief Prepare an async socket @c send(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] socket
         */
        PutPromise (Engine& engine, Task& task,
                    w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
            , result(0)
        {
            this->post_process = &PutPromise::check_status;
            this->cleanup = &destroy<PutPromise>;
            this->cancel = &Promise::Data::abort<PutPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         */
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

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (even if the cancel is successful and synchronous,
         *  an I/O completion notification will be posted to the I/O completion
         *  port, and it's simpler to deal with that notification in the usual
         *  manner).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (PutPromise& promise)
        {
            if (promise.socket.cancel(promise)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        /*!
         * @brief Check for error code in I/O completion notification.
         *
         * When using the asynchronous I/O operations, it's possible that the
         * I/O operation fails asynchronously, in which case an error code will
         * be reported in the I/O completion notification' status.
         *
         * @note This method is called by hub when processing the completion
         *  notification.
         *
         * @see check_status(void*)
         */
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

        /*!
         * @internal
         * @brief Trampoline function for C-style "virtual method".
         *
         * @param[in,out] context Pointer to the @c PutPromise.
         *
         * @see check_status()
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to disconnect a TCP socket.
     *
     * @see Engine::disconnect
     * @see AcceptPromise
     * @see ConnectPromise
     * @see GetPromise
     * @see PutPromise
     */
    struct DisconnectPromise :
        public Promise::Data
    {
        /// @brief Socket that should be disconnected.
        w32::net::StreamSocket socket;

        /*!
         * @brief Prepare an async socket @c shutdown(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] socket
         */
        DisconnectPromise (Engine& engine, Task& task,
                           w32::net::StreamSocket socket)
            : Promise::Data(engine, task)
            , socket(socket)
        {
            this->post_process = &DisconnectPromise::check_status;
            this->cleanup = &destroy<DisconnectPromise>;
            this->cancel = &Promise::Data::abort<DisconnectPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         */
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

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (even if the cancel is successful and synchronous,
         *  an I/O completion notification will be posted to the I/O completion
         *  port, and it's simpler to deal with that notification in the usual
         *  manner).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (DisconnectPromise& promise)
        {
            if (promise.socket.cancel(promise)) {
                // Cancel was synchronous, but we'll still receive a
                // completion notification so don't fulfill the promise quite
                // yet.
            }
            return (false);
        }

        /*!
         * @brief Check for error code in I/O completion notification.
         *
         * When using the asynchronous I/O operations, it's possible that the
         * I/O operation fails asynchronously, in which case an error code will
         * be reported in the I/O completion notification' status.
         *
         * @note This method is called by hub when processing the completion
         *  notification.
         *
         * @see check_status(void*)
         */
        void check_status ()
        {
            if (this->completion_status == ERROR_OPERATION_ABORTED) {
                // TODO: handle cancellation!
            }
            if (this->completion_status != 0) {
                // TODO: handle unexpected errors!
            }
        }

        /*!
         * @internal
         * @brief Trampoline function for C-style "virtual method".
         *
         * @param[in,out] context Pointer to the @c DisconnectPromise.
         *
         * @see check_status()
         */
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
     * @ingroup promises
     * @brief Promise to wait for a kernel object to enter its signaled state.
     *
     * @attention For some objects (e.g. a mutex, a semaphore or an auto-reset
     *  event), a satisfied wait atomically modifies the state of the object.
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
        /// @brief Waitable kernel object to acquire/wait for.
        w32::Waitable topic;

        /// @brief Request to wait for @c topic in the thread pool.
        ///
        /// @attention Using this ensures thread pool threads multiplex waits
        ///  for kernal objects -- means a single thread can wait for up to
        ///  64 waitable kernel objects at once.
        w32::tp::Wait watch;

        /*!
         * @brief Prepare an async @c WaitForSingleObject(...) call.
         * @param[in,out] engine
         * @param[in,out] task
         * @param[in,out] topic
         */
        WaitPromise (Engine& engine, Task& task, w32::Waitable topic)
            : Promise::Data(engine, task)
            , topic(topic)
            , watch(thread_pool_queue(engine), this,
                    w32::tp::Wait::function<&WaitPromise::unblock_hub>())
        {
            // TODO: use our own callback.

            // TODO: stop using abstraction that forces using "this" pointer in
            //       initialization list.  Use these directly instead:
            //       - CreateThreadpoolWait
            //       - SetThreadpoolWait
            this->cleanup = &destroy<WaitPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         *
         * @note The operation is not started in the constructor to avoid the
         *  risk of having the callback execute prior to the object's
         *  constructor completing execution.
         */
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
            // signaled, the `Promise` object's `wait_callback` will post a
            // completion notification to the I/O completion port and the hub
            // will resume us.
            this->watch.watch(this->topic.handle());

            // NOTE: even if the system can technically preempt the current
            //       thread at this point and execute the entire computation
            //       before returning control to us, the computation ends up
            //       posting a notification into the completion port for the
            //       hub to process.
        }

        /*!
         * @brief Entry point for tasks that run in the thread pool.
         *
         * @param[in,out] hints Means for the task to provide feedback to the
         *  thread pool about what's going on.
         * @param[in,out] context Pointer to the @c WaitPromise.
         */
        static void unblock_hub (w32::tp::Hints& hints, void * context)
        {
            // NOTE: this callback executes in a thread from the thread pool.
            //       Avoid manipulating any shared promise data to avoid
            //       synchronization issues.

            // Send hub a completion notification.  This will unblock the hub
            // and allow it to fulfill the promise and resume the task that
            // initiated the asynchronous operations.
            WaitPromise& data = *static_cast<WaitPromise*>(context);
            completion_port(*data.engine).post(0, data.engine, &data);
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to perform an operation in a background thread.
     *
     * This is simply a base class for reuse-by-inheritance.
     *
     * @see WorkPromise
     * @see BlockingGetPromise
     * @see BlockingPutPromise
     */
    struct BackgroundWorkPromise :
        public Promise::Data
    {
        /// @brief Request to perform work in thread pool.
        ///
        /// @attention Using this ensures that we don't spawn a new thread for
        ///  each computation.  When a thread pool thread completes one
        /// operation, it will check the thead pool's queue for more work (or a
        /// shutdown signal).
        w32::tp::Work job;

        /// @brief "Virtual method" that performs the work.
        ///
        /// @attention This @e must be set by base classes.
        w32::dword(*execute)(w32::tp::Hints&, void*);

        /*!
         * @brief Prepare an async thread pool task execution.
         * @param[in,out] engine Engine to which the completion notification
         *  will be reported.
         * @param[in,out] task Task to which the promise was issued.
         */
        BackgroundWorkPromise (Engine& engine, Task& task)
            : Promise::Data(engine, task)
            , job(thread_pool_queue(engine), this,
                  w32::tp::Work::function<&BackgroundWorkPromise::entry_point>())
            , execute(0)
        {
            // TODO: stop using abstraction that forces using "this" pointer in
            //       initialization list.  Use these directly instead:
            //       - CreateThreadpoolWait
            //       - SetThreadpoolWait
            this->cleanup = &destroy<BackgroundWorkPromise>;
        }

        /*!
         * @brief Start the asynchronous operation.
         *
         * @note The operation is not started in the constructor to avoid the
         *  risk of having the callback execute prior to the object's
         *  constructor completing execution.
         */
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
            // signaled, the `Promise` object's `wait_callback` will post a
            // completion notification to the I/O completion port and the hub
            // will resume us.
            this->job.submit();

            // NOTE: even if the system can technically preempt the current
            //       thread at this point and execute the entire computation
            //       before returning control to us, the computation ends up
            //       posting a notification into the completion port for the
            //       hub to process.
        }

        /*!
         * @brief Entry point for tasks that run in the thread pool.
         *
         * @param[in,out] hints Means for the task to provide feedback to the
         *  thread pool about what's going on.
         * @param[in,out] context Pointer to the @c BackgroundWorkPromise.
         */
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
            completion_port(*data.engine).post(result, data.engine, &data);
        }
    };

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to perform a computation in a background thread.
     */
    struct WorkPromise :
        public BackgroundWorkPromise
    {
        /// @brief Functor that should be called in a thread pool thread.
        Computation& computation;

        /*!
         * @brief Prepare an async thread pool task execution.
         * @param[in,out] engine Engine to which the completion notification
         *  will be reported.
         * @param[in,out] task Task that requested the asynchronous operation.
         * @param[in,out] computation Functor to execute in the thread pool.
         */
        WorkPromise (Engine& engine, Task& task, Computation& computation)
            : BackgroundWorkPromise(engine, task)
            , computation(computation)
        {
            this->cleanup = &destroy<WorkPromise>;
            this->execute = &WorkPromise::execute_computation;
        }

        /*!
         * @brief Entry point for tasks that run in the thread pool.
         *
         * @param[in,out] hints Means for the task to provide feedback to the
         *  thread pool about what's going on.
         * @param[in,out] context Pointer to the @c WorkPromise.
         * @return The "completion result" that should be assigned to the
         *  promise when it is settled by the engine when receiving the
         * completion notification.
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to read from a (possibly) blocking input stream.
     *
     * Synchronous read is made asynchronous by reading in a background thread.
     * Hopefully, this doesn't block for too long (e.g. when reading from a file
     * opened for blocking I/O), but it may block for quite a while on a the standard input
     * or an anonymous pipe when the remote end is not being written to
     * quickly enough.  Hopefully, the application is not manipulating too many
     * blocking streams.
     */
    struct BlockingGetPromise :
        public BackgroundWorkPromise
    {
        /// @brief Non-overlapped stream from which to read data.
        w32::io::InputStream stream;

        /// @brief Application buffer into which data should be written.
        void * data;

        /// @brief Maximum number of bytes to write in @c data.
        size_t size;

        /*!
         * @brief Prepare an async @c ReadFile(...) call.
         *
         * @param[in,out] engine Engine to which the completion notification
         *  will be reported.
         * @param[in,out] task Task that requested the asynchronous operation.
         * @param[in,out] stream Non-overlapped input stream from which up to
         *  @a size bytes should be consumed.
         * @param[in,out] data Buffer into which up to @a size bytes will be
         *  written.
         * @param[in] size Maximum amount of data (in bytes) to consume from
         *  @a stream and write to @a data.
         */
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
            this->cancel = &Promise::Data::abort<BlockingGetPromise>;
        }

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (we don't know if a notification will be sent when
         *  cancelling the I/O because it may have already completed -- it's
         *  easier to force a notification in all cases and wait for that
         *  notifcation before completing the request).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (BlockingGetPromise& promise)
        {
            // NOTE: since we get no confirmation about whether the background
            //       thread was still blocked on the stream when we tried to
            //       cancel the operation, we cannot prevent receiving a
            //       completion request.  The easiest way to handle this is to
            //       let have the background thread send a completion in all
            //       cases, even when errors occur.  This shouldn't be a
            //       problem since cancellation is asynchronous for real async
            //       I/O streams as well.
            promise.stream.cancel();
            return (false);
        }

        /*!
         * @brief Entry point for tasks that run in the thread pool.
         *
         * @param[in,out] hints Means for the task to provide feedback to the
         *  thread pool about what's going on.
         * @param[in,out] context Pointer to the @c BlockingGetPromise.
         * @return The "completion result" that should be assigned to the
         *  promise when it is settled by the engine when receiving the
         * completion notification.
         */
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

    /*!
     * @internal
     * @ingroup promises
     * @brief Promise to write on a (possibly) blocking output stream.
     *
     * Synchronous write is made asynchronous by writing in a background thread.
     * Hopefully, this doesn't block for too long (e.g. when writing to the
     * standard output), but it may block for quite a while on an anonymous pipe
     * when the remote input buffer is full and its owner is not reading from it
     * quickly enough.  Hopefully, the application is not manipulating too many
     * blocking streams.
     */
    struct BlockingPutPromise :
        public BackgroundWorkPromise
    {
        /// @brief Non-overlapped stream to which data should be written.
        w32::io::OutputStream stream;

        /// @brief Application buffer containing data to write.
        const void * data;

        /// @brief Amount of data in the application buffer.
        size_t size;

        /*!
         * @brief Prepare an async @c WriteFile(...) call.
         *
         * @param[in,out] engine Engine to which the completion notification
         *  will be reported.
         * @param[in,out] task Task that requested the asynchronous operation.
         * @param[in,out] stream Non-overlapped input stream to which up to
         *  @a size bytes should be written.
         * @param[in] data Buffer from which up to @a size bytes should be
         *  read.
         * @param[in] size Maximum amount of data (in bytes) to read from
         *  @a data and write to @a stream.
         */
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
            this->cancel = &Promise::Data::abort<BlockingPutPromise>;
        }

        /*!
         * @brief Cancel the asynchronous operation.
         *
         * @param[in,out] promise Promise that should be cancelled.
         * @return @c false (we don't know if a notification will be sent when
         *  cancelling the I/O because it may have already completed -- it's
         *  easier to force a notification in all cases and wait for that
         *  notifcation before completing the request).
         *
         * @attention @a promise can be fulfilled despite your attempt to
         *  cancel it.  The reason is that there is a natural race condition:
         *  the asynchronous operation may have already completed and the I/O
         *  completion notification may already be queued in the I/O completion
         *  port.
         *
         * @note This is a trampoline function for C-style "virtual method".
         */
        static bool abort (BlockingPutPromise& promise)
        {
            // NOTE: since we get no confirmation about whether the background
            //       thread was still blocked on the stream when we tried to
            //       cancel the operation, we cannot prevent receiving a
            //       completion request.  The easiest way to handle this is to
            //       let have the background thread send a completion in all
            //       cases, even when errors occur.  This shouldn't be a
            //       problem since cancellation is asynchronous for real async
            // I/O streams as well.
            promise.stream.cancel();
            return (false);
        }

        /*!
         * @brief Entry point for tasks that run in the thread pool.
         *
         * @param[in,out] hints Means for the task to provide feedback to the
         *  thread pool about what's going on.
         * @param[in,out] context Pointer to the @c BlockingPutPromise.
         * @return The "completion result" that should be assigned to the
         *  promise when it is settled by the engine when receiving the
         * completion notification.
         */
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
