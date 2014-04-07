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


// Suggested reading:
// - http://www.serverframework.com/speeding-up-socket-server-connections-with-acceptex.html


#include "comfort-zone.hpp"

#include <memory>
#include <iostream>
#include <set>
#include <queue>


namespace echo {


    class Connection;


    /*!
     * @brief Application state (shared between all tasks).
     */
    class Server
    {
        /* data. */
    private:
        cz::Engine& myEngine;

        // Signal when we should shut down.  All tasks will wait for this in
        // parallel with their other asynchronous tasks so that they can block
        // yet stay reactive to important server control events.
        w32::mt::ManualResetEvent myShutdownSignal;

        // Hols all `Connection` objects.
        std::set<Connection*> myConnections;

        int myConnectionIDGenerator;

        /* construction. */
    public:
        Server (cz::Engine& engine)
            : myEngine(engine)
            , myConnectionIDGenerator(0)
        {}

    private:
        Server (const Server&);

        /* methods. */
    public:
        cz::Engine& engine ()
        {
            return (myEngine);
        }

        cz::Hub& hub ()
        {
            return (myEngine.hub());
        }

        void shutdown ()
        {
            myShutdownSignal.set();
        }

        w32::mt::ManualResetEvent& shutdown_signal ()
        {
            return (myShutdownSignal);
        }

        int next_connection_id ()
        {
            return (++myConnectionIDGenerator);
        }

        w32::dword initial_payload_timeout () const
        {
            return (5);
        }

        bool new_socket_aborted_listen () const
        {
            return (false);
        }

        int total_connections () const
        {
            // Current value of the generator is the number of connections
            // we've accepted so far.
            return (myConnectionIDGenerator);
        }

        void add_connection (Connection * connection)
        {
            myConnections.insert(connection);
        }

        // Defined below (circular dependency).
        void del_connection (Connection * connection);

        w32::net::ipv4::EndPoint host_endpoint () const
        {
            // TODO: grab this from command-line arguments or a configuration
            //       file.
            return (w32::net::ipv4::EndPoint(
                        w32::net::ipv4::Address::local(), 9000));
        }

        w32::dword get_timeout () const
        {
            // TODO: grab this from command-line arguments or a configuration
            //       file.
            return (15*1000); // in milliseconds!
        }

        w32::size_t buffer_size () const
        {
            // TODO: grab this from command-line arguments or a configuration
            //       file.
            return (4*1024);
        }

        /* operators. */
    private:
        Server& operator= (const Server&);
    };


    /*!
     * @brief Client state (one per TCP connection).
     *
     * @todo Use reuse-socket feature and recycle `Connection` objects,
     *  reducing the need for dynamic allocations and improving the process
     *  longevity.
     */
    class Connection
    {
        /* data. */
    private:
        Server& myServer;

        int myID;

        // Counts number of TCP connections on same instance.
        int myTotalConnections;

        // Used to check if state transitions are safe.
        unsigned int myActiveRequests;
        static const unsigned int NEW = 1 << 0; // myNewRequest.
        static const unsigned int DEL = 1 << 1; // myDelRequest.
        static const unsigned int GET = 1 << 2; // myGetRequest.
        static const unsigned int GTO = 1 << 3; // myGetTimeout.
        static const unsigned int PUT = 1 << 4; // myPutRequest.
        static const unsigned int PTO = 1 << 5; // myPutTimeout.

        // I/O stuff.  Note that we may have overlapping async get and put
        // operations on the same connection at any given time.
        cz::SocketChannel myChannel;
        cz::AcceptRequest     myNewRequest;
        cz::DisconnectRequest myDelRequest;
        cz::SocketGetRequest  myGetRequest;
        cz::SocketPutRequest  myPutRequest;

        // Force reads to timeout on (deliberately) slow clients to avoid
        // performance hit when there are lots of slow clients.
        //
        // TODO: implement put timeout to protect against slow readers.
        cz::TimeRequest myGetTimeout;

