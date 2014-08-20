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

#include "Promise.hpp"

#include <w32.hpp>
#include <w32.mt.hpp>

#include <deque>
#include <exception>
#include <set>
#include <utility>

namespace cz {

    class Hub;
    class Task;
    class Engine;
    class Promise;

    /*!
     * @ingroup core
     * @brief Coroutine scheduler
     */
    class Hub
    {
    friend class Task;

        /* nested types. */
    public:
        class Slave;

    private:
        class Shutdown
            : public std::exception
        {
        public:
            virtual const char * what () const throw();
        };

        typedef std::deque< std::pair<Slave*,Promise::Data*> > SlaveQueue;

    public:
        /*!
         * @see spawn
         */
        enum SpawnMode {
            Deferred, // queue for later execution,
            Inactive, // "resume" manually.
            StartNow, // start before returning.
        };

        /*!
         * @internal
         * @brief Hub state.
         * @see shutdown
         */
        enum State {
            Running,
            Paused,
            Closing,
        };

        /* data. */
    private:
        w32::mt::Fiber myMaster;
        std::set<Slave*> mySlaves;

        // Holds slaves ready to resume.
        SlaveQueue myQueue;

        State myState;

        /* construction. */
    public:
        Hub ();
        ~Hub ();

        /* methods. */
    public:
        /*!
         * @brief Check if the hub's fiber is currently running.
         * @return @c true if the hub's fiber is running, @c false if one of
         *  the @c Task instances is running.
         *
         * @pre The method is being called from the thread in which the hub was
         *  created.
         */
        bool running () const;

        /*!
         * @brief Create a fiber that will run @a task.
         *
         * @param[in,out] task Application-defined task that will run in the
         *  hub.
         * @param[in] mode Allows the caller to control whether control should
         *  be immediately passed to the task, if @a task should be
         *  automatically created later or if @a task should initially be
         *  suspended (in that case, the application is expected to explicitly
         *  resume @a task later on).
         */
        void spawn (Task& task, SpawnMode mode=Deferred);

        /*!
         * @brief Count the slaves that have spawned and haven't yet finished.
         *
         * @return The number of slaves that have been spawned but that haven't
         *  yet completed their execution.
         */
        std::size_t active_slaves ();

        /*!
         * @brief Check if at least one slave is scheduled for execution.
         */
        bool slaves_pending () const;

        /*!
         * @brief Resume @e one currently pending slave.
         *
         * @note This is a no-op if there are no pending slaves.
         */
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

        /*!
         * @internal
         * @brief Schedule @a slave for later execution.
         *
         * @param[in,out] slave Task that should be scheduled.
         * @param[in,out] promise Promise issued to @a task and for which a
         *  completion notification was received.  When @a slave is resumed,
         *  it will effectively settle @a promise.
         *
         * @todo Change the @c Slave argument to a @c Task argument.
         */
        void schedule (Slave& slave, Promise::Data * promise=0);

        /*!
         * @internal
         * @brief Yield control to the hub.
         * @pre The caller is a slave of this hub.
         * @pre One or more asynchron ous requests have been started, or @c
         *  resume_later() has been called.
         *
         * @attention This function should only be called by the slaves after
         *  they initiate one or more asynchronous requests.
         */
        void resume ();

        /*!
         * @internal
         * @brief Force all slaves to end.
         */
        void shutdown ();

    private:
        /*!
         * @brief Check if @a slave has been spawned and hasn't yet completed.
         */
        bool exists (Slave * slave) const;

        /*!
         * @brief Mark @a slave as completed.
         */
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
    friend class Engine;  // TODO: remove this (merge engine & hub)!

        /* nested types. */
    private:
        class Online;

    public:
        /*!
         * @brief State of the task.
         */
        enum State {
            /// @brief Has not been started yet (no fiber is assigned).
            Offline,

            /// @brief Task has been started by a call to @c Hub::spawn.
            Started,

            /// @brief Task is running (implies that hub is paused).
            Running,

            /// @brief Task is paused (control was yielded to the hub).
            Paused,

            /// @brief Task has started shutting down (cannot be rescheduled).
            Closing,

            /// @brief Task has completed (no fiber is assigned).
            Dead,
        };

        /* data. */
    private:
        Hub::Slave * mySlave;
        State myState;

        /* construction. */
    protected:
        /*!
         * @brief Prepare the task for running later.
         */
        Task ();

    public:
        /*!
         * @brief Cleanup resources allocated by the task.
         */
        virtual ~Task ();

        /* methods. */
    private:
        /*!
         * @brief Assign @a slave to run the task.
         */
        void start (Hub::Slave * slave);

    public:
        /*!
         * @brief Check if the task has been started.
         *
         * @return @c true if the task has ever been started (stays true after
         *  the task completes).
         *
         * @see Hub::spawn
         */
        bool spawned () const;

        /*!
         * @brief Check if the task is running.
         *
         * @return @c true if the task is currently running (in which case, the
         *  hub is paused).
         *
         * @see Hub::resume
         */
        bool running () const;

