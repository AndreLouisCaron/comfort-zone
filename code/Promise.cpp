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


#include "Promise.hpp"
#include "Hub.hpp"
#include "Engine.hpp"


#define debug_when(condition) cz_debug_when(condition)


namespace cz {

    Promise::Promise ()
        : myData(0)
    {
    }

    Promise::Promise (Data * data)
        : myData(data)
    {
        cz_debug_when(data == 0);
        cz_debug_when(data->state != Promise::Busy);
        cz_debug_when(data->references < 2);
        cz_debug_when(data->cleanup == &Data::destroy<Data>);
    }

    Promise::Promise (const Promise& rhs)
        : myData(rhs.myData)
    {
        if (myData != 0) {
            ++(myData->references);
        }
    }

    Promise::Promise (Engine& engine, Task& task)
        : myData(new Data(engine, task))
    {
    }

    Promise::~Promise ()
    {
        // Release the reference count and delete the data if this was the last
        // reference.  The last reference cannot be released while the async
        // operation is in progress; an "artificial" reference is created by
        // starting the operation and then released by processing the
        // completion notification, so this should never happen even if there
        // are no promises referring to the async request data anymore.
        if ((myData != 0) && (--(myData->references) == 0))
        {
            debug_when(myData->state == Busy);
            myData->cleanup(myData), myData = 0;  // "Virtual destructor".
        }
    }

    void Promise::swap (Promise& rhs) throw()
    {
        std::swap(myData, rhs.myData);
    }

    Promise& Promise::then (void(*method)(Promise&,void*), void* object)
    {
        if (myData == 0) {
            // TODO: throw!
        }
        myData->method = method;
        myData->object = object;
        return (*this);
    }

    bool Promise::forget ()
    {
        if (state() != Promise::Busy) {
            // TODO: throw!
        }

        // Cancellation may not be supported.  In that case, the task must wait
        // for the operation to complete.
        if (myData->cancel == 0) {
            return (false);
        }

        // Cancellation may be asynchronous.  In that case, the task must wait
        // for a completion notification, which may confirm successfull
        // cancellation or a proper completion (e.g. if the operation has
        // already completed and completion notification is already queued).
        if (!(myData->cancel)(myData)) {
            return (false);
        }

        // The cancellation was successful and the promise implementation
        // reported that we will not be receiving a completion notification.
        // In that case, we must settle the promise right away.
        if (myData->state == Promise::Busy) {
            myData->state = Promise::Dead;
        }
        return (true);
    }

    Promise::State Promise::state () const
    {
        if (!myData) {
            return (Null);
        }
        return (myData->state);
    }

    w32::io::CompletionPort& Promise::completion_port (Engine& engine)
    {
        return (engine.myCompletionPort);
    }

    w32::tp::Queue& Promise::thread_pool_queue (Engine& engine)
    {
        return (engine.myThreadPoolQueue);
    }

    Promise& Promise::operator= (const Promise& rhs)
    {
        Promise lhs(rhs);
        lhs.swap(*this);
        return (*this);
    }

    Promise::Data::Data (Engine& engine, Task& task)
    {
        ::ZeroMemory(this, sizeof(*this));
        this->references = 1;
        this->state = Idle;
        this->engine = &engine;
        this->task = &task;
        this->hint = Set::not_watched();
        this->completion_status = NO_ERROR;
        this->completion_result = MAXDWORD;
        this->cleanup = &destroy<Promise::Data>;
    }

    w32::io::CompletionPort& Promise::Data::completion_port (Engine& engine)
    {
        return (Promise::completion_port(engine));
    }

    w32::tp::Queue& Promise::Data::thread_pool_queue (Engine& engine)
    {
        return (engine.myThreadPoolQueue);
    }

    Promise::Set::Set ()
        : myPromises()
        , myMark(0)
    {
    }

    Promise::Set::~Set ()
    {
        // Stop watching all promises.
        for (std::size_t i=0; (i < myPromises.size()); ++i) {
            myPromises[i].myData->hint = not_watched();
        }
        myPromises.clear(), myMark = 0;
    }

