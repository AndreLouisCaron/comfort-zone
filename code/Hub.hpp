#ifndef _cz_Hub_hpp__
#define _cz_Hub_hpp__

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
#include <w32.mt.hpp>

#include <deque>
#include <set>

namespace cz {

    class Hub;
    class Task;
    class Request;

    /*!
     * @ingroup core
     * @brief Coroutine scheduler
     */
    class Hub
    {
    friend class Task;
    friend class Request;

        /* nested types. */
    public:
        class Slave;

    private:
        typedef std::deque<Slave*> SlaveQueue;

    public:
        /*!
         * @see spawn
         */
        enum SpawnMode {
            Deferred, // queue for later execution,
            Inactive, // "resume" manually.
            StartNow, // start before returning.
        };

        /* data. */
    private:
        w32::mt::Fiber myMaster;
        std::set<Slave*> mySlaves;

        // Holds slaves ready to resume.
        SlaveQueue myQueue;

        /* construction. */
    public:
        Hub ();
        ~Hub ();

        /* methods. */
    public:
        bool running () const;

        // Create new task.
        void spawn (Task& task, SpawnMode mode=Deferred);

        // Query number of active slaves.
        std::size_t active_slaves ();

        // Dispatch work to scheduled slave.
        bool slaves_pending () const;
        void resume_pending_slave ();

        /*!
         * @brief Resumes all @e currently pending slaves.
         *
         * This will resume all pendings slaves one by one until the current
         * list of queued slaves is exhausted.  Any slaves queued during the
         * execution of these slaves will not be resumed as part of this
         * function's execution.
         *
         * @note This function's behavior is specified as such to prevent
         *  starving I/O tasks started through an @c Engine.  If the function
         *  simply executed until the queue became empty, a slave that simply
         *  @c paused() itself in a loop would starve all I/O tasks because it
         *  would re-queue itself before this function exited.
         */
        void resume_pending_slaves ();

        // Schedule slave for execution.
        // TODO: make this parameter a `Task`?
        void schedule (Slave& slave);

        /*!
         * @internal
         * @brief Yield control to the hub.
         *
         * @attention This function should only be called by the slaves.
         */
        void resume ();

    private:
        bool exists (Slave * slave) const;
        void forget (Slave * slave);
    };

    /*!
     * @ingroup core
     * @brief User task, run inside a coroutine.
     *
     * @todo Make this class not copyable.
     */
    class Task
    {
    friend class Hub;

        /* nested types. */
    private:
        class Online;

    public:
        enum State {
            Offline, // no fiber assigned yet.
            Started, // fiber assigned.
            Running, // fiber has run at least once.
            Paused,  // control yielded to master.
            Closing, // Task::run() has finished, can't be rescheduled.
            Dead,    // fiber has been terminated.
        };

        /* data. */
    private:
        Hub::Slave * mySlave;
        State myState;

        /* construction. */
    protected:
        Task ();

    public:
        virtual ~Task ();

        /* methods. */
    private:
        void start (Hub::Slave * slave);

    public:
        bool spawned () const;
        bool running () const;
        bool paused () const;
        bool closing () const;
        bool dead () const;

        // Yield to hub.
        void pause ();

    protected:
        // Actual code to run.
        virtual void run () = 0;
    };

    // Exception-safe state change for tasks.
    class Task::Online
    {
    private:
        Task& myTask;

    public:
        Online (Task& task, Hub::Slave& slave)
            : myTask(task)
        {
            myTask.mySlave = &slave;
            myTask.myState = Running;
        }

        ~Online () {
            myTask.myState = Closing;
        }
    };

    /*!
     * @ingroup core
     * @brief Coroutine assigned to a running Task.
     */
    class Hub::Slave
    {
    friend class Hub;

        /* class methods. */
    public:
        /*!
         * @internal
         * @brief Return currently executing slave.
         *
         * @warning Using this is usually a sign of bad design.  It should be
         *  used sparingly and as a last resort.
         */
        static Slave& self ();

        /* data. */
    private:
        Hub& myHub;
        Task& myTask;

        w32::mt::Fiber myFiber;

        /* construction. */
    public:
        Slave (Hub& hub, Task& task);
        ~Slave ();

        /* methods. */
    public:
        Hub& hub ();
        Task& task ();

        /*!
         * @brief Queue task in hub, yield control.
         *
         * @attention This function should only be called by the slave itself.
         *
         * @see resume_later()
         */
        void pause ();

        /*!
         * @internal
         * @brief Yield control to this slave.
         *
         * @attention This function should only be called by the Hub.
         */
        void resume ();

        /*!
         * @brief Queue task in hub, keep control.
         *
         * @attention This function should only be called by the hub or another
         *  slave.
         *
         * @see pause()
         * @todo Figure out if there are valid use cases for this function.
         */
        void resume_later ();

    private:
        static void entry (w32::mt::Fiber::Context context);
    };

}

#endif /* _cz_Hub_hpp__ */
