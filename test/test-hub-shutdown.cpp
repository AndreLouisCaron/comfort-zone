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

    /// @test Verify that shutting down the hub causes tasks to complete.
    /// @return Non-zero on test failure.
    int test_hub_shutdown ()
    {
        class TestTask
            : public cz::Task
        {
            /* data. */
        private:
            int myLimit;
            int myCount;

            /* construction. */
        public:
            TestTask (int limit)
                : myLimit(limit)
                , myCount(0)
            {}

            /* overrides. */
        public:
            virtual void run ()
            {
                try {
                    while (++myCount <= myLimit) {
                        std::cout << "iteration #" << myCount << std::endl;
                        pause();
                    }
                }
                catch (...) {
                    //std::cout << "task shutting down?" << std::endl;
                    //pause();
                }
                std::cout << "task completed normally." << std::endl;
            }
        };

        // Star the task.
        cz::Hub hub;

        // Check that the task can complete by itself.
        TestTask task1(5);
        hub.spawn(task1);
        for (int i=0; (i < 6); ++i) {
            hub.resume_pending_slaves();
        }
        if (!task1.dead()) {
            std::cerr
                << "Expecting task #1 to complete!"
                << std::endl;
            return (EXIT_FAILURE);
        }

        // Check that the task can complete by itself.
        TestTask task2(5);
        hub.spawn(task2);
        for (int i=0; (i < 3); ++i) {
            hub.resume_pending_slaves();
        }
        if (task2.dead()) {
            std::cerr
                << "Not expecting task #2 to complete!"
                << std::endl;
            return (EXIT_FAILURE);
        }

        std::cerr
            << "Shutting hub down."
            << std::endl;
        hub.shutdown();

        std::cerr
            << "Checking task #2's state."
            << std::endl;
        if (!task2.dead()) {
            std::cerr
                << "Expecting task #2 to be aborted!"
                << std::endl;
            return (EXIT_FAILURE);
        }

        // Done.
        return (EXIT_SUCCESS);
    }

}


#include <w32/app/console-program.hpp>


namespace {

    int run (int, wchar_t **)
    {
        const w32::net::Context _;
        return (::test_hub_shutdown());
    }

}


#include <w32/app/console-program.cpp>
