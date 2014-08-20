#ifndef _cz_Buffer_hpp__
#define _cz_Buffer_hpp__

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


#include <algorithm>
#include <cstddef>
#include <utility>


namespace cz {


    using std::size_t;


    /*!
     * @brief Fixsed-size I/O buffer that facilitates zero-copy.
     *
     * @attention Implementation is deliberately reversed from apparent
     *  terminology!  The idea is that the naming convention reflects
     *  the intended use at call site: when doing a "get" operation,
     *  the code will use "g*" functions, etc., which means it'll be
     *  inserting into the buffer (getting from the stream).
     */
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
        /*!
         * @brief Allocate @c size bytes of contiguous memory.
         * @param[in] size Number of bytes of contiguous memory to allocate.
         */
        Buffer (size_t size)
            : myData(new char[size])
            , mySize(size)
            , myBase(0)
            , myPeek(0)
        {}

    private:
        /*!
         * @internal
         * @brief Copy-construction is not supported.
         * @param[in] rhs Value to use for initialization.
         */
        Buffer (const Buffer& rhs);

    public:
        /*!
         * @brief Release allocated memory.
         */
        ~Buffer () {
            delete [] myData; myData = 0; mySize = myPeek = myBase = 0;
        }

        /* methods. */
    public:
        /*!
         * @brief O(1) exchange of contents with another buffer.
         * @param[in,out] other Future store location for @c *this's contents.
         *
         * @post @a other contains @c *this's contents.
         * @post @c *this contains @a other's contents.
         *
         * This operation is useful for moving buffer objects and changing
         * their "owner" without resorting to dynamically allocated @c Buffer
         * objects.  In particular, in proxy-like applications, this can be
         * used to exchange get & put buffers to implement zero-copy.
         */
        void swap (Buffer& other)
        {
            std::swap(myData, other.myData);
            std::swap(mySize, other.mySize);
            std::swap(myBase, other.myBase);
            std::swap(myPeek, other.myPeek);
        }

        /*!
         * @brief Total size of the buffer.
         * @return Size of the buffer, in bytes.
         */
        size_t size () const
        {
            return (mySize);
        }

        /*!
         * @brief Reset get and put pointers, as if the buffer were empty.
         *
         * @attention This operation is O(1).  It simply changes the get and
         *  put pointers -- buffer contents are not modified.  If the buffer
         *  contents contain sensitive information, you may want to reset the
         *  memory before using this method.
         */
        void reset ()
        {
            myBase = myPeek = 0;
        }

        /*!
         * @brief Amount of memory currently used by the buffer.
         * @return Size of used portion of the buffer, in bytes.
         */
        size_t used () const
        {
            return (myPeek-myBase);
        }

        /*!
         * @brief Pointer to the first byte of used memory.
         *
         * @return A pointer to the first byte of @e used memory.  The
         *  application can read up to @c psize() bytes starting at this
         *  location.
         *
         * @attention This call should be used to write contents to a stream
         *  (e.g. used when you perform a stream put operation).
         */
        char * pbase () {
            return (myData+myBase);
        }

        /*!
         * @brief Read-only pointer to the first byte of used memory.
         *
         * @return A read-only pointer to the first byte of @e used memory.
         *  The  application can read up to @c psize() bytes starting at this
         *  location.
         *
         * @attention This call should be used to write contents to a stream
         *  (e.g. used when you perform a stream put operation).
         */
        const char * pbase () const {
            return (myData+myBase);
        }

        /*!
         * @brief Amount of application data that is currently in the buffer.
         *
         * @return The maximum number of bytes that the application can use,
         *  starting at @c pbase().
         *
         * After the application has consumed data from the buffer, it should
         * call @c pused(size_t) to advance the put pointer.
         */
        size_t psize () const {
            return (myPeek-myBase);
        }

        /*!
         * @brief Mark @a size bytes of data as consumed.
         *
         * @param[in] size Number of bytes consumed by the application,
         *  starting at @c pbase().  This value is automatically truncated to
         *  @c psize() to protect the put pointer.
         * @return @c *this, for method chaining.
         */
        Buffer& pused (size_t size)
        {
            // TODO: truncate to @c psize()!
            myBase = std::min(myBase+size, myPeek);
            return (*this);
        }

