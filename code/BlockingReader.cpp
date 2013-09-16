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

#include "BlockingReader.hpp"
#include "Engine.hpp"

#include "trace.hpp"

namespace cz {

    BlockingReader::BlockingReader (Engine& engine,
                                    w32::io::InputStream& stream)
        : myEngine(engine)
        , myBackend(stream)
    {
    }

    size_t BlockingReader::get (void * data, size_t size)
    {
        BlockingGet computation(myBackend, data, size);
        myEngine.compute(computation);
        return (computation.result());
    }

    w32::io::InputStream& BlockingReader::stream ()
    {
        return (myBackend);
    }


    BlockingGet::BlockingGet (w32::io::InputStream& stream,
                                void * data, size_t size)
        : myStream(stream)
        , myData(data)
        , mySize(size)
        , myUsed(0)
    {
    }

    size_t BlockingGet::result () const
    {
        return (myUsed);
    }

    void BlockingGet::execute ()
    {
        cz_trace(" >> Reading " << mySize << " bytes from blocking stream.");

        // For a stream like the standard input, the application needs to
        // react to each line as soon as it is received, but the buffer may
        // be larger than necessary.  Don't try to fill the buffer because
        // it may block this kind of stream.
        myUsed += myStream.get(static_cast<char*>(myData), mySize);
    }

}
