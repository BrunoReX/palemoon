# Copyright 2011, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# Constants indicating WebSocket protocol version.
VERSION_HYBI07 = 7
VERSION_HYBI00 = 0
VERSION_HIXIE75 = -1

# Port numbers
DEFAULT_WEB_SOCKET_PORT = 80
DEFAULT_WEB_SOCKET_SECURE_PORT = 443

# Schemes
WEB_SOCKET_SCHEME = 'ws'
WEB_SOCKET_SECURE_SCHEME = 'wss'

# Frame opcodes defined in the spec.
OPCODE_CONTINUATION = 0x0
OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xa

# UUIDs used by HyBi 07 opening handshake and frame masking.
WEBSOCKET_ACCEPT_UUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'

# Opening handshake header names and expected values.
UPGRADE_HEADER = 'Upgrade'
WEBSOCKET_UPGRADE_TYPE = 'websocket'
WEBSOCKET_UPGRADE_TYPE_HIXIE75 = 'WebSocket'
CONNECTION_HEADER = 'Connection'
UPGRADE_CONNECTION_TYPE = 'Upgrade'
HOST_HEADER = 'Host'
SEC_WEBSOCKET_ORIGIN_HEADER = 'Sec-WebSocket-Origin'
SEC_WEBSOCKET_KEY_HEADER = 'Sec-WebSocket-Key'
SEC_WEBSOCKET_ACCEPT_HEADER = 'Sec-WebSocket-Accept'
SEC_WEBSOCKET_VERSION_HEADER = 'Sec-WebSocket-Version'
SEC_WEBSOCKET_PROTOCOL_HEADER = 'Sec-WebSocket-Protocol'
SEC_WEBSOCKET_EXTENSIONS_HEADER = 'Sec-WebSocket-Extensions'

# Status codes
STATUS_NORMAL = 1000
STATUS_GOING_AWAY = 1001
STATUS_PROTOCOL_ERROR = 1002
STATUS_UNSUPPORTED = 1003
STATUS_TOO_LARGE = 1004