        /*!
         * @brief Check if there is any application data in the buffer.
         *
         * @return @c true when it is "free" to call @c reset(), else @c false.
         *
         * Applications are strongly encouraged to call @c reset() each time
         * @c pdone() becomes true after a call to @c pused(size_t).  This
         * ensures that the buffer always offers the maximum amount of space
         * for stream get operations.  In the worst case, @c pdone() is
         * guaranteeed to happen every time your application has consumes
         * @c size() bytes of data.
         *
         * If you wish to ensure that the maximum space is available for get
         * operations, you can call @c pack() even when the buffer contains
         * application data.  However, this will "move" data to the beginning
         * of the buffer, which incurs a performance hit when the buffer
         * contains a large amount of data.
         */
        bool pdone () const {
            return (myBase == myPeek);
        }

        /*!
         * @brief Compact application data at the front of the buffer.
         *
         * @return @c *this, for method chaining.
         *
         * Packing data at the front of the buffer ensures that a maximum
         * amount of space is available for the next stream get operation.
         * This should occur naturally each time your application consumes
         * @c size() bytes from the buffer, but you may wish to force this to
         * happen in other situations.
         *
         * @warning Calling this too often will incur a performance hit on your
         *  application.  This operation should be used only when using (very)
         *  small buffer sizes, or when the cost of initiating "small" I/O
         *  operations is greater than that of copying the buffer's contents
         *  back to the front of the buffer.
         */
        Buffer& pack ()
        {
            // Don't copy if there's no space at the front.
            if (myBase > 0) {
                std::copy(myData+myBase, myData+myPeek, myData);
                myPeek -= myBase, myBase = 0;
            }
            return (*this);
        }

        /*!
         * @brief Pointer to the first byte of @e unused memory.
         *
         * @return A pointer to the first byte of @e unused memory.  The
         *  application can write up to @c gsize() bytes starting at this
         *  location.
         *
         * @attention This call should be used to read contents from a stream
         *  (e.g. used when you perform a stream get operation).
         */
        char * gbase () {
            return (myData+myPeek);
        }

        /*!
         * @brief Amount of buffer space available for the next stream get.
         *
         * @return The maximum number of bytes that the application can write
         *  to, starting at @c pbase().
         *
         * After the application has inserted data into the buffer, it should
         * call @c gused(size_t) to advance the get pointer.
         */
        size_t gsize () const {
            return (mySize-myPeek);
        }

        /*!
         * @brief Mark @a size bytes of data as used by the application.
         *
         * @param[in] size Number of bytes inserted by the application,
         *  starting at @c pbase().  This value is automatically truncated to
         *  @c gsize() to protect the get pointer.
         * @return @c *this, for method chaining.
         */
        Buffer& gused (size_t size)
        {
            myPeek = std::min(myPeek+size, mySize);
            return (*this);
        }

        /*!
         * @brief Check if there is any free buffer space.
         *
         * @return @c true when it is "free" to call @c reset(), else @c false.
         *
         * Applications are strongly encouraged to call @c reset() each time
         * @c gdone() becomes true after a call to @c gused(size_t).  This
         * ensures that the buffer always offers the maximum amount of space
         * for stream get operations.  In the worst case, @c gdone() is
         * guaranteeed to happen every time your application has consumes
         * @c size() bytes of data.
         *
         * If you wish to ensure that the maximum space is available for get
         * operations, you can call @c pack() even when the buffer contains
         * application data.  However, this will "move" data to the beginning
         * of the buffer, which incurs a performance hit when the buffer
         * contains a large amount of data.
         */
        bool gdone () const {
            return (myPeek == mySize);
        }

        /* operators. */
    private:
        /*!
         * @internal
         * @brief Copy-assignement is not supported.
         * @param[in] rhs Right-hand-side operand of the assignment.
         * @return @c *this, for chained assignment.
         */
        Buffer& operator= (const Buffer& rhs);
    };


}

#endif /* _cz_Buffer_hpp__ */
