==========================
  `fio`: Fiber-based I/O
==========================
:authors:
   André Caron
:contact: andre.l.caron@gmail.com


Description
===========

This library is an I/O framework for Windows.  It is based on strategic use of
fibers (userspace threads with cooperative scheduling), which allows it to
combine the efficiency of asynchronous I/O with the simplicity of
synchronous/blocking I/O programs.


Compiling
=========

#. Launch a `Microsoft Visual Studio`_ command prompt.

   The program and its dependencies are known to compile using Microsoft Visual
   Studio 9 (2008) and Microsoft Visual Studio 10 (2010) and may compile using
   some older versions.

#. Check out the source code and dependencies using Git_.

   ::

      git clone git@github.com:AndreLouisCaron/fio.git
      cd fio
      git submodule init
      git submodule update

#. Generate NMake build scripts using CMake_.

   ::

      mkdir work && cd work
      cmake -G "NMake Makefiles" ..

#. Compile the program.

   ::

      nmake

.. _`Microsoft Visual Studio`: http://www.microsoft.com/visualstudio/en-us
.. _Git: http://git-scm.com/
.. _CMake: http://www.cmake.org/


License
=======

This software is free for use in open source and commercial/closed-source
applications so long as you respect the terms of this 2-clause BSD license:

::

   Copyright (c) 2012, Andre Caron (andre.l.caron@gmail.com)
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