        // Storage locations for I/O.  Zero-copy policy is implemented using
        // O(1) swap of buffers, resulting in a nice circular queue of outbound
        // data.  After a read, if both buffers contain data, we stop reading
        // and the network stack will use TCP flow control to block our peer's
        // subsequent pending operations.
        cz::Buffer myGetBuffer;
        cz::Buffer myPutBuffer;

        // State for the state machine!
        void(Connection::*myState)(cz::Request*);

        /* construction. */
    public:
        Connection (Server& server, w32::net::tcp::Listener& listener)
            : myServer(server)
            , myID(0)
            , myTotalConnections(0)
            , myActiveRequests(0)
            , myChannel(myServer.engine())
              // I/O buffers.
            , myGetBuffer(myServer.buffer_size())
            , myPutBuffer(myServer.buffer_size())
              // Note: `this` pointer will not be used until the requests
              //       complete, and they cannot complete until this thread
              //       checks the completion port for notifications, so using
              //       `this` pointer is safe here; ignore compiler warnings.
            , myNewRequest(myServer.engine(), listener,
                           myChannel, myGetBuffer, this)
            , myGetRequest(myServer.engine(), myChannel, this)
            , myDelRequest(myServer.engine(), myChannel, this)
            , myPutRequest(myServer.engine(), myChannel, this)
            , myGetTimeout(myServer.engine(), myServer.get_timeout(), this)
              // Connection state.
            , myState(&Connection::disconnected)
        {
        }

        /* methods. */
    public:
        int id () const {
            return (myID);
        }

        // Start watching for an incoming connection.
        void start_accept ()
        {
            if (myActiveRequests != 0) {
                ::DebugBreak();
            }

            if (myTotalConnections > 0) {
                std::cerr
                    << myID << ": reusing socket."
                    << std::endl;
            }

            // Make sure we don't end up with data leftover from the previous
            // connection!
            myGetBuffer.reset();
            myPutBuffer.reset();

            myID = myServer.next_connection_id();
            std::cerr
                << myID << ": +new"
                << std::endl;
            myNewRequest.start();
            myActiveRequests |= NEW;
        }

        bool accept_timed_out () const
        {
            const w32::dword seconds = myNewRequest.connected_since();
            return ((seconds != cz::AcceptRequest::not_connected)
                 && (seconds >= myServer.initial_payload_timeout()));
        }

        void abort_accept ()
        {
            // Sanity check.
            if (myActiveRequests != NEW) {
                ::DebugBreak();
            }

            std::cerr
                << myID << ": #new => client not sending!"
                << std::endl;
            if (myNewRequest.abort()) {
                myActiveRequests &= ~NEW;
                std::cerr
                    << myID << ": #new => accept aborted!"
                    << std::endl;
            }
            else {
                // Client ended up sending data just as we were about to
                // cancel.  The accept request notification is already queued,
                // so we'll decide to wait for that to be dispatched instead
                // of cancelling the accept.
                //
                // NOTE: this should be super hard to reproduce in practice!

                // Sanity check.
                if (myState != &Connection::disconnected) {
                    ::DebugBreak();
                }

                return;
            }

            if (myServer.new_socket_aborted_listen())
            {
                std::cerr
                    << myID << ": #new => *not* re-using socket!"
                    << std::endl;

                // Close the socket and create a new socket.  At least, this
                // will allow us to reuse the `Connection` instance, and we'll
                // end up in the "standard" behavior of creating a new socket
                // for each TCP connection as the worst case behavior.
                //
                // NOTE: this will send an RST packet to tell the peer that we
                //       disconnected.  The point here is that unless the
                //       client is polite and closes their socket in response
                //       to this (unlikely since we're forcibly
                //       disconnecting them), the async disconnect request will
                //       not complete until the TIME_WAIT state is over (2*MSL
                //       is 4 minutes under a default system configuration).
                //
                // NOTE: the socket will still linger in the kernel until the
                //       TIME_WAIT state completes, but at least the server can
                //       reuse the buffers and other allocated resources.
                myChannel.reset();

                // NOTE: async request objects have a reference to `myChannel`
                //       and will automatically start using the new socket!

                // NOTE: async accept request will never complete (aborted
                //       above).
                myState = &Connection::disconnected;
            }
            else {
                std::cerr
                    << myID << ": #new => re-using socket!"
                    << std::endl;

                // Force connection reset when we disconnect or else we will
                // end up waiting for the peer to send a shutdown and/or close
                // their socket and the `DisconnectEx()` call will never
                // return, meaning the malicious client would still achieve a
                // denial of service!
                //
                // TODO: run manual test to confirm if this actually helps!
                myChannel.socket().linger(0);

                // NOTE: unless the peer promptly closes their socket, this call
                //       will put the socket in the "TIME_WAIT" state and the
                //       disconnect request will take up to 4 minutes (2*MSL) to
                //       complete.
                initiate_disconnection();
            }
        }

