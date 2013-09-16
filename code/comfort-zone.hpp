#ifndef _cz_hpp__
#define _cz_hpp__

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
#include "BlockingWriter.hpp"
#include "Computation.hpp"
#include "Engine.hpp"
#include "FileReader.hpp"
#include "FileWriter.hpp"
#include "Hub.hpp"
#include "Request.hpp"
#include "Stream.hpp"
#include "Listener.hpp"
#include "SocketChannel.hpp"


/*!
 * @defgroup core Core infrastructure
 * @brief Heart of the toolkit.
 *
 * This group contains components that form the basic infrastructure.
 */


/*!
 * @defgroup resources I/O resources
 * @brief Resources that perform asynchronous I/O.
 *
 * This group provides basic functionality: simple asynchronous I/O.
 */


/*!
 * @defgroup requests Asynchronous requests
 * @brief Tools for multi-request dispatch.
 *
 * This group provides a more advanced functionality for tasks to block on
 * multiple asynchronous requests simultaneously (a socket and a file, for
 * example).  Using this functionaliy can avoid spawning multiple independant
 * tasks for a single logical algorithm.
 */

#endif /* _cz_hpp__ */
