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

    /// @brief Task that acquires a mutual-exclusion lock.
    class AcquireMutex
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        w32::mt::Mutex& myMutex;

    public:
        AcquireMutex (cz::Engine& engine, w32::mt::Mutex& mutex)
            : myEngine(engine)
            , myMutex(mutex)
        {
        }

    private:
        virtual void run ()
        {
            // Start an asynchronous decrement.
            std::cout
                << "Obtaining promise."
                << std::endl;
            cz::Promise promise = myEngine.acquire(myMutex);

            // No need to block, promise should have been fulfilled
            // synchronously.

            // Check promise state.
            std::cout << "Checking promise state." << std::endl;
            if (promise.state() != cz::Promise::Done) {
                std::cout
                    << "Promise hasn't been fulfilled!"
                    << std::endl;
                cz_debug_when(true);
            }
        }
    };

    /// @test Acquiring an available mutex completes synchronously.
    /// @return Non-zero on test failure.
    int test_mutex_synchronous_acquire ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);
        w32::mt::Mutex mutex;

        // Start the tast that will acquire the mutex.
        AcquireMutex task(engine, mutex);
        hub.spawn(task, cz::Hub::StartNow);

        // Task should complete wihtout needing to wait for a notification.
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
        return (::test_mutex_synchronous_acquire());
    }

}


#include <w32/app/console-program.cpp>
