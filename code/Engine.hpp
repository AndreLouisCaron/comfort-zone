#ifndef _cz_Engine_hpp__
#define _cz_Engine_hpp__

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

#include <w32.hpp>
#include <w32.fs.hpp>
#include <w32.io.hpp>
#include <w32.ipc.hpp>
#include <w32.mt.hpp>
#include <w32.net.hpp>
#include <w32.tp.hpp>

#include "Hub.hpp"
#include "Request.hpp"

#include <list>

namespace cz {

    class BlockingReader;
    class BlockingWriter;
    class Channel;
    class Computation;
    class Engine;
    class Listener;
    class Reader;
    class Request;
    class Writer;

    // Async operations.
    class WaitRequest;
    class WorkRequest;

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
    friend class Request;

    // Async requests need access to the thread pool.
    friend class WaitRequest;
    friend class WorkRequest;

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
        Engine (Hub& hub);

        ~Engine ();

        /* methods. */
    private:
        /*!
         * @internal
         * @brief Have the hub unblock the task that initiated the request.
         * @param request The request to mark as completed.
         * @param data Pointer to any data concerned by the request, usually
         *  the address of a memory block concerned by an I/O request.
         * @param size Size of @a data, in bytes.
         *
         * This posts a notification to the I/O completion port so that the hub
         * has only a single place to check for completion of all types of
         * requests.  Use this at the end of an asynchronous request to unblock
         * the task that initiated @a request.
         */
        void complete_request (Request& request,
                               void * data=0,
                               w32::dword size=0);

    public:
        /*!
         * @internal
         */
        w32::io::CompletionPort& completion_port ();

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

        // Hub-friendly synchronous I/O.
        BlockingReader * standard_input ();
        BlockingWriter * standard_output ();
        BlockingWriter * standard_error ();
        // TODO: add anonymous pipes.

        // Hub-friendly asynchronous disk I/O.
        Reader * file_reader (const w32::string& path);
        Writer * file_writer (const w32::string& path);

        // Hub-friendly asynchronous network I/O.
        Listener * listen (w32::net::ipv4::EndPoint host);
        Channel * connect (w32::net::ipv4::EndPoint peer);
        Channel * connect (w32::net::ipv4::Address host,
                           w32::net::ipv4::EndPoint peer);
        Channel * connect (w32::net::ipv4::EndPoint host,
                           w32::net::ipv4::EndPoint peer);

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
         */
        void compute (Computation& computation);

        // Hub-friendly wait functions.
        void join (w32::ipc::Process process);
        void join (w32::ipc::Job job);
        void join (w32::mt::Thread thread);
        void acquire (w32::mt::Mutex mutex);
        void acquire (w32::mt::Semaphore semaphore);
        void await (w32::mt::Timer timer);
        void await (w32::mt::ManualResetEvent event);
        void await (w32::mt::AutoResetEvent event);
        void await (w32::fs::Changes changes);

    private:
        void wait (w32::Waitable waitable);

        /*!
         * @internal
         * @brief Common code to process a notification.
         *
         * @see wait_for_notification()
         * @see process_notification()
         */
        void process (w32::io::Notification notification);
    };


    /*!
     * @ingroup requests
     * @brief %Request to wait for waitable kernel objects.
     */
    class WaitRequest
    {
        /* data. */
    private:
        Request myRequest;
        w32::Waitable myWaitable;
        w32::tp::Wait myJob;

    public:
        WaitRequest (Engine& engine, w32::Waitable waitable);

        /* methods. */
    public:
        void start ();
        bool ready () const;
        void close ();
        void reset (); // call before calling `start()` again.
    };


    /*!
     * @ingroup requests
     * @brief %Request to execute CPU-intensive work in the background.
     */
    class WorkRequest
    {
        /* data. */
    private:
        Request myRequest;
        Computation& myComputation;
        w32::tp::Work myJob;

    public:
        WorkRequest (Engine& engine, Computation& computation);

        /* methods. */
    public:
        void start ();
        bool ready () const;
        void close ();
        void reset (); // call before calling `start()` again.
    };

}

#endif /* _cz_Engine_hpp__ */
