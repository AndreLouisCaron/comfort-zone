#ifndef _cz_Engine_hpp__
#define _cz_Engine_hpp__

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

#include <w32.hpp>
#include <w32.fs.hpp>
#include <w32.io.hpp>
#include <w32.ipc.hpp>
#include <w32.mt.hpp>
#include <w32.net.hpp>
#include <w32.tp.hpp>

#include "Hub.hpp"
#include "Promise.hpp"

#include <list>

namespace cz {

    class Channel;
    class Computation;
    class Engine;
    class Listener;
    class Reader;
    class SocketChannel;
    class Writer;
    class Buffer;

    /*!
     * @ingroup core
     * @brief Hub, enhanced with I/O capabilities.
     *
     * The typical way to use this function is to call it in a loop until
     * the application should exit, like so:
     * @code
     *  int main (int, char **)
     *  {
     *    cz::Hub hub;
     *    cz::Engine engine(hub);
     *
     *    // Start initial fibers (sockets listeners, etc.).
     *    // ...
     *
     *    while (application_running())
     *    {
     *      // Process all available notifications.  This has
     *      // the effect of queueing slaves for execution.
     *      engine.process_notifications();
     *
     *      // If we don't have any work to do, wait for a
     *      // notification rather than spin needlessly.
     *      if (!hub.slaves_pending()) {
     *        engine.wait_for_notification(); continue;
     *      }
     *
     *      // Execute all currently queued work.  Any slaves queued
     *      // during this function's execution will not be considered
     *      // for execution until this function is called again (this
     *      // prevents starving I/O tasks).
     *      hub.resume_pending_slaves();
     *    }
     *  }
     * @endcode
     */
    class Engine
    {
    // Async requests need to post to the completion port.
    friend class Promise;

        /* data. */
    private:
        Hub& myHub;

        // Queue for notifications from asynchronous I/O operations.
        w32::io::CompletionPort myCompletionPort;

        // Thread pool for background work, synchronous I/O, timers, etc.
        w32::tp::Pool myThreadPool;
        w32::tp::Queue myThreadPoolQueue;

        /* construction. */
    public:
        /*!
         * @brief Create an engine that will perform I/O for tasks in @a hub.
         *
         * @param[in,out] hub Hub that runs tasks that will issue I/O requests
         * to the engine.
         */
        Engine (Hub& hub);

        /*!
         * @brief Cleanup acquired resources.
         *
         * This process will shut down the internal thread pool and close the
         * I/O completion port that receives completion notifications signaling
         * the settlement of promises issued by the engine.
         *
         * @pre All promises issued by @c *this have been settled.
         */
        ~Engine ();

        /* methods. */
    public:
        /*!
         * @internal
         * @brief Hub that runs tasks to which promises are issued by @c *this.
         *
         * @return The reference to the hub that runs tasks to which promises
         *  are issued by @c *this.
         */
        Hub& hub ();

        /*!
         * @brief Bind @a listener to the engine's I/O completion port.
         *
         * Binding @a listener is required in order to receive completion
         * notifications that signal the settlement of promises issued by the
         * engine.
         *
         * @pre @a listener has been opened for overlapped I/O.
         *
         * @param[in,out] listener TCP listener that should be bound to
         *  @c *this.
         *
         * @see accept
         */
        void bind (w32::net::tcp::Listener& listener);

        /*!
         * @brief Bind @a listener to the engine's I/O completion port.
         *
         * Binding @a stream is required in order to receive completion
         * notifications that signal the settlement of promises issued by the
         * engine.
         *
         * @pre @a stream has been opened for overlapped I/O.
         *
         * @param[in,out] stream TCP socket that should be bound to @c *this.
         */
        void bind (w32::net::tcp::Stream& stream);

