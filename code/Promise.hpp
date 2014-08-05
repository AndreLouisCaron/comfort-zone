#ifndef _cz_Promise_hpp__
#define _cz_Promise_hpp__

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


#include <w32.hpp>
#include <limits>
#include <vector>

#define cz_debug_when(condition) if ((condition)){::DebugBreak();}


namespace cz {

    class Engine;
    class Task;

    /*!
     * @brief Monitor for the completion of an asynchronous operation.
     *
     * @see Promise::Set
     */
    class Promise
    {
    friend class Engine;

        /* nested types. */
    public:
        enum State  // TODO: make private and use higher-level methods.
        {
            Null,  // nothing!
            Idle,  // not started.
            Busy,  // in progress.
            Done,  // success/failure.
            Dead,  // cancelled/aborted.
        };

        // Reference-counted PIMPL.
        struct Data;

    public:
        class Set;

        /* data. */
    private:
        // PIMPL.  Can be NULL.
        Data * myData;

        /* construction. */
    public:
        Promise () throw();

        /*!
         * @brief Copy-construct a new promise.
         *
         * @param[in] rhs The promise to refer to.
         *
         * @post Both @c *this and @c rhs refer to the same promise.
         */
        Promise (const Promise& rhs) throw();

        Promise (Engine& engine, Task& task);

        ~Promise () throw();

        // Build a promise from an existing data (for use by engine).
        explicit Promise (Data * data);

        /* methods. */
    public:
        /*!
         * @brief O(1) swap operation.
         *
         * @param[in,out] rhs Future contents and storage location of @c *this.
         *
         * @post @c *this contains the contents of @a rhs.
         * @post @a rhs contains the contents of @c *this.
         *
         * @see swap(Promise&, Promise&)
         */
        void swap (Promise& rhs) throw();

        // TODO: turn this into boolean state query methods (fulfilled, broken).
        State state () const;

        /*!
         * @brief Call a free function when the promise is fulfilled.
         *
         * @attention Only one callback can be registered for each promise.
         *  Calling this method a second time on the same promise will override
         *  the previous callback.
         *
         * @param[in] method Application-defined callback.
         * @param[in,out] object Application-defined context that will be
         *  passed to @a method uninterpreted by the engine or the promise.
         * @return @c *this, for method chaining.
         *
         * @post @c method(*this,object) will be called in the task that
         *  initiated the asynchronous operation when the promise is
         *  settled (check the promise state for errors).
         */
        Promise& then (void(*method)(Promise&, void*), void* object);

        /*!
         * @brief Call an instance method when the promise is fulfilled.
         *
         * @attention Only one callback can be registered for each promise.
         *  Calling this method a second time on the same promise will override
         *  the previous callback.
         *
         * @warning Refering to a virtual method binds the call to the
         *  implementation available as `object.T::M()`, which may not
         *  correspond to the `object.M()` if `T` is a base class of `object`'s
         *  real type.
         *
         * @tparam T The class that defines the method to be called.
         * @tparam M The instance method to call on @a object.
         * @param[in] object Object on which @a M will be called.
         * @return @c *this, for method chaining.
         *
         * @post @c object.*M(*this) will be called in the task that initiated
         *  the asynchronous operation when the promise is settled (check the
         *  promise state for errors).
         */
        template<typename T, void(T::*M)(Promise&)>
        Promise& then (T& object);

        /*!
         * @brief Signal your intent to ignore the promise's result.
         *
         * @return @c true if the operation could be cancelled synchronously.
         *  If cancellation is unsuccessful (e.g. because the promise has
         *  already been fulfilled) or asynchronous, @c false is returned.
         *
         * @attention There is always a race in cancellation of an asynchronous
         *  operation because it can complete concurrently with your attempt to
         *  cancel it.  @e Always check the return value to see if cancellation
         *  has been confirmed.
         *
         * @note Even if cancellation is asynchronus, it is safe to delete all
         *  @c Promise objects referring to this asynchronous operation.
         *
         * @warning Keep in mind that in presence of asynchronous computation
         *  operations, especially when application-defined, it's possible that
         *  cancellation of the asynchronous operation is not supported at all.
         *  In that case, the promise will never become fulfilled (even if it
         *  completes successfully) even if it completes successfully.  Note
         *  that it safe to release all promise objects watching for completion
         *  of this operation, but may not be safe to release the
         *  application-defined resources until the promise state becomes
         *  "dead".
         */
        bool cancel ();

