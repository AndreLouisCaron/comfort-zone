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

#include "Hub.hpp"
#include <algorithm>
#include <functional>

#include "trace.hpp"

namespace cz {

    Hub::Hub ()
        : myMaster(w32::mt::start_fiber())
        , mySlaves()
        , myQueue()
    {
        cz_trace("+hub(0x" << this << ")");
        cz_trace("@hub(0x" << this << ")");
    }

    Hub::~Hub ()
    {
        if (!mySlaves.empty()) {
            cz_trace("?hub(): slaves still alive.");
        }
        if (!myQueue.empty()) {
            cz_trace("?hub(): slaves still scheduled.");
        }
        cz_trace("-hub(0x" << this << ")");
    }

    bool Hub::running () const
    {
        return (w32::mt::Fiber::current() == myMaster);
    }

    void Hub::spawn (Task& task, SpawnMode mode)
    {
        Slave *const slave = new Slave(*this, task);
        task.start(slave);
        mySlaves.insert(slave);

        // Schedule first execution.
        if (mode == StartNow) {
            slave->resume();
        }
        if (mode == Deferred) {
            schedule(*slave);
        }
    }

    std::size_t Hub::active_slaves ()
    {
        return (mySlaves.size());
    }

    bool Hub::slaves_pending () const
    {
        return (!myQueue.empty());
    }

    void Hub::resume_pending_slave ()
    {
        // Fetch the next slave to schedule.
        if (myQueue.empty()) {
            return;
        }
        std::pair<Slave*,Request*> queued_slave = myQueue.front();
        myQueue.pop_front();
        if (!exists(queued_slave.first)) {
            cz_trace("?hub(0x" << this << "): unknown slave(0x" << queued_slave.first << ").");
            return;
        }

        // Have it run until it passes control back to us.
        queued_slave.first->resume(queued_slave.second);
    }

    void Hub::resume_pending_slaves ()
    {
        // "Steal" all queued slaves.  Note we intentionally iterate over a
        // different queue because we don't want to include any slaves that are
        // queued as a result of resuming the currently queued slaves.  This is
        // done to avoid starting I/O tasks.
        SlaveQueue queue;
        myQueue.swap(queue);

        // Resume all of them.
        SlaveQueue::iterator current = queue.begin();
        const SlaveQueue::iterator end = queue.end();
        for (; current != end; ++current) {
            current->first->resume(current->second);
        }
    }

    void Hub::schedule (Slave& slave, Request * request)
    {
        cz_trace(">hub(0x" << this << "): scheduling slave(0x" << &slave << ")");
        myQueue.push_back(std::make_pair(&slave, request));
    }

    void Hub::resume ()
    {
        myMaster.yield_to();

        // Slave has just been resumed.
        cz_trace("@slave(0x" << &Slave::self() << ")");
    }

    bool Hub::exists (Slave * slave) const
    {
        return (std::find(mySlaves.begin(),
                          mySlaves.end(),
                          slave) != mySlaves.end());
    }

    void Hub::forget (Slave * slave)
    {
        mySlaves.erase(slave);
    }

    Task::Task ()
        : mySlave(0)
        , myState(Offline)
    {
        cz_trace("+task(0x" << this << ")");
    }

    Task::~Task ()
    {
        // Note: slave is deleted by hub to free resources as soon as the task
        //       completes while still keeping application's task alive as long
        //       as necessary.

        if (mySlave != 0) {
            cz_trace("?task(0x" << this << "): slave is still alive.");
        }

        if (spawned() && !dead()) {
            cz_trace("?task(0x" << this << "): task not completed.");
        }

        cz_trace("-task(0x" << this << ")");
    }

    void Task::start (Hub::Slave * slave)
    {
        mySlave = slave, myState = Started;
    }

    bool Task::spawned () const
    {
        return (myState >= Started);
    }

    bool Task::running () const
    {
        return (myState == Running);
    }

    bool Task::paused () const
    {
        return (myState == Paused);
    }

    bool Task::closing () const
    {
        return (myState == Closing);
    }

