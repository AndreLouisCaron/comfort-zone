#ifndef _cz_BlockingWriter_hpp__
#define _cz_BlockingWriter_hpp__

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

#include "Computation.hpp"
#include "Request.hpp"
#include "Stream.hpp"

namespace cz {

    class Engine;

    /*!
     * @ingroup resources
     * @brief Stream that writes to a blocking output stream.
     */
    class BlockingWriter :
        public Writer
    {
        /* data. */
    private:
        Engine& myEngine;
        w32::io::OutputStream myBackend;

        /* constructor. */
    public:
        // TODO: allow caller to prevent overwriting existing file.
        explicit BlockingWriter (Engine& engine, w32::io::OutputStream& stream);

        /* overrides. */
    public:
        virtual size_t put (const void * data, size_t size);
    };


    class BlockingPutRequest :
        public cz::Computation
    {
        w32::io::OutputStream& myStream;
        const void *const myData;
        const size_t mySize;
        size_t myUsed;

    public:
        BlockingPutRequest (w32::io::OutputStream& stream,
                            const void * data, size_t size);
        size_t result () const;

    protected:
        virtual void execute ();
    };

}

#endif /* _cz_BlockingWriter_hpp__ */