        /*!
         * @brief Bind @a stream to the engine's I/O completion port.
         *
         * Binding @a stream is required in order to receive completion
         * notifications that signal the settlement of promises issued by the
         * engine.
         *
         * @pre @a stream has been opened for overlapped I/O.
         *
         * @param[in,out] stream File that should be bound to @ *this.
         */
        void bind (w32::io::InputFile& stream);

        /*!
         * @brief Bind @a stream to the engine's I/O completion port.
         *
         * Binding @a stream is required in order to receive completion
         * notifications that signal the settlement of promises issued by the
         * engine.
         *
         * @pre @a stream has been opened for overlapped I/O.
         *
         * @param[in,out] stream File that should be bound to @ *this.
         */
        void bind (w32::io::OutputFile& stream);

        /*!
         * @brief Process an asynchronous operation completion notification.
         *
         * This blocks on the I/O completion port for completions notifications
         * for asynchronous I/O operations and any other operation executed via
         * the thread pool.  When a notification becomes available, the fiber
         * that initiated the request attached to the notification will be
         * resumed to collect results.
         *
         * This is probably the only function that safely blocks all fibers in
         * the hub.  It can safely block because, by design, it is only called
         * when all other fibers are paused and waiting for completion
         * notifications.
         *
         * @note This can only be called by the hub's main fiber.
         *
         * @see process_notification()
         */
        void wait_for_notification ();

        /*!
         * @brief Non-blocking version of @c wait_for_notification().
         * @return @c true if a notification was processed.
         *
         * This function can be called in a loop to process all available
         * notifications in single pass like so:
         * @code
         *  while (engine.process_notification())
         *    ;
         * @endcode
         *
         * @note This can only be called by the hub's main fiber.
         *
         * @see process_notifications()
         */
        bool process_notification ();

        /*!
         * @brief Process all available completion notifications.
         *
         * @note This can only be called by the hub's main fiber.
         *
         * @see process_notification()
         */
        void process_notifications ();

        /*!
         * @brief Read from blocking input stream.
         *
         * This method turns a synchronous read operation into an asynchronous
         * promise to obtain data by performing the blocking read operation on
         * a background thread (in the engine's thread pool).  This is mainly
         * intended to make reading from the standard input and anonymous pipes
         * possible in a task.
         *
         * When it's possible to obtain an input stream that supports
         * asynchronous I/O and reads from the same data source, you should
         * attempt that instead as it will use fewer system resources (there is
         * no need to use one thread per stream in that case).
         *
         * @attention A successful read that returns 0 bytes indicates that the
         *  "end of file" condition has been reached and that there is no more
         *  data to read from @a stream.
         *
         * @note You should never pass a stream that has been configured for
         *  non-blocking synchronous I/O because it brings no benefit and may
         *  yield strange results (such as a 0-byte read regardless of the "end
         *  of file" condition).
         *
         * @param[in,out] stream Input stream to read from.
         * @param[out] data Buffer into which data read from @a stream will be
         *  copied.  This buffer will receive up to @a size bytes.
         * @param[in] size Maximum amount of data (in bytes) to read into
         *  @a data from @a stream.
         * @return A promise to read up to @a size bytes from @ stream into
         *  @a data.  This promise will be fulfilled when @a stream has
         *  available data.
         */
        Promise get (w32::io::InputStream stream, void * data, size_t size);

