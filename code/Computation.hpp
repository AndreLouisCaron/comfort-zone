#ifndef _cz_Computation_hpp__
#define _cz_Computation_hpp__

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

namespace cz {

    class Engine;

    /*!
     * @brief Base class for asynchronous execution of CPU-bound tasks.
     *
     * @c Task instances execute in fibers and therefore share the same thread.
     * When you execute lengthy CPU-bound tasks directly in a @c Task instance,
     * you run the risk of preventing I/O bound (control) tasks from running.
     * The @c Engine::execute(Computation&) method allows you to schedule
     * execution of a CPU-bound task in a background thread, thereby preventing
     * this CPU-bound work from blocking I/O (control) tasks.
     *
     * @warning The @c Computation::execute() method runs in a "normal"
     *  background thread.  In contrast with sharing state between @c Task
     *  instances, sharing state between a @c Task and a @c Computation
     *  requires synchronization (remember to use @ Engine methods for
     *  synchronization in the @c Task and "normal" synchronization in the
     *  @c Computation).
     */
    class Computation
    {
        /* data. */
    private:
        const bool myBounded;

        /* construction. */
    protected:
        /*!
         * @brief Prepares a computation for execution.
         *
         * @param[in] bounded Should be @c true when the application can
         *  "guarantee" that the operation has a deterministic (and short) run
         *  time.
         *
         * The @c Engine uses a thread pool to execution CPU-bound application
         * computations.  This thread pool attempts to keep a fixed size, so
         * that it can queue incoming tasks and execute all of them in a fixed
         * number of threads.  However, it's possible that applications will
         * perform some operations which don't have a deterministic run time
         * (e.g. typically something that depends on an external resource).  In
         * those cases, the application should set @a bounded to @c false so
         * that the thread pool knows it can/should grow.
         *
         * @note Setting @a bounded to @c true gurantees that the thread pool
         *  will not grow only to accomodate this new computation.  However,
         *  setting it to @c false does not always result in spawning a new
         *  thread (the system may use an idle thread from the thread pool if
         *  it desires).
         *
         * @attention This class assumes the worst case (@a bounded is @c false
         *  by default) and lets specific tasks "optimize" as needed by setting
         *  @a bounded to @c true.
         */
        Computation (bool bounded=false);

    public:
        /*!
         * @brief Cleanup resources allocated by the computation.
         */
        virtual ~Computation ();

        /* methods. */
    public:
        /*!
         * @brief Check if the computation is known to be short.
         *
         * @return @c true if the task is known to have a deterministic run
         *  time.  If it doesn't, the system @e may attempt to start a
         *  dedicated background thread for the computation if none are
         *  available in the thread pool.
         */
        bool bounded () const;

        /*!
         * @brief Executes the application-defined computation.
         */
        virtual void execute () = 0;
    };

}

#endif /* _cz_Computation_hpp__ */
