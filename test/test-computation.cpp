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

    class TestComputation :
        public cz::Computation
    {
    private:
        w32::mt::ManualResetEvent myReady;
        w32::mt::ManualResetEvent myClose;

    public:
        TestComputation ()
            : myReady()
            , myClose()
        {
        }

        w32::mt::ManualResetEvent& ready () {
            return (myReady);
        }

        w32::mt::ManualResetEvent& close () {
            return (myClose);
        }

        virtual void execute ()
        {
            // Let the main thread know we're running.
            myReady.set();

            // Let the main thread tell us when we can complete.
            myClose.wait();
        }
    };

    /// @brief Task that joins a background thread.
    class RunComputation
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        cz::Computation& myComputation;

    public:
        RunComputation (cz::Engine& engine, cz::Computation& computation)
            : myEngine(engine)
            , myComputation(computation)
        {
        }

    private:
        virtual void run ()
        {
            // Start an asynchronous computation.
            std::cout
                << "Obtaining promise."
                << std::endl;
            cz::Promise promise = myEngine.execute(myComputation);

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
    int test_computation ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);

        // Start a background thread and wait until we have a confirmation that
        // it's running.
        TestComputation computation;

        // Start a background task that will join the thread.
        std::cout
            << "Spawning background task."
            << std::endl;
        RunComputation task(engine, computation);
        hub.spawn(task, cz::Hub::StartNow);

        // Wait until the computation confirms that it's running.
        computation.ready().wait();

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

        // Allow the computation to complete.  This will cause the asynchronous
        // wait to be satisfied, a completion notification to be posted, and
        // the promise to be fulfilled.
        computation.close().set();

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
        return (::test_computation());
    }

}


#include <w32/app/console-program.cpp>