        /*!
         * @brief Write to a blocking output stream.
         *
         * This method turns a synchronous write operation into an asynchronous
         * promise to write data by performing the blocking write operation on
         * a background thread (in the engine's thread pool).  This is mainly
         * intended to make writing to the standard output, the standard error
         * and anonymous pipes possible in a task.
         *
         * When it's possible to obtain an output stream that supports
         * asynchronous I/O and writes to the same data source, you should
         * attempt that instead as it will use fewer system resources (there is
         * no need to use one thread per stream in that case).
         *
         * @attention A successful write that returns 0 bytes indicates that
         *  the "end of file" condition has been reached and that the
         *  application cannot write more data to @a stream.
         *
         * @note You should never pass a stream that has been configured for
         *  non-blocking synchronous I/O because it brings no benefit and may
         *  yield strange results (such as a 0-byte write regardless of the
         *  "end of file" condition).
         *
         * @param[in,out] stream Output stream to write to.
         * @param[out] data Buffer from which data should be taken when writing
         *  to @a stream.  Up to @a size bytes will be copied into this buffer.
         * @param[in] size Maximum amount of data (in bytes) to read from
         *  @a data and written to @a stream.
         * @return A promise to write up to @a size bytes from @a data into
         *  @a stream into.  This promise will be fulfilled when at least 1
         *  byte is written into @a stream or @a stream has reached its "end of
         *  file" state.
         */
        Promise put (w32::io::OutputStream stream,
                     const void * data, size_t size);

        /*!
         * @brief Read from a file stream opened for asynchronous I/O.
         *
         * @pre @a stream has been opened for asynchronous I/O.
         *
         * @param[in,out] stream Input stream to read from.
         * @param[out] data Buffer into which data read from @a stream will be
         *  copied.  This buffer will receive up to @a size bytes.
         * @param[in] size Maximum amount of data (in bytes) to read into
         *  @a data from @a stream.
         * @return A promise to read up to @a size bytes from @ stream into
         *  @a data.  This promise will be fulfilled when @a stream has
         *  available data.
         */
        Promise get (w32::io::InputFile stream, void * data, size_t size);

        /*!
         * @brief Write to a file stream opened for asynchronous I/O.
         *
         * @pre @a stream has been opened for asynchronous I/O.
         *
         * @param[in,out] stream Output stream to write to.
         * @param[out] data Buffer from which data should be taken when writing
         *  to @a stream.  Up to @a size bytes will be copied into this buffer.
         * @param[in] size Maximum amount of data (in bytes) to read from
         *  @a data and written to @a stream.
         * @return A promise to write up to @a size bytes from @a data into
         *  @a stream into.  This promise will be fulfilled when at least 1
         *  byte is written into @a stream or @a stream has reached its "end of
         *  file" state.
         */
        Promise put (w32::io::OutputFile stream,
                     const void * data, size_t size);

        /*!
         * @brief Connect to a TCP server.
         *
         * This is the simplest of all @c connect(...) overloads.  It binds
         * @a stream to any IP address on the machine and automatically selects
         * a random port.
         *
         * @param[in,out] stream Socket over which data will be exchanged with
         *  the TCP server.
         * @param[in] peer IP address of the server to which @a stream should
         *  be connected.
         * @return A promise to establish a TCP connection with @a peer.
         *  This promise will be fulfilled as soon as @a stream is connected.
         *
         * @see accept
         * @see disconnect
         */
        Promise connect (w32::net::tcp::Stream stream,
                         w32::net::ipv4::EndPoint peer);

        /*!
         * @brief Connect to a TCP server.
         *
         * In addition to connecting @a stream to @a peer, this overload allows
         * you to select the IP address to which @a stream should be bound.
         * This allows you to use a specific network card when the host
         * computer has more than one.  A random port is automatically selected
         * when binding @a stream.
         *
         * @param[in,out] stream Socket over which data will be exchanged with
         *  the TCP server.
         * @param[in] peer IP address of the server to which @a stream should
         *  be connected.
         * @param[in] host IP address to which @a stream should be bound.
         * @return A promise to establish a TCP connection with @a peer.
         *  This promise will be fulfilled as soon as @a stream is connected.
         *
         * @see accept
         * @see disconnect
         */
        Promise connect (w32::net::tcp::Stream stream,
                         w32::net::ipv4::EndPoint peer,
                         w32::net::ipv4::Address host);

