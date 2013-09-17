#ifndef _cz_Request_hpp__
#define _cz_Request_hpp__

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

#include <w32.hpp>
#include <w32.io.hpp>
#include <w32.tp.hpp>

#include "Hub.hpp"

namespace cz {

    class Engine;

    // TODO: make this non-copyable.
    // TODO: implement `reset()` method for reusing the request.
    class Request
    {
    friend class Engine;

        /* nested types. */
    private:
        // Attention: make sure this stays a POD.
        struct Data :
            public ::OVERLAPPED
        {
            Request * request; // owner.
        };

        /* class methods. */
    public:
        // Yields entry point for thread pool.
        static w32::tp::Work::Callback work_callback ();
        static w32::tp::Wait::Callback wait_callback ();

        /* data. */
    private:
        Data myData;

        Engine& myEngine;

        // TOOD: break this down into necessary items?
        w32::io::Notification myNotification;

        Hub::Slave * mySlave; // slave that started the request.

        void * myContext; // application defined context.

        /* construction. */
    public:
        Request (Engine& engine, void * context=0);

        virtual ~Request ();

        /* methods. */
    public:
        Engine& engine ();

        /*!
         * @internal
         */
        Data& data ();

        /*!
         * @internal
         * @see close()
         */
        void start (); // patch `ready()` test for async operations that don't
                       // deal with system streams.

        /*!
         * @brief Check if the operation has completed.
         */
        bool ready () const;

        /*!
         * @internal
         * @see start()
         */
        void close (); // patch `ready()` test for async operations that don't
                       // deal with system streams.

        /*!
         * @internal
         * @brief Prepare for issuing another request.
         */
        void reset ();

        /*!
         * @internal
         */
        const w32::io::Notification& notification () const;

        void * context () const;

        template<typename T>
        T * context () const
        {
            return (static_cast<T*>(myContext));
        }

    private:
        // Post notification to completion port.
        void report_completion (w32::dword size=0);

        // Entry point for thread pool.
        void execute_computation (w32::tp::Hints& hints);

        // Entry point for thread pool.
        void release_wait_client (w32::tp::Hints& hints);
    };

}

#endif /* _cz_Request_hpp__ */