        /* operators. */
    public:
        /*!
         * @brief Copy-assign by reference.
         *
         * @param[in] rhs The promise to refer to.
         * @return @c *this, for chained assignment.
         *
         * @post Both @c *this and @c rhs refer to the same promise.
         */
        Promise& operator= (const Promise& rhs) throw();
    };

    /*!
     * @relates Promise
     * @brief Overload of @c Promise::swap for argument-dependent lookup.
     *
     * @param[in,out] lhs Future storage location of @a rhs.
     * @param[in,out] rhs Future storage location of @a lhs.
     */
    inline void swap (Promise& lhs, Promise& rhs) throw()
    {
        lhs.swap(rhs);
    }

    /*!
     * @internal
     * @brief Bookkeeping for an asynchronous operation.
     *
     * @note This *MUST* absolutely stay a POD.  In particular, this struct
     *  should *never* contain virtual functions because the vtable would
     *  offset the `this` pointer and we wouldn't be able to `reinterpret_cast`
     *  between `OVERLAPPED*` and `Data*`.
     */
    struct Promise::Data :
        public ::OVERLAPPED
    {
        /* data. */
    public:
        // Intrusive ref-counting :-)
        //
        // Each `Promise` object that points to this holds a reference to this
        // structure.  In addition, an "artificial" reference is held to this
        // object while the async operation is in progress: the reference count
        // is increased before initiating the async operation and decreased *by
        // the engine* when processing the notification received through the
        // I/O completion port.
        //
        // NOTE: because all reference count changes are done in the (single)
        //       thread in which all the engine's tasks are running, there is
        //       no need to use (expensive) atomic operations.  However, this
        //       optimization makes it unsafe to access the reference count in
        //       other threads -- make sure this is reflected in the
        //       implementation.
        int references;

        // State of the atomic operation.
        State state;

        Engine * engine;  // Engine to which the completion will be reported.
        Task * task;      // Task that initiated the request.  Task must be
                          // running inside the engine above.

        // Allows O(1) dispatch in multiplexed wait for fulfillment of multiple
        // promises.  This is set implicitly by the wait set.
        //
        // When equal to MAXDWORD, the promise is not in use by a wait set.
        std::size_t hint;

        // Completion data received via the I/O completion notificaiton.
        w32::dword completion_status;
        w32::dword completion_result;

        // "Virtual destructor".
        void (*cleanup)(void*);

        // Virtual "close" method.  Called by hub when the completion
        // notification is processed.
        void (*post_process)(void*);

        // Promise completion callback.
        void(*method)(Promise&, void*);
        void * object;

        // Helper for one-liner implementation of C-style virtual destructors.
        template<typename T>
        static void destroy (void * object)
        {
            delete static_cast<T*>(object);
        }

        /* construction. */
    public:
        Data (Engine& engine, Task& task);
    };

    template<typename T, void(T::*M)(Promise&)>
    Promise& then (T& object)
    {
        // Nested trampoline function.
        struct unbind {
            static void callback (Promise& promise, void* context) {
                return ((static_cast<T*>(context)->*M)(promise));
            }
        };
        // Use trampoline function as actual callback.
        return (this->then(&unbind::callback, &object));
    }

    /*!
     * @brief Container for multiplexed monitoring of multiple promises.
     *
     * @see Engine::any
     * @see Engine::all
     * @see Promise
     */
    class Promise::Set
    {
        // Granting special access to engine avoids exposing unnecessary
        // implementation details to other classes.
    friend class Engine;

        /* constants. */
    public:
        static std::size_t not_watched () {
            return (std::numeric_limits<std::size_t>::max());
        }

        /* data. */
    private:
        std::vector<Promise> myPromises;

        // Helper for O(1) consumption of next fulfilled promise.
        //
        // Invariant: all fulfilled promises are stored at positions greater or
        // equal to this mark.
        std::size_t myMark;

        /* construction. */
    public:
        Set ();

        ~Set ();

        /* methods. */
    public:
        std::size_t size () const;
        std::size_t done () const;

        void watch (const Promise& promise);
        void ignore (const Promise& promise);
        Promise next ();

    private:
        void swap (std::size_t i, std::size_t j);
        void mark_as_fulfilled (Promise::Data * promise);
    };

}

#endif /* _cz_Promise_hpp__ */