        /*!
         * @brief Connect to a TCP server.
         *
         * In addition to connecting @a stream to @a peer, this overload allows
         * you to select the IP address and port to which @a stream should be
         * bound.  This allows you to use a specific network card, as well as a
         * pre-determined port number when binding @a stream.
         *
         * @param[in,out] stream Socket over which data will be exchanged with
         *  the TCP server.
         * @param[in] peer IP address of the server to which @a stream should
         *  be connected.
         * @param[in] host IP address to which @a stream should be bound.
         * @return A promise to establish a TCP connection with @a peer.
         *  This promise will be fulfilled as soon as @a stream is connected.
         *
         * @see accept
         * @see disconnect
         */
        Promise connect (w32::net::tcp::Stream stream,
                         w32::net::ipv4::EndPoint peer,
                         w32::net::ipv4::EndPoint host);

        /*!
         * @brief Connect to a TCP server.
         *
         * In contrast to other overloads, this one provides all options
         * combined: the ability to select the IP address and port to which
         * @a stream will be bound, and the ability to send the data in
         * @a buffer while establishing the connection.
         *
         * @param[in,out] stream Socket over which data will be exchanged with
         *  the TCP server.
         * @param[in] peer IP address of the server to which @a stream should
         *  be connected.
         * @param[in] host IP address to which @a stream should be bound.
         * @param[in,out] buffer Data to send to the server as soon as the
         *  connection is established.
         * @return A promise to establish a TCP connection with @a peer.
         *  This promise will be fulfilled when at least 1 byte is written into
         *  @a buffer.
         *
         * @see accept
         * @see disconnect
         */
        Promise connect (w32::net::tcp::Stream stream,
                         w32::net::ipv4::EndPoint peer,
                         w32::net::ipv4::EndPoint host, Buffer& buffer);

        /*!
         * @brief Wait for a connection from a TCP client.
         *
         * @pre @a listener has been bound to @c *this.
         * @pre @a stream has been bound to @c *this.
         *
         * @param[in,out] listener Listening socket that has been bound and is
         *  in the "listening" state.
         * @param[in,out] stream Socket that will be used to transfer data to
         *  and from the peer after the connection is established.
         * @return A promise to establish a TCP connection when @a listener
         *  receives a TCP handshake.  This promise will be fulfilled as soon
         *  as the connection is established.
         *
         * @see accept
         * @see disconnect
         */
        Promise accept (w32::net::tcp::Listener listener,
                        w32::net::tcp::Stream stream);

        /*!
         * @brief Wait for a connection from a TCP client.
         *
         * @attention When using a buffer to receive an initial payload, the
         *  promise will not be fulfilled until the client sends at least 1
         *  byte of data.  This exposes the client to an easy denial of service
         *  attack that is quite subtle to handle correctly: it requires using
         *  multiple parallel accept requests and polling them to detect
         *  inactive connections.
         *
         * @pre @a listener has been bound to the engine.
         *
         * @param[in,out] listener TCP "server" socket that is listening for
         *  incoming connections.
         * @param[in,out] stream TCP "client" socket that will be connected
         *  when this promise fulfills.
         * @param[in,out] buffer Buffer that will receive data sent by the
         *  client when connection is established.
         * @return A promise to establish a TCP connection when @a listener
         *  receives a TCP handshake.  This promise will be fulfilled when at
         *  least 1 byte is received over @a stream and written to @a buffer.
         *
         * @see bind(w32::net::tcp::Listener&)
         * @see connect
         * @see get(w32::net::StreamSocket,void*,size_t)
         * @see put(w32::net::StreamSocket,const void*,size_t)
         * @see disconnect
         */
        Promise accept (w32::net::tcp::Listener listener,
                        w32::net::tcp::Stream stream, Buffer& buffer);