        /*!
         * @brief Check if the task is paused.
         *
         * @return @c true if the task is currently paused (in which case, the
         *  hub is running @e or one of the other tasks is running).
         *
         * @see Hub::resume
         */
        bool paused () const;

        /*!
         * @brief Check if the task is shutting down.
         *
         * @return @c true if the task has started to shut down or if it has
         *  been requested to shut down.
         *
         * @see Task::run()
         * @see Hub::shutdown()
         */
        bool closing () const;

        /*!
         * @brief Check if the task has completed.
         *
         * @return @c true if the task has shut down (normally or by being
         *  cancelled).
         *
         * @see Task::run()
         * @see Hub::shutdown()
         */
        bool dead () const;

        /*!
         * @brief Obtain the task that is currently running.
         *
         * @return A reference to the task that is currently running.
         *
         * @pre The method is being called from the thread in which the hub was
         *  created.
         * @pre The hub is @e not running.
         */
        static Task& current ();

    protected:
        /*!
         * @brief Implement to define what the task will do.
         *
         * @pre The task is assigned a fiber and its state is set to
         *  @c Running for the first time.
         * @post The task is shut down.
         *
         * While this function is executing, its state may get set to
         * @c Closing in response to a request to @c Hub::shutdown().  In that
         * case, an exception will be thrown inside the task the next time it
         * is resumed (it may be forcibly resumed if it is currently
         * suspended).  Applications @e must not intercept this exception (in
         * particular, avoid all @c except(...) blocks inside tasks.
         */
        virtual void run () = 0;
    };

    /*!
     * @internal
     * @brief Exception-safe state change for tasks.
     */
    class Task::Online
    {
    private:
        Task& myTask;

    public:
        /*!
         * @brief Assign @a slave to @a task and set @a task to @c Running.
         */
        Online (Task& task, Hub::Slave& slave)
            : myTask(task)
        {
            myTask.mySlave = &slave;
            myTask.myState = Running;
        }

        /*!
         * @brief Set the task's state to @c Closing.
         */
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
    friend class Engine;  // TODO: merge hub & engine.

        /* class methods. */
    public:
        /*!
         * @internal
         * @brief Return currently executing slave.
         *
         * @warning Using this is usually a sign of bad design.  It should be
         *  used sparingly and as a last resort.
         *
         * @pre The method is being called from the thread in which the hub was
         *  created.
         * @pre The hub is @e not running.
         *
         * @return The slave that is currently running.
         */
        static Slave& self ();

        /* data. */
    private:
        Hub& myHub;
        Task& myTask;

        w32::mt::Fiber myFiber;

        // Set when the hub is resumed after a completion notification that
        // fulfills a promise is received.
        Promise::Data * myLastPromise;

        /* construction. */
    public:
        /*!
         * @brief Create a new fiber that will run @a task inside @a hub.
         *
         * @pre This method is called from the thread in which @a hub was
         *  created and @a hub is currently running.
         *
         * @param[in,out] hub Hub that will run @a task.
         * @param[in,out] task Task that will be run inside the slave.
         */
        Slave (Hub& hub, Task& task);

        /*!
         * @brief Delete the fiber and release resources it allocated.
         *
         * @attention If the task has not completed, the memory for the fiber's
         *  stack will be released, but the objects on the stack will not be
         *  "correctly" cleaned up, resulting in undefined behavior.  The hub
         *  shutdown process repeatedly resumes all spawned, but uncompleted
         *  slaves until they terminate.
         */
        ~Slave ();

        /* methods. */
    public:
        /*!
         * @brief Hub for which the slave is running.
         *
         * @return The hub that spawned the slave.
         */
        Hub& hub ();

        /*!
         * @brief Task that is being run inside the slave.
         *
         * @return The task for which the slave was spawned.
         */
        Task& task ();

        /*!
         * @internal
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
         * @param promise Pointer to the promise whose fulfillment caused the
         *  slave to be resumed.
         *
         * @attention This function should only be called by the Hub.
         */
        void resume (Promise::Data * promise=0);

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

        /*!
         * @internal
         * @brief Obtain the request for which the task was last resumed.
         *
         * When a completion notification is dequeued from the I/O completion
         * port, a reference to the promise that sent the completion
         * notification is stored in the slave so that the engine can settle
         * the promise.
         *
         * @return A pointer to the promise who'se settlement caused the task
         *  to be scheduled.  This is a null pointer, except for the short time
         *  during which the completion notification is being processed.
         */
        Promise::Data * last_promise () const;

    private:
        /*!
         * @internal
         * @brief Entry point for the fiber that hosts the slave.
         *
         * @param[in,out] context A pointer to the @c Slave for which the fiber
         *  was spawned.
         * @return This function must @e never return.  If it does, the thread
         *  in which the fiber is running will terminate, causing @e all fibers
         *  to be terminated immediately.
         */
        static void entry (w32::mt::Fiber::Context context);
    };

}

#endif /* _cz_Hub_hpp__ */