        // Start disconnecting client!
        void start_close ()
        {
        }

        bool connected () const {
            return (myState != &Connection::disconnected);
        }

        // Entry point for the main notification loop.
        void process_notification (cz::Request * request)
        {
            std::cerr
                << myID << ": ----"
                << std::endl;

            // Sanity check.
            if (myActiveRequests == 0) {
                ::DebugBreak();
            }

            // Dispatch to  the current state.  Note that if the state becomes
            // aware of any transition that should be made, it will simply
            // assign `myState` and the next notification will be automagically
            // dispatched to the new state.  In other words, `myState` changes
            // every once in a while and we don't need to worry about it's
            // current value here.
            return ((this->*myState)(request));
        }

    private:
        void initiate_disconnection ()
        {
            if ((myActiveRequests & DEL) != 0) {
                ::DebugBreak();
                return;
            }

            std::cerr
                << myID << ": +del"
                << std::endl;
            myDelRequest.start(true);
            myActiveRequests |= DEL;
            myState = &Connection::disconnecting;
        }

        void initiate_get ()
        {
            // Sanity check.
            if ((myActiveRequests & GET) != 0) {
                ::DebugBreak();
            }
            if (myGetBuffer.gsize() == 0) {
                ::DebugBreak();
            }

            std::cerr
                << myID << ": +get(" << myGetBuffer.gsize() << ")"
                << std::endl;

            myGetRequest.start(myGetBuffer.gbase(),
                               myGetBuffer.gsize());
            myActiveRequests |= GET;

            if (myGetRequest.ready()) {
                // WARNING: synchronous completion also triggers completion
                //          notification; we can't handle synchronous
                //          completion correctly!
                std::cerr
                    << myID << ": *get => synchronous?"
                    << std::endl;
            }
            else {
                // Don't allow slow writers to hold the connection.
                myGetTimeout.start();
                myActiveRequests |= GTO;
            }
        }

