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


"""WebSocket HyBi 07 opening handshake processor."""


# Note: request.connection.write is used in this module, even though mod_python
# document says that it should be used only in connection handlers.
# Unfortunately, we have no other options. For example, request.write is not
# suitable because it doesn't allow direct raw bytes writing.


import base64
import logging
import os
import re

from mod_pywebsocket import common
from mod_pywebsocket.stream import Stream
from mod_pywebsocket.stream import StreamOptions
from mod_pywebsocket import util
from mod_pywebsocket.handshake._base import HandshakeError
from mod_pywebsocket.handshake._base import check_header_lines
from mod_pywebsocket.handshake._base import get_mandatory_header


_MANDATORY_HEADERS = [
    # key, expected value or None
    [common.UPGRADE_HEADER, common.WEBSOCKET_UPGRADE_TYPE],
    [common.CONNECTION_HEADER, common.UPGRADE_CONNECTION_TYPE],
]

_BASE64_REGEX = re.compile('^[+/0-9A-Za-z]*=*$')


def compute_accept(key):
    """Computes value for the Sec-WebSocket-Accept header from value of the
    Sec-WebSocket-Key header.
    """

    accept_binary = util.sha1_hash(
        key + common.WEBSOCKET_ACCEPT_UUID).digest()
    accept = base64.b64encode(accept_binary)

    return (accept, accept_binary)


class Handshaker(object):
    """This class performs WebSocket handshake."""

    def __init__(self, request, dispatcher):
        """Construct an instance.

        Args:
            request: mod_python request.
            dispatcher: Dispatcher (dispatch.Dispatcher).

        Handshaker will add attributes such as ws_resource during handshake.
        """

        self._logger = util.get_class_logger(self)

        self._request = request
        self._dispatcher = dispatcher

    def do_handshake(self):
        check_header_lines(self._request, _MANDATORY_HEADERS)
        self._request.ws_resource = self._request.uri

        unused_host = get_mandatory_header(self._request, common.HOST_HEADER)

        self._get_origin()
        self._check_version()
        self._set_protocol()
        self._set_extensions()

        key = self._get_key()
        (accept, accept_binary) = compute_accept(key)
        self._logger.debug('Sec-WebSocket-Accept: %r (%s)' %
                           (accept, util.hexify(accept_binary)))

        self._logger.debug('IETF HyBi 07 protocol')
        self._request.ws_version = common.VERSION_HYBI07
        stream_options = StreamOptions()
        stream_options.deflate = self._request.ws_deflate
        self._request.ws_stream = Stream(self._request, stream_options)

        self._request.ws_close_code = None
        self._request.ws_close_reason = None

        self._dispatcher.do_extra_handshake(self._request)

        if self._request.ws_requested_protocols is not None:
            if self._request.ws_protocol is None:
                raise HandshakeError(
                    'do_extra_handshake must choose one subprotocol from '
                    'ws_requested_protocols and set it to ws_protocol')

            # TODO(tyoshino): Validate selected subprotocol value.

            self._logger.debug(
                'Subprotocol accepted: %r',
                self._request.ws_protocol)
        else:
            if self._request.ws_protocol is not None:
                raise HandshakeError(
                    'ws_protocol must be None when the client didn\'t request '
                    'any subprotocol')

        self._send_handshake(accept)

    def _get_origin(self):
        origin = self._request.headers_in.get(
            common.SEC_WEBSOCKET_ORIGIN_HEADER)
        self._request.ws_origin = origin

    def _check_version(self):
        unused_value = get_mandatory_header(
            self._request, common.SEC_WEBSOCKET_VERSION_HEADER, '7')

    def _set_protocol(self):
        self._request.ws_protocol = None

        protocol_header = self._request.headers_in.get(
            common.SEC_WEBSOCKET_PROTOCOL_HEADER)

        if not protocol_header:
            self._request.ws_requested_protocols = None
            return

        # TODO(tyoshino): Validate the header value.

        requested_protocols = protocol_header.split(',')
        self._request.ws_requested_protocols = [
            s.strip() for s in requested_protocols]

        self._logger.debug('Subprotocols requested: %r', requested_protocols)

    def _set_extensions(self):
        self._request.ws_deflate = False

        extensions_header = self._request.headers_in.get(
            common.SEC_WEBSOCKET_EXTENSIONS_HEADER)
        if not extensions_header:
            self._request.ws_extensions = None
            return

        self._request.ws_extensions = []

        requested_extensions = extensions_header.split(',')
        # TODO(tyoshino): Follow the ABNF in the spec.
        requested_extensions = [s.strip() for s in requested_extensions]

        for extension in requested_extensions:
            # We now support only deflate-stream extension. Any other
            # extension requests are just ignored for now.
            if extension == 'deflate-stream':
                self._request.ws_extensions.append(extension)
                self._request.ws_deflate = True

        self._logger.debug('Extensions requested: %r', requested_extensions)
        self._logger.debug(
            'Extensions accepted: %r', self._request.ws_extensions)

    def _validate_key(self, key):
        # Validate
        key_is_valid = False
        try:
            # Validate key by quick regex match before parsing by base64
            # module. Because base64 module skips invalid characters, we have
            # to do this in advance to make this server strictly reject illegal
            # keys.
            if _BASE64_REGEX.match(key):
                decoded_key = base64.b64decode(key)
                if len(decoded_key) == 16:
                    key_is_valid = True
        except TypeError, e:
            pass

        if not key_is_valid:
            raise HandshakeError(
                'Illegal value for header %s: %r' %
                (common.SEC_WEBSOCKET_KEY_HEADER, key))

        return decoded_key

    def _get_key(self):
        key = get_mandatory_header(
            self._request, common.SEC_WEBSOCKET_KEY_HEADER)

        decoded_key = self._validate_key(key)

        self._logger.debug('Sec-WebSocket-Key: %r (%s)' %
                           (key, util.hexify(decoded_key)))

        return key

    def _sendall(self, data):
        self._request.connection.write(data)

    def _send_header(self, name, value):
        self._sendall('%s: %s\r\n' % (name, value))

    def _send_handshake(self, accept):
        self._sendall('HTTP/1.1 101 Switching Protocols\r\n')
        self._send_header(common.UPGRADE_HEADER, common.WEBSOCKET_UPGRADE_TYPE)
        self._send_header(
            common.CONNECTION_HEADER, common.UPGRADE_CONNECTION_TYPE)
        self._send_header(common.SEC_WEBSOCKET_ACCEPT_HEADER, accept)
        # TODO(tyoshino): Encode value of protocol and extensions if any
        # special character that we have to encode by some manner.
        if self._request.ws_protocol is not None:
            self._send_header(
                common.SEC_WEBSOCKET_PROTOCOL_HEADER,
                self._request.ws_protocol)
        if self._request.ws_extensions is not None:
            self._send_header(
                common.SEC_WEBSOCKET_EXTENSIONS_HEADER,
                ', '.join(self._request.ws_extensions))
        self._sendall('\r\n')


# vi:sts=4 sw=4 et
