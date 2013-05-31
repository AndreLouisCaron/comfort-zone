# Copyright (c) 2012, Andre Caron (andre.l.caron@gmail.com)
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set(fio_headers
  BlockingReader.hpp
  BlockingWriter.hpp
  Computation.hpp
  Engine.hpp
  FileReader.hpp
  FileWriter.hpp
  Hub.hpp
  Listener.hpp
  Request.hpp
  SocketChannel.hpp
  Stream.hpp
)
set(fio_sources
  BlockingReader.cpp
  BlockingWriter.cpp
  Computation.cpp
  Engine.cpp
  FileReader.cpp
  FileWriter.cpp
  Hub.cpp
  Listener.cpp
  Request.cpp
  SocketChannel.cpp
  Stream.cpp
)
set(fio_libraries
  w32
  w32.dbg
  w32.io
  w32.mt
  w32.tp
)

add_library(fio
  STATIC
  ${fio_headers}
  ${fio_sources}
)
target_link_libraries(fio ${fio_libraries})
