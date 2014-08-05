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

    /// @brief Task that writes to an anonymous pipe.
    class WriteToAnonymousPipe
        : public cz::Task
    {
    private:
        cz::Engine& myEngine;
        w32::io::OutputStream& myStream;

    public:
        WriteToAnonymousPipe (cz::Engine& engine,
                              w32::io::OutputStream& stream)
            : myEngine(engine)
            , myStream(stream)
        {
        }

    private:
        virtual void run ()
        {
            // Start an asynchronous anonymous pipe read.
            std::cout
                << "Obtaining promise."
                << std::endl;
            const char data[] = "Hello, world!";
            const w32::size_t size = sizeof(data) - 1;
            cz::Promise promise = myEngine.put(myStream, data, size);

            // NOTE: technically speaking, there is a possibility that we don't
            //       write all the data in a single pass so we should loop.
            //       However, it would make the test more complex (would
            //       require looping in both the main thread and here) and this
            //       case hasn't really arisen yet.

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

            // TODO: check completion result.
        }
    };

    int test_anonymous_pipe_put ()
    {
        cz::Hub hub;
        cz::Engine engine(hub);

        // Create a reader/writer pipe pair.
        w32::io::AnonymousPipe pipe;
        w32::io::InputStream reader = pipe;
        w32::io::OutputStream writer = pipe;

        // Start a background task that will read from the pipe.
        std::cout
            << "Spawning background task."
            << std::endl;
        WriteToAnonymousPipe task(engine, writer);
        hub.spawn(task, cz::Hub::StartNow);

        // Read from the pipe to confirm that a background thread has written
        // to it due to a request from the task.
        std::cout
            << "Reading from the anonymous pipe."
            << std::endl;
        char data[14] = {'\0'};
        const w32::size_t size = sizeof(data) - 1;
        w32::size_t used = 0;
        w32::size_t pass = 0;
        do {
            pass = reader.get(data+used, size-used);
        }
        while ((pass > 0) && ((used += pass) < size));

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

        if (strncmp(data, "Hello, world!", 13) != 0) {
            std::cerr
                << "Received invalid data!"
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
        return (::test_anonymous_pipe_put());
    }

}


#include <w32/app/console-program.cpp>