        void complete_get ()
        {
            // Sanity check.
            if ((myActiveRequests & GET) == 0) {
                ::DebugBreak();
            }
            if (!myGetRequest.ready()) {
                ::DebugBreak();
            }

            myActiveRequests &= ~GET;

            const size_t xferred = myGetRequest.result();
            const bool eof = myGetRequest.eof();
            myGetRequest.reset();
            std::cerr
                << myID << ": -get => " << xferred
                << std::endl;
            if (xferred > 0) {
                myGetBuffer.gused(xferred);
                // NOTE: buffer is full or we got everything that has
                //       already arrived.  Let us get unblocked when
                //       we finish processing the initial payload.

                if ((myActiveRequests & PUT) == 0)
                {
                    // If we stopped writing because the buffer was empty, queue
                    // the data we just received for output and start writing
                    // again.
                    if (myPutBuffer.pdone())
                    {
                        myGetBuffer.swap(myPutBuffer);
                        myGetBuffer.pack();
                        initiate_get();
                    }
                    initiate_put();
                }
            }
            else {
                // Close the connection unless the peer initiated a graceful
                // shutdown procedure AND we have queued outbound data, in
                // which case we should keep sending whatever has been queued.
                if (!eof && !myPutBuffer.pdone()) {
                    initiate_put();
                    myState = &Connection::exhausting;
                }
            }

            if ((myActiveRequests & GTO) != 0)
            {
                // Cancel the timer, we'll need to restart it on the next get.
                //
                // If we cannot abort the timeout, a notification has already
                // been queued and there's nothing we can do about it.  In most
                // cases, `eof == true` at this point, but there's a small race
                // condition making it possible that we received data from the
                // client just as the timeout was going to expire (the two
                // notifications can be queued almost at the same time and
                // arrive in either order).
                if (myGetTimeout.abort()) {
                    myGetTimeout.reset();
                    myActiveRequests &= ~GTO;
                }
                else {
                    // NOTE: client ended up sending data just as we were about to
                    //       cancel.  The timeout request notification is already
                    //       queued, so we'll decide to wait for that to be
                    //       dispatched.
                    myState = &Connection::disconnecting;
                }
            }

            // NOTE: issuing another read here (if possible) would lead to
            //       reduced latency as we'd be trying to fill the get buffer
            //       instead of trying to empty the put buffer.  If the peer
            //       waits for the reply instead of sending more data, we would
            //       end up in a deadlock which would then eventally be
            //       resolved by a timeout.  Prefer trying to empty the put
            //       buffer instead :-)
        }

        void initiate_put ()
        {
            // Sanity check.
            if ((myActiveRequests & PUT) != 0) {
                ::DebugBreak();
            }
            if (myPutBuffer.psize() == 0) {
                ::DebugBreak();
            }

            std::cerr
                << myID << ": +put(" << myPutBuffer.psize() << ")"
                << std::endl;

            myPutRequest.start(myPutBuffer.pbase(),
                               myPutBuffer.psize());
            myActiveRequests |= PUT;

            if (myPutRequest.ready()) {
                // WARNING: synchronous completion also triggers completion
                //          notification; we can't handle synchronous
                //          completion correctly!
                std::cerr
                    << myID << ": *put => synchronous?"
                    << std::endl;
            }
            else {
                // TODO: don't allow slow readers to hold the connection.
            }
        }

        void complete_put ()
        {
            // Sanity check.
            if ((myActiveRequests & PUT) == 0) {
                ::DebugBreak();
            }
            if (!myPutRequest.ready()) {
                ::DebugBreak();
            }

            myActiveRequests &= ~PUT;

            const size_t xferred = myPutRequest.result();
            myPutRequest.reset();
            std::cerr
                << myID << ": -put => " << xferred
                << std::endl;
            if (xferred > 0) {
                myPutBuffer.pused(xferred);
                if (myPutBuffer.pdone())
                {
                    // Resume reader if it was paused blocked because its queue
                    // was full.
                    if ((myGetBuffer.used() > 0) &&
                        ((myActiveRequests & GET) == 0))
                    {
                        myGetBuffer.swap(myPutBuffer);
                        myGetBuffer.pack();
                        initiate_get();
                    }
                }
                else {
                    // We still have data to send, 
                    initiate_put();
                }
            }
            else {
                // Disconnect if we can't write anymore.
                //
                // TODO: figure out when this happens?
                initiate_disconnection();
            }

            // Keep writing if we can.
            //
            // TODO: figure out if this makes any sense!
            if (((myActiveRequests & DEL) == 0) && !myPutBuffer.pdone()) {
                initiate_put();
            }
        }