    std::size_t Promise::Set::size () const
    {
        return (myPromises.size());
    }

    std::size_t Promise::Set::done () const
    {
        return (myPromises.size() - myMark);
    }

    void Promise::Set::watch (const Promise& promise)
    {
        if (promise.myData == 0)
        {
            // The application is trying to watch a promise that's never going
            // to complete.

            // TODO: throw!
        }

        // Special case for handling synchronous completion naturally.
        if (promise.myData->state == Done) {
            myPromises.push_back(promise);
            promise.myData->hint = myPromises.size() - 1;
            return;
        }

        if (promise.myData->state != Busy) {
            // Promise is never going to fulfill!
        }
        if (promise.myData->hint != not_watched())
        {
            // The application is trying to watch a promise that's already
            // being watch in another set.
        }

        // Make sure this task will be resumed when the promise is fulfilled.
        promise.myData->task = &Task::current();
        promise.myData->hint = myMark;

        if (myMark == myPromises.size()) {
            // No fulfilled promises, simply append.
            myPromises.push_back(promise), ++myMark;
        }
        else {
            // At least 1 fulfilled promise, append & swap with first fulfilled
            // promise to preserve partition invariant.
            //
            // NOTE: technically, this prevents FIFO processing of promises
            //       because it puts the most recently fulfilled promise at the
            //       end of the array.  However, the order of completion of all
            //       pending asynchronous requestsis always unpredictable so
            //       this shouldn't be noticeable.
            myPromises.push_back(promise);
            swap(myPromises.size()-1, myMark), ++myMark;
        }
    }

    void Promise::Set::ignore (const Promise& promise)
    {
        // TODO: handle promise.hint >= myMark!

        if ((promise.myData == 0) ||
            (promise.myData->hint == not_watched()))
        {
            // The application is trying to remove a promise that's not being
            // watched.
            return;
        }
        const std::size_t hint = promise.myData->hint;
        if ((hint >= myPromises.size()) ||
            (myPromises[hint].myData != promise.myData))
        {
            // The application is trying to remove a promise from the wrong set
            // (or there is a bug in our implementation of dispatch hints).

            // TODO: throw!
        }

        // Move the promise to the end of the partition and pop it off.  When
        // there are no fulfilled promises in the array, we need to use the
        // last item in the array directly.
        if (myMark == myPromises.size()) {
            swap(hint, myPromises.size()-1), --myMark;
        }
        else if (hint >= myMark) {
            swap(hint, myPromises.size()-1);
        }
        else {
            swap(hint, myMark-1);
            swap(myMark-1, myPromises.size()-1), --myMark;
        }
        myPromises.back().myData->hint = not_watched();
        myPromises.pop_back();
    }

    Promise Promise::Set::next ()
    {
        Promise promise;
        if (!myPromises.empty() && (myMark < myPromises.size()))
        {
            promise.swap(myPromises.back());
            debug_when(promise.myData->state != Done);
            promise.myData->hint = not_watched();
            myPromises.pop_back();
        }
        return (promise);
    }

    void Promise::Set::swap (std::size_t i, std::size_t j)
    {
        using std::swap;
        if (i == j) {
            return;
        }
        swap(myPromises[i], myPromises[j]);
        swap(myPromises[i].myData->hint, myPromises[j].myData->hint);
    }

    void Promise::Set::mark_as_fulfilled (Promise::Data * promise)
    {
        // This method can only be called by the engine when receiving a
        // completion notification that fulfills a promise that's being watched
        // by this set.
        debug_when(promise == 0);
        debug_when(promise->state != Done);
        debug_when(promise->hint == not_watched());
        debug_when(myMark == 0);

        // Move the promise to the end of the partition.  When there are no
        // fulfilled promises in the array, we need to use the last item in the
        // array directly.
        const std::size_t hint = promise->hint;
        debug_when((promise->hint >= myPromises.size()) ||
                   (myPromises[hint].myData != promise));
        if (myMark == myPromises.size()) {
            swap(myPromises.size()-1, hint);
        }
        else {
            swap(myMark-1, hint);
        }
        --myMark;
    }

}
