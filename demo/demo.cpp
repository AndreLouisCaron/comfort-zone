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


#include "comfort-zone.hpp"

#include <iostream>
#include <memory>

namespace {

    class SampleTask1 :
        public cz::Task
    {
        /* data. */
    private:
        cz::Engine& myEngine;

        /* construction. */
    public:
        SampleTask1 (cz::Engine& engine)
            : myEngine(engine)
        {}

        /* methods. */
    private:
        w32::dword background_job ()
        {
            std::cerr
                << "@task(sample-1): thread has begun"
                << std::endl;

            w32::mt::sleep(w32::Timespan(2500));

            std::cerr
                << "@task(sample-1): thread complete"
                << std::endl;

            return 0;
        }

        static w32::mt::Thread::Function entry_point ()
        {
            return (w32::mt::Thread::method<SampleTask1,
                                            &SampleTask1::background_job>());
        }

        /* overrides. */
    protected:
        virtual void run ()
        {
            // Launch a background thread.
            std::cerr
                << "@task(sample-1): starting thread"
                << std::endl;
            w32::mt::Thread thread(entry_point(), this);

            // Wait for it complete.
            std::cerr
                << "@task(sample-1): async wait"
                << std::endl;
            myEngine.join(thread);

            // Make sure the thread completes.
            std::cerr
                << "@task(sample-1): checking thread"
                << std::endl;
            thread.join();

            std::cerr
                << "@task(sample-1): done"
                << std::endl;
        }
    };

    class SampleTask2 :
        public cz::Task
    {
        /* data. */
    private:
        cz::Engine& myEngine;

        /* construction. */
    public:
        SampleTask2 (cz::Engine& engine)
            : myEngine(engine)
        {}

        /* overrides. */
    protected:
        virtual void run ()
        {
            std::cerr
                << "@task(sample-2): 1"
                << std::endl;

            // Try reading from the standard input.
            { char data[1024]; cz::size_t size = sizeof(data);
                std::auto_ptr<cz::Reader> reader(
                    //myEngine.file_reader(L"demo.exe.resource.txt"));
                    myEngine.standard_input());
                std::auto_ptr<cz::Writer> writer(
                    //myEngine.file_writer(L"some-output.txt"));
                    myEngine.standard_output());

                // Exhaust input stream.
                for (cz::size_t used=0; ((used=reader->get(data, size)) > 0);)
                {
                    for (cz::size_t pass=0; (pass < used);) {
                        pass += writer->put(data+pass, used-pass);
                    }
                }
            }

            std::cerr
                << "@task(sample-2): 2"
                << std::endl;
        }
    };

    class SampleTask3 :
        public cz::Task
    {
        /* data. */
    private:
        cz::Engine& myEngine;

        /* construction. */
    public:
        SampleTask3 (cz::Engine& engine)
            : myEngine(engine)
        {}

        /* overrides. */
    protected:
        virtual void run ()
        {
            std::cerr
                << "@task(sample-3): 1"
                << std::endl;

            try {
                // Start listening for incomming connections.
                const w32::net::ipv4::EndPoint endpoint(127, 0, 0, 1, 8080);
                std::auto_ptr<cz::Listener> listener
                    (myEngine.listen(endpoint));

                std::cerr
                    << "@task(sample-3): 2"
                    << std::endl;

                // Accept 1st connection!
                char data[1024]; cz::size_t size = sizeof(data);
                std::auto_ptr<cz::Channel> stream
                    (listener->accept(data, size));

                std::cerr
                    << "@task(sample-3): 3"
                    << std::endl;
            }
            catch (const w32::Error& error) {
                std::cerr << error << std::endl;
            }
            catch (...) {
                std::cerr << "Caught execption in task 3!" << std::endl;
            }

            std::cerr
                << "@task(sample-4): 4"
                << std::endl;
        }
    };

    class SampleTask4 :
        public cz::Task
    {
        /* data. */
    private:
        cz::Engine& myEngine;

        /* construction. */
    public:
        SampleTask4 (cz::Engine& engine)
            : myEngine(engine)
        {}

        /* overrides. */
    protected:
        virtual void run ()
        {
            std::cerr
                << "@task(sample-4): 1"
                << std::endl;

            try {
                // Start listening for incomming connections.
                const w32::net::ipv4::EndPoint peer(127, 0, 0, 1, 8080);
                std::auto_ptr<cz::Channel> stream
                    (myEngine.connect(peer));

                std::cerr
                    << "@task(sample-4): 2"
                    << std::endl;
            }
            catch (const w32::Error& error) {
                std::cerr << " >> exception!" << std::endl;
                std::cerr << " >> " << error << std::endl;
            }
            catch (...) {
                std::cerr << " >> unknown exception!" << std::endl;
            }

            std::cerr
                << "@task(sample-4): 3"
                << std::endl;
        }
    };

}

#include <w32/app/console-program.hpp>

namespace {

    int run (int argc, wchar_t ** argv)
    {
        std::cerr << "+program()" << std::endl;

        const w32::net::Context _;

        // Start an I/O application!
        cz::Hub hub;
        cz::Engine engine(hub);

        // Block on waitable.
        std::cerr
            << std::endl
            << "Running sample task 1." << std::endl
            << "----------------------" << std::endl;
        { SampleTask1 task(engine);
            // Start the slave!
            hub.spawn(task);
            hub.resume_pending_slave();

            // Process notifications until all tasks are done.
            while (hub.active_slaves() > 0) {
                engine.wait_for_notification(), hub.resume_pending_slave();
            }
        }
        std::cerr << "-sample()" << std::endl;
        std::cerr << std::endl;

        // Stream copy.
        std::cerr
            << std::endl
            << "Running sample task 2." << std::endl
            << "----------------------" << std::endl;
        { SampleTask2 task(engine);
            // Start the slave!
            hub.spawn(task);
            hub.resume_pending_slave();

            // Process notifications until all tasks are done.
            while (hub.active_slaves() > 0) {
                engine.wait_for_notification(), hub.resume_pending_slave();
            }
        }
        std::cerr << "-sample()" << std::endl;
        std::cerr << std::endl;

        // Simple TCP client & server.
        std::cerr
            << std::endl
            << "Running sample tasks 3 & 4." << std::endl
            << "---------------------------" << std::endl;
        { SampleTask3 server(engine);
          SampleTask4 client(engine);
            // Start the slaves!
            hub.spawn(server), hub.resume_pending_slave();
            hub.spawn(client), hub.resume_pending_slave();

            // Process notifications until all tasks are done.
            while (hub.active_slaves() > 0) {
                engine.wait_for_notification(), hub.resume_pending_slave();
            }
        }
        std::cerr << "-sample()" << std::endl;
        std::cerr << std::endl;

        std::cerr << "-program()" << std::endl;
        return (EXIT_SUCCESS);
    }

}

#include <w32/app/console-program.cpp>