    bool Task::dead () const
    {
        return (myState == Dead);
    }

    void Task::pause ()
    {
        if (myState >= Dead) {
            // TODO: throw something.
        }
        myState = Paused;
        mySlave->pause();
        myState = Running;
    }

// Note: 'this' in constructor initializer list is safe here: fiber must be
// explicitly resumed to actually start!
#ifdef _MSC_VER
#   pragma warning (push)
#   pragma warning (disable: 4355)
#endif

    Hub::Slave::Slave (Hub& hub, Task& task)
        : myHub(hub)
        , myTask(task)
        , myFiber(w32::mt::Fiber::function<&Slave::entry>(), this)
        , myLastRequest(0)
    {
        cz_trace("+slave(0x" << this << ")");
    }

#ifdef _MSC_VER
#   pragma warning (pop)
#endif

    Hub::Slave::~Slave ()
    {
        myHub.mySlaves.erase(this);
    }

    Hub& Hub::Slave::hub ()
    {
        return (myHub);
    }

    Task& Hub::Slave::task ()
    {
        return (myTask);
    }

    Hub::Slave& Hub::Slave::self ()
    {
        Slave *const slave = w32::mt::Fiber::context<Slave*>();
        if (slave == 0) {
            throw (std::exception("Not running inside a slave!"));
        }
        return (*slave);
    }

    void Hub::Slave::entry (w32::mt::Fiber::Context context)
    {
        // Note: this is the system entry point for the fiber, it's effectively
        //       the root of the call stack for the fiber.

        Slave& self = *static_cast<Slave*>(context);

        // Alive for the first time.
        cz_trace("@slave(0x" << &self << ")");

        // Enter application code.
        { const Task::Online _(self.myTask, self);
            try {
                self.myTask.run();
            }
            catch (const w32::Error& error)
            {
                cz_trace("?slave(0x" << self.myFiber.handle()
                    << "): aborted by uncaught Win32 error: " << error << ".");
            }
            catch (...)
            {
                cz_trace("?slave(0x" << self.myFiber.handle()
                    << "): aborted by uncaught exception.");
            }
        }

        // Just completed execution.
        cz_trace("!slave(0x" << &self << ")");

        // Let the application know that the hub has one less slave.
        self.myHub.forget(&self);

        // Exiting a fiber exits the thread.  Make sure everything is popped
        // off the stack and then return control to the hub (it will detect
        // that the slave has completed and kill this fiber).
        self.myHub.resume();

        // Make sure the fiber never exits and *always* returns control to the
        // hub, even if it is accidentally resumed after it has supposedly
        // completed.
        while (true) {
            cz_trace("?slave(0x" << &self << "): already dead!");
            self.myHub.resume();
        }
    }

    void Hub::Slave::resume (Request * request)
    {
        // Note: this function is always called by the hub!
        if (!myHub.running()) {
            throw (std::exception("Only the hub can resume slaves!"));
        }

        myLastRequest = request;
        myFiber.yield_to();
        myLastRequest = 0;

        // Hub has just been resumed.
        cz_trace("@hub(0x" << &myHub << ")");

        // Check if the fiber has just completed execution.
        if (myTask.closing()) {
            // TODO: check for exception in fiber.
            cz_trace("-slave(0x" << myFiber.handle() << ")");

            // Mark the task as collected and kill the slave.
            myTask.myState = Task::Dead;
            myTask.mySlave = 0;

            myHub.mySlaves.erase(this); delete this;
        }
    }

    void Hub::Slave::resume_later ()
    {
        myHub.schedule(*this);
    }

    void Hub::Slave::pause ()
    {
        if (this != &self()) {
            throw (std::exception("The slave must pause itself!"));
        }

        // Put current fiber in queue to continue execution later, then return
        // control to the hub until it's ready to resume us.
        resume_later(), myHub.resume();

        cz_trace("@slave(0x" << myFiber.handle() << ")");
    }

    Request * Hub::Slave::last_request () const
    {
        return (myLastRequest);
    }

}
