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


#include "comfort-zone.hpp"


namespace {

    struct ThreadContext
    {
        w32::mt::ManualResetEvent ready;
        w32::mt::ManualResetEvent close;
    };

    w32::dword __stdcall background_thread (void * context)
    {
        ThreadContext& thread = *static_cast<ThreadContext*>(context);
        thread.ready.set();
        thread.close.wait();
        return (0);
    }

    /// @brief Task that joins a background thread.
    class JoinThread
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        w32::mt::Thread& myThread;

    public:
        JoinThread (cz::Engine& engine, w32::mt::Thread& thread)
            : myEngine(engine)
            , myThread(thread)
        {
        }

    private:
        virtual void run ()
        {
            // Start an asynchronous decrement.
            std::cout
                << "Obtaining promise."
                << std::endl;
            cz::Promise promise = myEngine.join(myThread);

            // Block until the promise is fulfilled.
            if (promise.state() != cz::Promise::Busy) {
                std::cerr
                    << "Promise already settled!"
                    << std::endl;
                cz_debug_when(true);
                return;
            }
            std::cout
                << "Waiting for promise to get fulfilled."
                << std::endl;
            myEngine.wait_for(promise);

            // Check promise state.
            std::cout << "Checking promise state." << std::endl;
            if (promise.state() != cz::Promise::Done) {
                std::cerr
                    << "Promise hasn't been fulfilled!"
                    << std::endl;
                cz_debug_when(true);
            }
        }
    };

    /// @test Joining a completed thread completes synchronously.
    /// @return Non-zero on test failure.
    int test_thread_synchronous_join ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);

        // Start a background thread and wait until we have a confirmation that
        // it's running.
        ThreadContext context;
        w32::mt::Thread thread(&background_thread, &context);
        context.ready.wait();

        // Start a background task that will join the thread.
        std::cout
            << "Spawning background task."
            << std::endl;
        JoinThread task(engine, thread);
        hub.spawn(task, cz::Hub::StartNow);

        // Make sure the task is paused, waiting for the promise fulfillment.
        if (!task.paused()) {
            std::cerr
                << "Task is not paused, as expected."
                << std::endl;
            cz_debug_when(true);
            return (EXIT_FAILURE);
        }
        if (engine.process_notification()) {
            std::cerr
                << "Processed unexpected notification."
                << std::endl;
            cz_debug_when(true);
            return (EXIT_FAILURE);
        }

        // Allow the thread to complete.  This will cause the asynchronous wait
        // to be satisfied, a completion notification to be posted, and the the
        // promise to be fulfilled.
        context.close.set();
        std::cout
            << "Waiting for thread to complete."
            << std::endl;
        thread.join();
        if (thread.status() != 0) {
            std::cerr
                << "Thread did not exit cleanly!"
                << std::endl;
            cz_debug_when(true);
            return (EXIT_FAILURE);
        }

        // When the promise is fulfilled, the task's `wait_for(promise)` will
        // return, allowing it to complete normally.
        std::cout
            << "Waiting for completion notification."
            << std::endl;
        engine.wait_for_notification();
        hub.resume_pending_slave();
        if (!task.dead()) {
            std::cerr
                << "Task is not dead!"
                << std::endl;
            cz_debug_when(true);
            return (EXIT_FAILURE);
        }

        // Make sure the hub closes successfully to avoid suprises when
        // destroying it.
        std::cout
            << "Shutting hub down."
            << std::endl;
        hub.shutdown();

        // Eveything should all be well and good by now :-)
        std::cout
            << "Test passed!"
            << std::endl;
        return (EXIT_SUCCESS);
    }

}


#include <w32/app/console-program.hpp>


namespace {

    int run (int, wchar_t **) {
        return (::test_thread_synchronous_join());
    }

}


#include <w32/app/console-program.cpp>
