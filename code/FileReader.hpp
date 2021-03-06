#ifndef _cz_FileReader_hpp__
#define _cz_FileReader_hpp__

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
#include <w32.net.hpp>

#include "Request.hpp"
#include "Stream.hpp"

namespace cz {

    class Engine;

    /*!
     * @ingroup resources
     * @brief Stream that reads from an input file.
     */
    class FileReader :
        public Reader
    {
        /* data. */
    private:
        Engine& myEngine;
        w32::io::InputStream myStream;

        /* constructor. */
    public:
        explicit FileReader (Engine& engine, const w32::string& path);

        /* overrides. */
    public:
        virtual size_t get (void * data, size_t size);
    };


    /*!
     * @ingroup requests
     * @brief %Request to read from a file.
     */
    class GetRequest
    {
        /* data. */
    private:
        Request myRequest;
        w32::io::InputStream myStream;

        // Used to prevent `ready()` from returning the same value after _and
        // before_ the overlapped operation has completed (the default state
        // makes `ready()` return `true`).
        enum State {
            Idle,
            Busy,
        } myState;

        w32::dword myResult;

    public:
        GetRequest (Engine& engine, w32::io::InputStream stream, void * context=0);

        /* methods. */
    public:
        void start (void * data, size_t size);
        bool ready () const;

        bool is (Request * request) const;

        // Note: `close()` is implicit (native async request).

        size_t result ();

        // call before calling `start()` again.
        void reset ();
        void reset (void * context);

        void * context () const;

        template<typename T>
        T * context () const
        {
            return (myRequest.context<T>());
        }

    private:
        // TODO: implement this.
        void abort ();
    };

}

#endif /* _cz_FileReader_hpp__ */