        /*!
         * @brief Read data from a TCP socket.
         *
         * @pre @a stream has been bound to the engine.
         *
         * @param[in,out] stream Input stream to read from.
         * @param[out] data Buffer into which data read from @a stream will be
         *  copied.  This buffer will receive up to @a size bytes.
         * @param[in] size Maximum amount of data (in bytes) to read into
         *  @a data from @a stream.
         * @return A promise to read up to @a size bytes from @ stream into
         *  @a data.  This promise will be fulfilled when @a stream has
         *  available data.
         *
         * @see bind(w32::net::StreamSocket&)
         * @see accept
         * @see connect
         * @see put(w32::net::StreamSocket,const void*,size_t)
         * @see disconnect
         */
        Promise get (w32::net::StreamSocket stream, void * data, size_t size);

        /*!
         * @brief Write data to a TCP socket.
         *
         * @param[in,out] stream Output stream to write to.
         * @param[out] data Buffer from which data should be taken when writing
         *  to @a stream.  Up to @a size bytes will be copied into this buffer.
         * @param[in] size Maximum amount of data (in bytes) to read from
         *  @a data and written to @a stream.
         * @return A promise to write up to @a size bytes from @a data into
         *  @a stream into.  This promise will be fulfilled when at least 1
         *  byte is written into @a stream or @a stream has reached its "end of
         *  file" state.
         *
         * @see accept
         * @see connect
         * @see get(w32::net::StreamSocket,void*,size_t)
         * @see disconnect
         */
        Promise put (w32::net::StreamSocket stream,
                     const void * data, size_t size);

        /*!
         * @brief Disconnect a socket and prepare it for reuse.
         *
         * Once the promise is fulfilled, the socket may be reused in @c accept
         * or @c connect calls to process a new connection.
         *
         * @attention When the peer performs an unclean shutdown sequence (e.g.
         *  because of a crash), the socket is subject to the @c TIME_WAIT
         *  state.  Because it is not safe to reuse the socket while it is in
         *  this state, the promise will not be fulfilled until this state
         *  changes.  In a default system configuration, this may take up to 4
         *  minutes.  While this may seem excessive, it prevents accidentally
         *  catching data from the wrong connection and corrupting the data
         *  stream (see RFC 793).
         *
         * @pre @a stream has been bound to the engine.
         * @pre @a stream is connected.
         * @pre Linger has been configured on @a stream (the system default of
         *  indefinite timeout applies unless explicitly overriden).
         *
         * @param[in,out] stream Socket that should be disconnected.
         * @return A promise to disconnect @a stream.  The promise will be
         *  fulfilled when all data has been flushed (subject to linger) and
         *  the shutdown sequence has completed.
         *
         * @see accept
         * @see connect
         */
        Promise disconnect (w32::net::StreamSocket stream);

        /*!
         * @brief Acquire the mutual-exclusion lock.
         *
         * This method returns a promise to acquire @a mutex.  The
         * returned promise will be fulfilled when another thread releases
         * @a mutex.
         *
         * @param[in,out] mutex The mutual-exclusion lock to acquire.
         * @return A promise to acquire @a mutex.  This promise may be
         *  fulfilled synchronously, so check the promise state when it is
         *  returned.
         */
        Promise acquire (w32::mt::Mutex mutex);

        /*!
         * @brief Decrement @a semaphore by 1.
         *
         * This method returns a promise to decrement @a semaphore.  The
         * returned promise will be fulfilled when another thread increments
         * @a semaphore.
         *
         * @param[in,out] semaphore The semaphore that you wish to decrement.
         * @return A promise to decrement @a semaphore.  This promise may
         *  fulfill synchronously, so check the promise state when it is
         *  returned.
         */
        Promise acquire (w32::mt::Semaphore semaphore);

        /*!
         * @brief Wait for @a thread to complete its execution.
         *
         * This method returns a promise to observe the completion of @a
         * thread.  The returned promise will be fulfilled when the thread's
         * execution completes.  This may happen in several circumstances:
         * 1) the thread's entry point returns to the system;
         * 2) the thread calls @c ExitThread(); or
         * 3) another thread kills the thead using @c TerminateThread().
         *
         * @param[in] thread The thread whose completion you are interested in.
         * @return A promise to notify you when the thread completes its
         *  execution.  This promise may fulfill synchronously, so check the
         *  promise state when it is returned.
         */
        Promise join (w32::mt::Thread thread);