        // Waiting for a connection.
        //
        // Possible transitions:
        // - disconnected: the connection has been established for a while but
        //   the peer is not sending any data;
        // - full_duplex: @a request told us the connection is established and
        //   we got our first payload.
        void disconnected (cz::Request * request)
        {
            // Sanity check.
            if ((myActiveRequests & ~NEW) != 0) {
                ::DebugBreak();
            }

            if (myNewRequest.is(request))
            {
                myActiveRequests &= ~NEW;

                // Note: get buffer will be updated automatically to reflect
                //       the amount of data already received from the client.
                if (!myNewRequest.result()) {
                    initiate_disconnection();
                    return;
                }

                std::cerr
                    << myID << ": -new => echo://" << myNewRequest.peer()
                    << std::endl;

                // No more use for the accept request.  It will be reused if we
                // decide to accept another connection on the same socket.
                myNewRequest.reset();

                // Keep track of number of TCP connections!
                ++myTotalConnections;

                // Prepare to write whatever we already got.
                myPutBuffer.swap(myGetBuffer);

                // We're connected, so start reacting "normally" unless told
                // otherwise.
                myState = &Connection::full_duplex;

                // Start reading ASAP.
                initiate_get();

                // NOTE: try to write only if we successfully managed to issue
                //       a read request (if the socket gets disconnected before
                //       we process the accept request notification, the send
                //       call doesn't seem to fail -- darn Windows I/O
                //       inconsistencies).
                if (((myActiveRequests & GET) != 0) && !myPutBuffer.pdone()) {
                    initiate_put();
                }

                // If we couldn't launch anything, get rid of this connection.
                if (myActiveRequests == 0) {
                    initiate_disconnection();
                }
            }
        }

        // Disconnection in progress (for socket reuse).  Basically, we can end
        // up here from any number of states.  The purpose of this state is to
        // wait until all async requests complete no matter who sent us here.
        //
        // - disconnected: @a request told us that the disconnection process
        //   has completed and our stream is ready to start listening for
        //   another connection.
        void disconnecting (cz::Request * request)
        {
            // Sanity check.
            if ((myActiveRequests & ~(NEW|DEL|GTO|PUT|PTO)) != 0) {
                ::DebugBreak();
            }

            // The "normal" case for this state is that we're waiting for the
            // disconnection to complete.
            if (myDelRequest.is(request))
            {
                myActiveRequests &= ~DEL;
                std::cerr
                    << myID << ": -del"
                    << std::endl;
                myDelRequest.result();
                myDelRequest.reset();
            }

            // If the peer is not sending data, they will be disconnected,
            // but the pending `AcceptRequest` will still be in progress
            // and will eventually fail.  This is where we end up!
            if (myNewRequest.is(request))
            {
                myActiveRequests &= ~NEW;
                std::cerr
                    << myID << ": -new => aborted!"
                    << std::endl;
                myNewRequest.result();
                myNewRequest.reset();
            }

            // It's possible we end uptrying to disconnect before pending put
            // request completes, make sure we allow those to complete before
            // we mark the connection as disconnected.
            if (myPutRequest.is(request))
            {
                myActiveRequests &= ~PUT;
                std::cerr
                    << myID << ": -put => ignored!"
                    << std::endl;
                myPutRequest.reset();
            }

            // It's possible we end uptrying to disconnect before pending put
            // request completes, make sure we allow those to complete before
            // we mark the connection as disconnected.
            if (myGetTimeout.is(request))
            {
                myActiveRequests &= ~GTO;
                std::cerr
                    << myID << ": -get timeout => ignored!"
                    << std::endl;
                myGetTimeout.reset();
            }

            // TODO: clear put timeout request.

            // Confirm disconnection when we're sure there are no more active
            // requests.
            //
            // NOTE: the server will decide if another incomming connection
            //       should be accepted by calling `start_accept()`.
            if ((myActiveRequests & (NEW|DEL|GTO|PUT|PTO)) == 0) {
                myState = &Connection::disconnected;
            }
        }

        // Peer has stopped sending data, we're now trying to get rid of the
        // remaining data queued for sending.
        //
        // Possible transitions:
        // - disconnecting: @a request was the last put operation and we have
        //   nothing left to send;
        // - disconnected: @a request told us that the peer is no longer
        //   connected and we just dropped whatever data that was still queued.
        void exhausting (cz::Request * request)
        {
            // Sanity check.
            if ((myActiveRequests & ~(PUT|PTO)) != 0) {
                ::DebugBreak();
            }

            if (myPutRequest.is(request)) {
                complete_put();
            }

            // Initiate disconnection once we have stopped writing.
            if ((myActiveRequests & ~(PUT|PTO)) == 0) {
                initiate_disconnection();
            }
        }

