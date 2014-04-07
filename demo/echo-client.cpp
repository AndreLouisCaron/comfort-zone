// Copyright (c) 2013, Andre Caron (andre.l.caron@gmail.com)
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
#include <w32.net.hpp>

#include <iostream>
#include <vector>


namespace echo {


    class Settings
    {
        w32::mt::Mutex myMutex;
    public:
        w32::net::ipv4::Address peer_host () const {
            return (w32::net::ipv4::Address::local());
        }

        w32::uint16 peer_port () const {
            return (8000);
        }

        w32::net::ipv4::EndPoint peer () const {
            return (w32::net::ipv4::EndPoint(peer_host(), peer_port()));
        }

        int threads () const {
            return (100);
        }

        int iterations () const {
            return (10);
        }

        w32::mt::Mutex& console_lock () {
            return (myMutex);
        }
    };


    w32::dword client_thread (Settings& settings)
    try
    {
        char data[16*1024];
        char copy[16*1024];
        const w32::dword size = sizeof(data);

        bool error = false;

        // Repeatedly bash on the server!
        for (int i=0; !error && (i < settings.iterations()); ++i)
        {
            // Connect to the server.
            w32::net::tcp::Stream stream(settings.peer());
            stream.linger(5); // let system attempt to flush queued data after
                              // socket is closed with a timeout of 5 seconds.

            // Put the entire payload.
            w32::dword used = 0;
            while (used < size) {
                used += stream.put(data+used, size-used);
            }
            if (used < size) {
                w32::mt::Mutex::Lock _(settings.console_lock());
                std::cout
                    << "could not send everything."
                    << std::endl;
                error = true; continue;
            }

            // Signal our intent not to send anything else.
            stream.shutdown(w32::net::Socket::Shutdown::output());

            // Get the entire payload.
            w32::dword pass = 0; used = 0;
            do {
                pass = stream.get(copy+used, size-used);
            }
            while ((pass > 0) && ((used+=pass) < size));
            if (used < size) {
                w32::mt::Mutex::Lock _(settings.console_lock());
                std::cout
                    << "could not read everything."
                    << std::endl;
                error = true; continue;
            }

            // Make sure we got an exact copy.
            if (std::memcmp(data, copy, size) != 0)
            {
                w32::mt::Mutex::Lock _(settings.console_lock());
                std::cout
                    << "received incoherent data."
                    << std::endl;
                error = true; continue;
            }

            // Go for a clean exit (avoid `TIME_WAIT` state).
            stream.disconnect();
        }

        // If thread exits, any I/O operations (including pending writes whosse
        // payloads are lingering in the sockets' output buffer after the
        // sockets have been "closed") will be cancelled.  Prevent loss of data
        // by allowing all the I/O to complete.
        w32::mt::sleep(w32::Timespan(5*1000));

        // Let the main thread know an error occurred.
        return (error? EXIT_FAILURE : EXIT_SUCCESS);
    }
    catch (const w32::Error& error)
    {
        w32::mt::Mutex::Lock _(settings.console_lock());
        std::cerr
            << "Uncaught Win32 error: " << error << "."
            << std::endl;
        return (EXIT_FAILURE);
    }
    catch (const std::exception& error)
    {
        w32::mt::Mutex::Lock _(settings.console_lock());
        std::cerr
            << "Uncaught exception: " << error.what() << "."
            << std::endl;
        return (EXIT_FAILURE);
    }
    catch (...)
    {
        w32::mt::Mutex::Lock _(settings.console_lock());
        std::cerr
            << "Uncaught exception of unknown type."
            << std::endl;
        return (EXIT_FAILURE);
    }

    w32::dword client_thread (void * context)
    {
        return (client_thread(*static_cast<Settings*>(context)));
    }


    /*!
     * @brief Echo client demo program.
     */
    int client_main (int argc, wchar_t ** argv)
    {
        Settings settings;
        w32::Waitable::Set waitset;

        // Launch threads that will pound on the server.
        std::vector<w32::mt::Thread> threads;
        threads.reserve(settings.threads());
        for (int i = 0; (i < settings.threads()); ++i) {
            threads.push_back(w32::mt::Thread(
                w32::mt::Thread::function<client_thread>(), &settings));

            // Make sure we can monitor completion.
            waitset |= threads[i];
        }

        // Wait for all threads to complete.
        while (!waitset.empty())
        {
            // Block until at least one thread completes.
            const w32::dword i = w32::Waitable::any(waitset);

            { w32::mt::Mutex::Lock _(settings.console_lock());
                std::cerr
                    << "thread #" << i << " has joined."
                    << std::endl;
            }

            // Check for failure in the thread.
            w32::mt::Thread thread = threads[i];
            const w32::dword status = thread.status();
            if (status != 0) {
                w32::mt::Mutex::Lock _(settings.console_lock());
                std::cerr
                    << "  (exited abnormally, status=" << status << ")"
                    << std::endl;
            }

            // Stop monitoring the thread.
            waitset.remove(i);
            threads.erase(threads.begin()+i);
        }

        { w32::mt::Mutex::Lock _(settings.console_lock());
            std::cerr
                << "done!"
                << std::endl;
        }

        return (EXIT_SUCCESS);
    }

}


#include <w32/app/console-program.hpp>

namespace {

    int run (int argc, wchar_t ** argv)
    {
        const w32::net::Context _;
        return (echo::client_main(argc, argv));
    }

}

#include <w32/app/console-program.cpp>