        /*!
         * @brief Wait for @a process to complete its execution.
         *
         * This method returns a promise to observe the completion of @a
         * process.  The returned promise will be fulfilled when the process'
         * execution completes.  This may happen in several circumstances:
         * 1) the process' entry point returns to the system;
         * 2) the process calls @c ExitProcess(); or
         * 3) another process kills the process using @c TerminateProcess().
         *
         * @param[in] process The process whose completion you are interested
         *  in.
         * @return A promise to notify you when the process completes its
         *  execution.  This promise may fulfill synchronously, so check the
         *  promise state when it is returned.
         */
        Promise join (w32::ipc::Process process);

        /*!
         * @brief Wait for all processes in @a job to complete their execution.
         *
         * This method returns a promise to observe the completion of all
         * processes in @a job.  The returned promise will be fulfilled when
         * all the processes' execution complete.  Each process may complete in
         * several circumstances:
         * 1) the process' entry point returns to the system;
         * 2) the process calls @c ExitProcess(); or
         * 3) another process kills the process using @c TerminateProcess().
         *
         * The promise will also complete if the job is terminated by killing
         * all processes with a single call to @c TerminateJobObject().
         *
         * @param[in] job The job whose completion you are interested in.
         * @return A promise to notify you when the process completes its
         *  execution.  This promise may fulfill synchronously, so check the
         *  promise state when it is returned.
         */
        Promise join (w32::ipc::Job job);

        /*!
         * @brief Wait for @a timer to elapse.
         *
         * This method returns a promise to observe @a timer elapsing.  The
         * return promise will be fulfilled when enough time passes.
         *
         * @param[in] timer The thread you are interested in watching.
         * @return A promise to notify you when the timer elapses.  This
         *  promise may be fulfilled synchronously, so check the promise state
         *  when it is returned.
         */
        Promise await (w32::mt::Timer timer);

        /*!
         * @brief Wait for @a event to get signaled.
         *
         * This method returns a promise to observe @a event getting signaled.
         * The return promise will be fulfilled when another thread signals
         * @a event.
         *
         * @param[in] event The thread you are interested in watching.
         * @return A promise to notify you when the event is signaled.  This
         *  promise may be fulfilled synchronously, so check the promise state
         *  when it is returned.
         *
         * @attention While manual reset events seems simple, the are rather
         *  frequently used incorrectly because there is no control and there
         *  are no guarantees over which waiting thread(s) will be resumed when
         *  signaling the event.  The only recommended usage is as marker that
         *  some @e permanent state transition has occured (e.g. server
         *  shutdown request has been received).
         */
        Promise await (w32::mt::ManualResetEvent event);

        /*!
         * @brief Wait for @a event to get signaled.
         *
         * This method returns a promise to observe @a event getting signaled.
         * The return promise will be fulfilled when another thread signals
         * @a event.
         *
         * @param[in] event The thread you are interested in watching.
         * @return A promise to notify you when the event is signaled.  This
         *  promise may be fulfilled synchronously, so check the promise state
         *  when it is returned.
         *
         * @attention In contrast with a manual-reset event, an auto-reset
         *  event's state is cleared as soon as a single wait is satisfied.
         *  However, they are still somewhat clumsy to use since the signaler
         *  has no way to determine whether another thread has "consumed" the
         *  signal and if it is safe to signal the event again.
         */
        Promise await (w32::mt::AutoResetEvent event);

        /*!
         * @brief Wait until a file-system change is detected by @a changes.
         *
         * This method returns a promise to observe file-system changes.  The
         * returned promise will be fulfilled when any process performs
         * file-system changes that can be detected by the registered filter.
         *
         * @attention The caller is responsible for calling @c changes.next()
         *  between asynchronous wait operations.
         *
         * @param[in] changes The file-system change watcher.
         * @return A promise to notify you when the changes are detected.  This
         *  promise @e cannot be fulfilled synchronously.
         */
        Promise await (w32::fs::Changes changes);