        // Data is currently expected to flow in both directions.
        //
        // Possible transitions:
        // - exhausting: @a request was the last get operation and we have data
        //   left to put;
        // - disconnecting: @a request was the last get operation and we have
        //   nothing left to send;
        // - disconnected: @a request told us that the peer is no longer
        //   connected and we just dropped whatever data that was still queued.
        void full_duplex (cz::Request * request)
        {
            // Sanity check.
            if ((myActiveRequests & ~(GET|GTO|PUT|PTO)) != 0) {
                ::DebugBreak();
            }

            if (myGetTimeout.is(request))
            {
                myActiveRequests &= ~GTO;

                // Cleanup after async operation.
                if (!myGetTimeout.ready()) {
                    ::DebugBreak();
                }
                myGetTimeout.reset();

                if ((myActiveRequests & GET) != 0) {
                    ::DebugBreak();
                }

                std::cerr
                    << myID << ": timed out."
                    << std::endl;

                // Cancel pending get operation.  A follow-up notification will
                // be posted to indicate that the read operation has succeeded
                // (e.g. if it has already been queued) or failed to complete.
                myGetRequest.abort();
                initiate_disconnection();
            }

            if (myGetRequest.is(request)) {
                complete_get();
            }

            if (myPutRequest.is(request)) {
                complete_put();
            }

            // If we have nothing left to do (peer has shutdown and we have no
            // queued data to send), initiate disconnection.
            if ((myActiveRequests & (GET|GTO|PUT|PTO)) == 0) {
                initiate_disconnection();
            }
        }
    };


    void Server::del_connection (Connection * connection)
    {
        std::cerr
            << connection->id() << ": kaboom!"
            << std::endl;
        myConnections.erase(connection); delete connection;
    }


    /*!
     * @brief Task that will process control commands from the standard input.
     */
    class ControlTask :
        public cz::Task
    {
        /* data. */
    private:
        Server& myServer;

        /* construction. */
    public:
        ControlTask (Server& server)
            : myServer(server)
        {}

        /* overrides. */
    protected:
        virtual void run ()
        {
            // NOTE: in contrast with other tasks, this one does not block on
            //       the `shutdown_event` because it's the one responsible for
            //       triggering the event (in a way, the `shutdown_event` is a
            //       way to forward the fact that this task is going to shut
            //       down).

            // Get standard input & output streams.
            cz::BlockingReader reader(myServer.engine(),
                                      w32::io::StandardInput());
            cz::BlockingWriter writer(myServer.engine(),
                                      w32::io::StandardOutput());

            // Input buffer.
            char data[1024]; cz::size_t size = sizeof(data);

            // Exhaust input stream.
            for (cz::size_t used=0; ((used=reader.get(data, size)) > 0);)
            {
                for (cz::size_t pass=0; (pass < used);) {
                    pass += writer.put(data+pass, used-pass);
                }
            }

            // Force other tasks to shut down.
            myServer.shutdown();

            // TODO: make sure the shutdown event gets called even if an
            // exception is triggered.
        }
    };


    void move_connection(std::vector<Connection*>& lhs,
                         std::vector<Connection*>& rhs,
                         Connection * connection)
    {
        std::vector<Connection*>::iterator position =
            std::find(rhs.begin(), rhs.end(), connection);
        if (position == rhs.end())
        {
            std::cerr
                << "Connection #" << connection->id()
                << " is in the wrong set."
                << std::endl;
            return;
        }
        lhs.push_back(connection), rhs.erase(position);
    }


