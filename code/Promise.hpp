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
#include <w32.io.hpp>
#include <w32.tp.hpp>
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
        /*!
         * @brief State of a @c Promise.
         */
        enum State  // TODO: make private and use higher-level methods.
        {
            /// @brief Nothing has been promised.
            Null,

            /// @brief The promise has been prepared, but not issued.
            Idle,

            /// @brief The promise has been issued, but isn't yet settled.
            Busy,

            /// @brief The promise is settled and has been fulfilled.
            Done,

            /// @brief The promise is settled and has been broken.
            Dead,
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
        /*!
         * @brief Empty promise.
         */
        Promise () throw();

        /*!
         * @brief Empty promise to be issued by @a task, through @a engine.
         */
        Promise (Engine& engine, Task& task);

        /*!
         * @internal
         * @brief Wrap a promise.
         *
         * @param[in,out] data Promise data.
         *
         * @pre @a data's state is @c Busy.
         */
        explicit Promise (Data * data);

        /*!
         * @brief Copy-construct a new promise.
         *
         * @param[in] rhs The promise to refer to.
         *
         * @post Both @c *this and @c rhs refer to the same promise.
         */
        Promise (const Promise& rhs) throw();

        /*!
         * @brief Cleanup released resources.
         *
         * Promise objects are reference-counted.  The destructor only releases
         * the promise data when the @c Promise object being destroyed holds
         * the last reference to the promise data.
         *
         * @note Asynchronous operations hold a reference to the promise data.
         *  Deleting all @c Promise objects that refer to a promise data is
         *  safe even if the asynchronous operation referred to by the promise
         *  data has not yet completed.
         */
        ~Promise () throw();

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

        /*!
         * @brief Obtain the current state of the promise.
         *
         * @return The promise's current state.
         *
         * @note The promise state is only changed by the engine when
         *  processing I/O completion notifications.  The state is never
         *  changed by background threads.
         *
         * @todo Turn this into boolean state helpers (fulfilled, broken).
         */
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
        bool forget ();

        /*!
         * @brief Obtain the promise's completion status.
         *
         * For native asynchronous I/O operations, the completion status
         * contains the Win32 error code for the completion of the asynchronous
         * I/O request.  If the promise is fulfilled, this value is zero; if
         * the promise is broken, this value is non-zero.
         *
         * For a promise to execute a @c Computation, this value contains the
         * return code of the computation.
         *
         * For non-native operations, lookup the meaning of this value in the
         * documentation for that custom operation.
         *
         * @return The completion status that was received in the I/O
         *  completion notification that settled the promise.
         *
         * @pre The promise has been settled.
         */
        w32::dword completion_status () const;

        /*!
         * @brief Obtain the promise's completion result.
         *
         * For native asynchronous I/O operations, the completion result
         * contains the number of bytes transferred during the asynchronous I/O
         * request.  If the promise is broken, this value is undefined.
         *
         * For non-native operations, lookup the meaning of this value in the
         * documentation for that custom operation.
         *
         * @return The completion result that was received in the I/O
         *  completion notification that settled the promise.
         *
         * @pre The promise has been settled.
         */
        w32::dword completion_result () const;

    private:
        /*!
         * @internal
         * @brief Workaround to inherit friendship in promise implementations.
         *
         * @param[in,out] engine Engine who'se completion port we need access.
         * @return @a engine's completion port.
         *
         * @see Promise::Data::completion_port
         */
        static w32::io::CompletionPort& completion_port (Engine& engine);

        /*!
         * @internal
         * @brief Workaround to inherit friendship in promise implementations.
         *
         * @param[in,out] engine Engine who'se thread pool we need access.
         * @return @a engine's thread pool queue.
         *
         * @see Promise::thread_pool_queue
         */
        static w32::tp::Queue& thread_pool_queue (Engine& engine);

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
    friend class Promise;

        /* data. */
    public:
        /// @brief Intrusive reference-count.
        ///
        /// Each @c Promise object that points to this holds a reference to
        /// this structure.  In addition, an "artificial" reference is held to
        /// this object while the async operation is in progress: the reference
        /// count is increased before initiating the async operation and
        /// decreased *by the engine* when processing the notification received
        /// through the I/O completion port.
        ///
        /// @note Because all reference count changes are done in the (single)
        ///  thread in which all the engine's tasks are running, there is no
        ///  need to use (expensive) atomic operations to increment and
        ///  decrement the reference count.  However, this optimization makes
        ///  it unsafe to access the reference count in other threads -- make
        ///  sure this is reflected in the implementation.
        ///
        /// @invariant @c references>0
        int references;

        /// @brief State of the asynchronous operation.
        State state;

        /// @brief Engine to which the completion will be reported.
        ///
        /// @invariant @c engine!=0
        Engine * engine;

        /// @brief Task that initiated the request.
        ///
        /// @attention The task must be running inside the hub to which the
        ///  engine is associated.
        ///
        /// @invariant @c engine!=0
        Task * task;

        /// @brief Intrusive helper for O(1) dispatch in @c Promise::Set.
        ///
        /// Multiplexed wait for fulfillment of multiple promises normally
        /// requires O(N) dispatch when receiving the I/O completion
        /// notification.  This hint is the @c Promise::Data's position in the
        /// @c Promise::Set, so this O(N) lookup can be skipped.  This is
        /// attribute is set automatically when the promise is added to or
        /// removed from a wait set.
        ///
        /// The sentinel value @c MAXDWORD is used to indicate that the promise
        /// is not in use by a wait set.
        std::size_t hint;

        /// @brief Status of the asynchronous operation.
        ///
        /// @invariant Value is undefined until the promise is settled.
        w32::dword completion_status;

        /// @brief Result of the asynchronous operation.
        ///
        /// @invariant Value is undefined until the promise is settled.
        w32::dword completion_result;

        /// @brief "Virtual destructor".
        ///
        /// @invariant @c cleanup!=0
        void (*cleanup)(void*);

        /// @brief "Virtual method" that implements cancellation.
        ///
        /// @attention This "virtual method" is optional and the promise
        ///  implementation may choose not to provide it.
        bool (*cancel)(void*);

        /// @brief "Virtual method" that implements promise fulfillment.
        ///
        /// This "method" is called by hub when the completion notification is
        /// processed.  Promise implementations can use this hook to cleanup
        /// after the operation or prepare the results for the application.
        void (*post_process)(void*);

        /// @internal
        /// @brief Promise completion callback.
        ///
        /// When invoked, @c object is passed as the second parameter.
        ///
        /// @see object
        /// @todo Implement support for this.
        void(*method)(Promise&, void*);

        /// @internal
        /// @brief Application data for promise completion callback.
        ///
        /// @see method
        /// @todo Implement support for this.
        void * object;

        /* construction. */
    public:
        /*!
         * @brief Common initialization for promise data.
         *
         * @param[in] engine Engine through which the promise was issued.
         * @param[in] task Task to which the promise was issued.
         */
        Data (Engine& engine, Task& task);

        /* methods. */
    protected:
        /*!
         * @internal
         * @brief Type-safe wrapper for C-style "virtual destructor".
         *
         * This hack is required to work around the fact that @c Promise::Data
         * inherits from the @c OVERLAPPED structure and must remain a POD.
         *
         * @param[in,out] object  Promise implementation.
         *
         * @see Promise::~Promise
         */
        template<typename T> static void destroy (void * object) {
            delete static_cast<T*>(object);
        }

        /*!
         * @internal
         * @brief Type-safe wrapper for C-style "virtual method".
         *
         * This hack is required to work around the fact that @c Promise::Data
         * inherits from the @c OVERLAPPED structure and must remain a POD.
         *
         * @param[in,out] object  Promise implementation.
         * @return @c true if the promise was cancelled synchronously, @c false
         *  if the application should expect a cancellation (or completion)
         *  notification.
         *
         * @see Promise::cancel
         */
        template<typename T> static bool abort (void * object) {
            return (T::abort(*static_cast<T*>(object)));
        }

        /*!
         * @internal
         * @brief Workaround to inherit friendship in promise implementations.
         *
         * @param[in,out] engine Engine who'se completion port we need access.
         * @return @a engine's completion port.
         *
         * @see Promise::completion_port
         */
        static w32::io::CompletionPort& completion_port (Engine& engine);

        /*!
         * @internal
         * @brief Workaround to inherit friendship in promise implementations.
         *
         * @param[in,out] engine Engine who'se thread pool we need access.
         * @return @a engine's thread pool queue.
         *
         * @see Promise::thread_pool_queue
         */
        static w32::tp::Queue& thread_pool_queue (Engine& engine);
    };

    template<typename T, void(T::*M)(Promise&)>
    Promise& Promise::then (T& object)
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
        /*!
         * @internal
         * @brief Sentinel value for a promise's hint.
         *
         * When a promise is not part of a set, it's hint is set to this value.
         *
         * @return A sentiel value for the promise hint (@c MAXDWORD).
         */
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
        /*!
         * @brief Empty promise set.
         */
        Set ();

        /*!
         * @brief Remove all promises from the set.
         */
        ~Set ();

        /* methods. */
    public:
        /*!
         * @brief Number of promises in the set.
         */
        std::size_t size () const;

        /*!
         * @brief Number of @e settled promises in the set.
         */
        std::size_t done () const;

        /*!
         * @brief Add @a promise to the set.
         *
         * @note It is legal to add a settled promise to the set.  When adding
         *  a promise to the set, it will simply be part of the subset of
         *  settled promises and be immediately available for being removed via
         *  @c next().
         *
         * @param[in] promise Promise to add to the wait set.
         */
        void watch (const Promise& promise);

        /*!
         * @brief Remove @a promise from the set.
         *
         * @note It is legal to remove a settled promise from the set.
         *  Directly removing Settled promises will prevent them from being
         *  removed via @c next().
         *
         * @param[in] promise Promise to remove from the wait set.
         */
        void ignore (const Promise& promise);

        /*!
         * @brief Remove any settled promise from the set.
         *
         * @return When the set is empty, a null promise is returned.  If the
         *  set contains at least one settled promise, the set will pick one,
         *  remove it from the set and return it.
         *
         * @attention Settled promises are returned in @e approximately FIFO
         *  order.  Normally, settled promises are returned in the same order
         *  in which their completion notifications are received through the
         *  I/O completion port.  However, the O(1) dispatch implementation
         *  occasionally requires some special tricks that distort this order.
         *  Since the order in which asynchronous operations complete is
         *  already highly unpredictable, this additional twist should not be
         *  problematic.
         */
        Promise next ();

    private:
        /*!
         * @brief ...
         */
        void swap (std::size_t i, std::size_t j);
        void mark_as_fulfilled (Promise::Data * promise);
    };

}

#endif /* _cz_Promise_hpp__ */