        /*!
         * @brief Have @a computation execute in a background thread.
         *
         * The work is executed asynchronously (without blocking other fibers
         * in the hub), but this function returns synchronously.  Use this
         * function to execute any operation that would block other fibers for
         * too long or that require thread afinity.  You may also use this to
         * specifically have work executed on another processor (core) rather
         * than in the thread where all the hub's fibers are running.
         *
         * @note This cannot be called by the hub's main fiber.
         *
         * @param[in,out] computation Work to be done in the background thread.
         *  The @a computation object is shared state.  It should not be
         *  accessed until the promise is fulfilled.
         * @return A promise to execute @c computation.run() in a background
         *  thread.  The promise can @e never be fulfilled synchronously.
         */
        Promise execute (Computation& computation);

        /*!
         * @brief Block the current task until @a promise is fulfilled.
         *
         * @param[in] promise Promise that must be fulfilled in order for the
         *  current task to continue its execution.
         *
         * @post @a promise is either fulfilled or broken.
         *
         * @see wait_for_any
         * @see wait_for_all
         */
        void wait_for (Promise& promise);

        /*!
         * @brief Block the current task until one of @a promises is fulfilled.
         *
         * In a scatter-gather parallel computing model, this is a "gather"
         * operation that allows you to process individual results as they
         * become available (and possibly issue additional parallel operations
         * in response).
         *
         * @param[in,out] promises A set of promises from which one must be
         *  fulfilled in order for the current task to continue its execution.
         *
         * @post @c promises.next() will yield a promise that is either
         *  fulfilled or broken.
         *
         * @see wait_for
         * @see wait_for_all
         */
        void wait_for_any (Promise::Set& promises);

        /*!
         * @brief Block the current task until all of @a promises are fulfilled.
         *
         * In a scatter-gather parallel computing model, this is the "gather"
         * operation that collects results for all parallel operations.
         *
         * @param[in,out] promises A set of promises which must all be
         *  fulfilled in order for the current task to continue its execution.
         *
         * @post All promises in @a promises are either fulfilled or broken.
         *
         * @see wait_for
         * @see wait_for_any
         */
        void wait_for_all (Promise::Set& promises);

    private:
        /*!
         * @internal
         * @brief Common code to process a notification.
         *
         * @see wait_for_notification()
         * @see process_notification()
         */
        void process (w32::io::Notification notification);
    };


#if 0
    /*!
     * @ingroup requests
     * @brief %Request to wait for a waitable timer to expire.
     */
    class TimeRequest
    {
        /* data. */
    private:
        Request myRequest;
        w32::tp::Timer myJob;
        w32::dword myDelai;

    public:
        TimeRequest (Engine& engine, w32::dword milliseconds, void * context=0);

        /* methods. */
    public:
        bool is (const Request * request) const;

        void start ();

        /*!
         * @brief Attempt to cancel the timer.
         * @return @c true if the timer had already expired, else @c false.
         *
         * @attention If the timer has already expired, a completion
         *  notification has already been posted to the completion port (we
         *  can't do anything about this) and the application should prepare to
         *  process it.  Always check the return value!
         *
         * @code
         *  TimeRequest request(...);
         *  request.start();
         *
         *  // ... some time later ...
         *
         *  if (!request.abort()) {
         *    // Timer has already expired, notification will arrive despite
         *    // your attempt to cancel it.  Application should behave as if
         *    // 
         *  }
         * @endcode
         *
         * @see ready
         */
        bool abort ();

        /*!
         * @internal
         * @brief Called from thread pool to unblock the hub.
         * @post @c ready() returns @c true.
         */
        void close ();

        bool ready () const;
        void reset (); // call before calling `start()` again.
    };
#endif

}

#endif /* _cz_Engine_hpp__ */
