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

    class WaitForAnyEvent
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        w32::mt::ManualResetEvent myEvent1;
        w32::mt::ManualResetEvent myEvent2;
        w32::mt::ManualResetEvent myEvent3;
        w32::mt::ManualResetEvent myEvent4;
        w32::mt::ManualResetEvent myEvent5;
        w32::mt::ManualResetEvent myEvent6;
        w32::mt::ManualResetEvent myEvent7;
        w32::mt::ManualResetEvent myEvent8;

    public:
        WaitForAnyEvent (cz::Engine& engine)
            : myEngine(engine)
        {
        }

        void signal_1st ()
        {
            myEvent2.set();
        }

        void signal_2nd ()
        {
            myEvent3.set();
        }

        void signal_3rd ()
        {
            myEvent1.set();
        }

        void signal_4th ()
        {
            myEvent4.set();
        }

        void signal_5th ()
        {
            myEvent5.set();
        }

        void signal_6th ()
        {
            myEvent6.set();
        }

        void signal_7th ()
        {
            myEvent7.set();
        }

        void signal_8th ()
        {
            myEvent8.set();
        }

    private:
        virtual void run ()
        {
            // Start several parallel wait operations.
            cz::Promise::Set promises;
            cz_debug_when(promises.size() != 0);
            cz_debug_when(promises.done() != 0);

            // Try to wait when the set is empty.
            std::cout
                << "Waiting on empty set."
                << std::endl;
            myEngine.wait_for_any(promises);

            // Add a watch to the set while the set is empty.
            promises.watch(myEngine.await(myEvent1));
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 0);

            // Add a watch to the set while all promises are busy.
            promises.watch(myEngine.await(myEvent2));
            cz_debug_when(promises.size() != 2);
            cz_debug_when(promises.done() != 0);

            // Complete last watch in the set while all promises are busy.
            std::cout
                << "Waiting for 1st event."
                << std::endl;
            myEngine.wait_for_any(promises);
            cz_debug_when(!myEvent2.test());
            cz_debug_when(promises.size() != 2);
            cz_debug_when(promises.done() != 1);

            // Add a watch to the set while there are both busy and done
            // promises.
            promises.watch(myEngine.await(myEvent3));
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 1);

            // Complete last watch in the set while there are both busy and
            // done promises.
            std::cout
                << "Waiting for 2nd event."
                << std::endl;
            myEngine.wait_for_any(promises);
            cz_debug_when(!myEvent3.test());
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 2);

            // Complete 1st watch in the set while there are both busy and done
            // promises.
            std::cout
                << "Waiting for 3rd event."
                << std::endl;
            myEngine.wait_for_any(promises);
            cz_debug_when(!myEvent1.test());
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 3);

            // Try to wait when the set contains only done promises.
            std::cout
                << "Waiting on completed set."
                << std::endl;
            myEngine.wait_for_any(promises);
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 3);

            // Clear the set.
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 2);
            cz_debug_when(promises.done() != 2);
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 1);
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 0);
            cz_debug_when(promises.done() != 0);

            // Prepare a set with 1 done and 1 busy promise.
            std::cout
                << "Waiting on new promises."
                << std::endl;
            cz::Promise promise4 = myEngine.await(myEvent4);
            cz::Promise promise5 = myEngine.await(myEvent5);
            promises.watch(promise4);
            promises.watch(promise5);
            myEngine.wait_for_any(promises);
            cz_debug_when(promises.size() != 2);
            cz_debug_when(promises.done() != 1);

            // Ignore a settled promise.
            std::cout
                << "Ignoring completed promise."
                << std::endl;
            promises.ignore(promise4);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 0);
            std::cout
                << "Waiting for final promise."
                << std::endl;
            myEngine.wait_for_any(promises);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 1);
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 0);
            cz_debug_when(promises.done() != 0);

            cz::Promise promise6 = myEngine.await(myEvent6);
            cz::Promise promise7 = myEngine.await(myEvent7);
            cz::Promise promise8 = myEngine.await(myEvent8);

            // Add settled promise to the set when the set is empty.
            std::cout
                << "Adding settled promise to empty set."
                << std::endl;
            promises.watch(promise4);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 1);
            myEngine.wait_for_any(promises);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 1);
            promises.ignore(promise4);
            cz_debug_when(promises.size() != 0);
            cz_debug_when(promises.done() != 0);

            // Add settled promise to a set containg only busy promises.
            std::cout
                << "Adding settled promise to busy set."
                << std::endl;
            promises.watch(promise6);
            promises.watch(promise7);
            promises.watch(promise8);
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 0);
            promises.watch(promise4);
            cz_debug_when(promises.size() != 4);
            cz_debug_when(promises.done() != 1);
            myEngine.wait_for_any(promises);
            cz_debug_when(promises.size() != 4);
            cz_debug_when(promises.done() != 2);

            // Ignore a busy promise while there are done promises.
            std::cout
                << "Ignoring busy promise in partially done set."
                << std::endl;
            promises.ignore(promise8);
            cz_debug_when(promises.size() != 3);
            cz_debug_when(promises.done() != 2);

            // Ignore a busy promise while there are no done promises.
            std::cout
                << "Removing done promises from set."
                << std::endl;
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 2);
            cz_debug_when(promises.done() != 1);
            cz_debug_when(promises.next().state() != cz::Promise::Done);
            cz_debug_when(promises.size() != 1);
            cz_debug_when(promises.done() != 0);
            std::cout
                << "Ignoring busy promise in busy set."
                << std::endl;
            promises.ignore(promise7);
            cz_debug_when(promises.size() != 0);
            cz_debug_when(promises.done() != 0);
        }
    };

    int test_engine_wait_for_any ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);

        // Start a background task that will wait for events to complete.
        std::cout
            << "Spawning background task."
            << std::endl;
        WaitForAnyEvent task(engine);
        hub.spawn(task, cz::Hub::StartNow);

        task.signal_1st();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        task.signal_2nd();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        task.signal_3rd();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        task.signal_4th();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        task.signal_5th();
        engine.wait_for_notification();
        hub.resume_pending_slave();

        task.signal_6th();
        engine.wait_for_notification();
        hub.resume_pending_slave();

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
        return (::test_engine_wait_for_any());
    }

}


#include <w32/app/console-program.cpp>