    /*!
     * @brief Task that will accept TCP connections and perform I/O.
     */
    class ServiceTask :
        public cz::Task
    {
        /* data. */
    private:
        Server& myServer;

        cz::Listener myListener;

        // Pool of connections.
        std::vector<Connection*> myUnusedConnections; // disconnected.
        std::vector<Connection*> myListenConnections; // waiting on connection.
        std::vector<Connection*> myActiveConnections; // connected.

        /* construction. */
    public:
        ServiceTask (Server& server)
            : myServer(server)
            , myListener(myServer.engine(), myServer.host_endpoint())
        {}

        /* methods. */
    private:
        void new_pending_connection ()
        {
            // Allocate the connection.
            std::auto_ptr<Connection> _(
                new Connection(myServer, myListener.socket()));
            Connection *const connection = _.get();

            // Transfer ownership.
            myServer.add_connection(connection), _.release();

            // Track the connection.
            myListenConnections.push_back(connection);
            std::cerr
                << connection->id() << ": (new) => listen"
                << std::endl;

            // Tell it to start waiting for an incoming connection.
            connection->start_accept();
        }

        void check_slow_accepts ()
        {
            // TODO: try to avoid this O(n^2) loop to avoid decreased
            //       responsiveness when lots of clients are doing this!

            // NOTE: even though new accept requests insert in `Connection`
            //       instances at the end of `myListenConnetions`, meaning that
            //       that the 1st entries are the oldest and suggesting that
            //       they would timeout first, there is no documented guarantee
            //       that the system will satisfy `AcceptEx()` calls in the
            //       order in which they were issued.

            std::vector<Connection*>::iterator position =
                myListenConnections.begin();
            for (; position != myListenConnections.end(); ++position)
            {
                Connection *const connection = *position;
                if (connection->accept_timed_out()) {
                    connection->abort_accept();
                }
            }
        }

        /* overrides. */
    protected:
        virtual void run ()
        {
            // Listen for server control events.
            cz::WaitRequest shutdown_wait(myServer.engine(),
                                          myServer.shutdown_signal());
            shutdown_wait.start();

            // Passing a data buffer to the asynchronous accept request may
            // open the possibility of denial of service (DoS) attacks from
            // malicious clients that don't send ay data after they connect.
            //
            // Protect against this type of attack by asking the system to let
            // us know when new connections arrive while all pending accept
            // requests are already busy with connections.
            w32::mt::ManualResetEvent connection_ready;
            cz::WaitRequest accept_watcher(myServer.engine(),
                                           connection_ready);
            accept_watcher.start();
            myListener.socket().select(connection_ready,
                                       w32::net::Event::accept());

            // Initially, all we do is wait for incoming connections.
            new_pending_connection();

            // TODO: rewrite this condition, the task should complete only when
            //       all the connections have been deleted!
            while (!shutdown_wait.ready())
            {
                // OK, one or more asynchronous tasks are running.  Block until
                // one of them finishes.
                myServer.hub().resume();

                // TODO: implement `Task::self()` to replace this (applications
                //       shouldn't use slaves directly).
                cz::Request *const request =
                    cz::Hub::Slave::self().last_request();

                if (accept_watcher.is(request))
                {
                    std::cerr
                        << "0: queuing another accept request!"
                        << std::endl;

                    // All our accept requests are connected but blocked
                    // waiting for data.  We got signaled because, we have at
                    // least one more connection waiting in the accept socket's
                    // backlog.

                    // OK, acknowledge that we've consumed the event.
                    //
                    // Note: the network event is level-triggered, so we don't
                    //       need to worry about missing events for connection
                    //       attemps while we're consuming the event.  If an
                    //       incoming connection occurs at this point, the
                    //       system will delay signaling the event until we
                    //       call `accept_request.start()` again.
                    accept_watcher.reset();
                    connection_ready.reset();

                    // Check how long the existing accept requests have been
                    // waiting for data and cancel them if necesary.
                    check_slow_accepts();

                    // TODO: try to limit the total number of connections.

                    // Launch a new accept request.
                    if (myUnusedConnections.empty()) {
                        new_pending_connection(); // for pending request.
                        //new_pending_connection(); // TODO: queue ahead.
                    }
                    else {
                        // Note: using first item in the set for quick
                        //       `std::find()` in `move_connection()`.
                        Connection *const connection =
                            myUnusedConnections.front();

                        connection->start_accept();

                        std::cerr
                            << connection->id() << ": unused => listen"
                            << std::endl;
                        move_connection(myListenConnections,
                                        myUnusedConnections, connection);
                    }

                    // OK, start watching the event again.
                    //
                    // Note: this needs to be started after the `AcceptEx` call
                    //       because the event is level-triggered.
                    accept_watcher.start();
                }
                else if (shutdown_wait.is(request))
                {
                    // TODO: start cancelling pending accept requests.

                    // TODO: let all connections know they should start closing
                    //       their sockets.
                }
                else
                {
                    // Connections pass a pointer to themselves in all they
                    // asynchronous requests so that we can perform O(1)
                    // dispatch, avoiding a O(n) loop to found out which
                    // connection started the request that just completed.
                    //
                    // Only `Connection` objects use a context pointer
                    // because there is a variable number of them, so it's
                    // safe to assume here that we can safely cast.
                    //
                    // NOTE: will not enter this conditional block when the
                    //       `shutdown_event` signal is received because it
                    //       doesn't use the context pointer.
                    if (Connection *const connection =
                        request->context<Connection>())
                    {
                        // TODO: make sure aborted accept requests are properly
                        //       returned to the connection pool.  At the
                        //       moment, they get lost in the listening
                        //       connetions set!

                        // If the socket is not currently connected, move it to
                        // the active connections set.
                        if (!connection->connected()) {
                            std::cerr
                                << connection->id() << ": listen => active"
                                << std::endl;
                            move_connection(myActiveConnections,
                                            myListenConnections, connection);
                        }

                        // Let the connection act on the request completion.
                        connection->process_notification(request);

                        // If it just diconnected, move it back to the pending
                        // accepts list until is reconnects.
                        if (!connection->connected()) {
                            // TODO: if the accept request was aborted, we'll
                            //       end up here, but the connection will still
                            //       be in the "listening connections" set.

                            std::cerr
                                << connection->id() << ": active => unused"
                                << std::endl;
                            move_connection(myUnusedConnections,
                                            myActiveConnections, connection);
                        }
                    }
                }
            }

            // TODO: abort accept requests before exiting or else we may run
            //       into an access violation if the request eventually
            //       completes!
        }
    };


