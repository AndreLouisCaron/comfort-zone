#ifndef _cz_Buffer_hpp__
#define _cz_Buffer_hpp__

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


#include <algorithm>
#include <cstddef>
#include <utility>


namespace cz {


    using std::size_t;


    // WARNING: implementation is deliberately reversed from apparent
    //          terminology!  The idea is that the naming convention reflects
    //          the intended use at call site: when doing a "get" operation,
    //          uthe code will use "g*" functions, etc., which means it'll be
    //          inserting into the buffer (getting from the stream).
    class Buffer
    {
        /* data, */
    private:
        char * myData;
        size_t mySize;
        size_t myBase;
        size_t myPeek;

        /* construction. */
    public:
        Buffer (size_t size)
            : myData(new char[size])
            , mySize(size)
            , myBase(0)
            , myPeek(0)
        {}

    private:
        Buffer (const Buffer&);

    public:
        ~Buffer () {
            delete [] myData; myData = 0; mySize = myPeek = myBase = 0;
        }

        /* methods. */
    public:
        // O(1) swap, typically to exchange get & put buffers.
        void swap (Buffer& other)
        {
            std::swap(myData, other.myData);
            std::swap(mySize, other.mySize);
            std::swap(myBase, other.myBase);
            std::swap(myPeek, other.myPeek);
        }

        // total buffer size.
        size_t size () const
        {
            return (mySize);
        }

        void reset ()
        {
            myBase = myPeek = 0;
        }

        // amount of space currently in use.
        size_t used () const
        {
            return (myPeek-myBase);
        }

        // Pointer to base (get pointer).
        char * pbase () {
            return (myData+myBase);
        }
        const char * pbase () const {
            return (myData+myBase);
        }

        // Maximum size of next async get.
        size_t psize () const {
            return (myPeek-myBase);
        }

        // Call after async get.
        Buffer& pused (size_t size)
        {
            myBase = std::min(myBase+size, myPeek);
            return (*this);
        }

        // Check to see if more space is left.
        bool pdone () const {
            return (myBase == myPeek);
        }

        // Call before async get to free up space for next insert.
        Buffer& pack ()
        {
            // Don't copy if there's no space at the front.
            if (myBase > 0) {
                std::copy(myData+myBase, myData+myPeek, myData);
                myPeek -= myBase, myBase = 0;
            }
            return (*this);
        }

        // Pointer to peek (put pointer).
        char * gbase () {
            return (myData+myPeek);
        }
        const char * gbase () const {
            return (myData+myPeek);
        }

        // Maximum size of next async insert.
        size_t gsize () const {
            return (mySize-myPeek);
        }

        // Call after async put.
        Buffer& gused (size_t size)
        {
            myPeek = std::min(myPeek+size, mySize);
            return (*this);
        }

        // Check to see if more space is left (gsize()>0).
        bool gdone () const {
            return (myPeek == mySize);
        }

        /* operators. */
    private:
        Buffer& operator= (const Buffer&);
    };


}

#endif /* _cz_Buffer_hpp__ */
