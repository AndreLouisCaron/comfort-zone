===============================================
  `comfort-zone`: C++ I/O toolkit for Windows
===============================================
:authors:
   André Caron
:contact: andre.l.caron@gmail.com


Description
===========

This library is an I/O toolkit for Windows.  It is based on strategic use of
fibers (userspace threads with cooperative scheduling), which allows it to
combine the efficiency of asynchronous I/O with the simplicity of
synchronous/blocking I/O programs.

It take its name from the following definition of "comfort zone":

   The comfort zone is a behavioural state within which a person operates in an
   anxiety-neutral condition, using a limited set of behaviours to deliver a
   steady level of performance, usually without a sense of risk.

So basically it aims at providing an C++ I/O toolkit for Windows with the
following qualities:

- **simple**: understands that mixing callbacks, mutable state and manual
  memory management is not for the faint of heart (even highly experienced
  programmers have a hard time getting it right);
- **complete**: understands that you need to do more than just asynchronous I/O
  on sockets and that _real_ server programs manipulate files, pipes,
  processes, etc.;
- **uniform**: understands that you don't want to have to understand special
  cases for each type of I/O device you're dealing with;
- **efficient** understands that subpar performance is never acceptable, most
  programs grow into new situations that shouldn't require a complete rewrite
  to change the I/O toolkit.

Basically, the I/O toolkit makes sure you deliver the best performance while
staying in your comfort zone.


Status
======

The library currently serves as a proof of concept, is in active development
and should be considered *highly experimental*.


Features
========

Key features include:

- uniform API for dealing with:
  * TCP sockets;
  * files;
  * anonymous pipes;
  * standard input, output and error streams;
  * mutexes, semaphores, timers, threads, processes and jobs;
  * filesystem changes; and
  * CPU-bound tasks.
- efficient multiplexed waits for awaiting completion of large numbers of
  asynchronous operations;
- coroutine-based parallel processing for effortless lock-free programming.

The promise-based API makes it easy to implement scatter-gather type parallel
programming: start N tasks in parallel and wait until any or all of them to
complete!  This is especially useful for dealing with timeouts on asynchronous
operations: in contast to callback-based APIs, promises really shine here.


Design
======

The `cz::Engine` uses an I/O completion port as a synchronization primitive:
this kernel-level queue for notifications makes a nice multiple-producer,
single-consumer queue to which *all* asynchronous operations report their
completion.

The `cz::Promise` is just a bookeeping tool for tracking the status of an
asynchronous operation.  When the operation completes, a completion
notification is posted to the I/O completion port.  Then, application can have
the `cz::Engine` process notifiations and update the state of the
`cz::Promise`.

The `cz::Promise::Set` is an intrusive container for `cz::Promise` objects.
The following operations on the set are O(1):
- watching a promise;
- ignoring a promise;
- querying how many promises have been settled; and
- removing one settled promise.
To implement these O(1) operations, the set uses a couple of tricks.  In
particular, it relies on promises knowing their own position inside this
container (that's why you can't use a naive `std::vector<cz::Promise>` for
implementing multiplexed waits).


Compiling
=========

#. Launch a `Microsoft Visual Studio`_ command prompt.

   The program and its dependencies are known to compile using Microsoft Visual
   Studio 9 (2008) and Microsoft Visual Studio 10 (2010) and may compile using
   some older versions.

#. Check out the source code and dependencies using Git_.

   ::

      rem: fetch the code.
      git clone git@github.com:AndreLouisCaron/comfort-zone.git
      cd comfort-zone

      rem: fetch its dependencies.
      git submodule init
      git submodule update

#. Generate NMake build scripts using CMake_.

   ::

      rem: generate build scripts.
      mkdir work && cd work
      cmake .. -G "NMake Makefiles"

   You can add the ``-DFIO_TRACE=ON`` option to enable logging of context
   switches, I/O operations and other key elements in execution.

#. Compile the library and, its demo programs and its tests.

   ::

      rem: build all targets.
      nmake

#. Compile the documentation.

   ::

      rem: build the optional documentation target.
      nmake help

#. Run the tests.

   ::

      rem: run all tests.
      nmake /A test

.. _`Microsoft Visual Studio`: http://www.microsoft.com/visualstudio/en-us
.. _Git: http://git-scm.com/
.. _CMake: http://www.cmake.org/


License
=======

This software is free for use in open source and commercial/closed-source
applications so long as you respect the terms of this 2-clause BSD license:

::

   Copyright (c) 2014, Andre Caron (andre.l.caron@gmail.com)
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

If you use and like this software, please let me know.  If you are willing to
announce it publicly, I can add you to a list of known users.  Such a list
usually helps in attracting attention and giving the project more credibility,
ensuring it keeps growing and stays bug free!