    /*!
     * @brief Echo server demo program.
     */
    int server_main (int argc, wchar_t ** argv)
    {
        // Start an I/O application!
        cz::Hub hub;
        cz::Engine engine(hub);
        Server server(engine);

        // Start initial tasks.
        ServiceTask service(server);
        ControlTask control(server);

        // Start all tasks.  Make sure each one is started and blocks on its
        // first operation (or, at the very least, the server shutdown signal)
        // before returning here.
        hub.spawn(service, cz::Hub::StartNow);
        hub.spawn(control, cz::Hub::StartNow);

        while (hub.active_slaves() > 0)
        {
            // Process all available notifications.  This has
            // the effect of queueing slaves for execution.
            engine.process_notifications();

            // If we don't have any work to do, wait for a
            // notification rather than spin needlessly.
            if (!hub.slaves_pending()) {
                engine.wait_for_notification(); continue;
            }

            // Execute all currently queued work.  Any slaves queued
            // during this function's execution will not be considered
            // for execution until this function is called again (this
            // prevents starving I/O tasks).
            hub.resume_pending_slaves();
        }

        return (EXIT_SUCCESS);
    }

}


#include <w32/app/console-program.hpp>

namespace {

    int run (int argc, wchar_t ** argv)
    {
        const w32::net::Context _;
        return (echo::server_main(argc, argv));
    }

}

#include <w32/app/console-program.cpp>
