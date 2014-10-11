# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Combined with build/autoconf/config.status.m4, ConfigStatus is an almost
# drop-in replacement for autoconf 2.13's config.status, with features
# borrowed from autoconf > 2.5, and additional features.

from __future__ import print_function

import logging
import os
import sys

from optparse import OptionParser

from mach.logging import LoggingManager
from mozbuild.backend.configenvironment import ConfigEnvironment
from mozbuild.backend.recursivemake import RecursiveMakeBackend
from mozbuild.frontend.emitter import TreeMetadataEmitter
from mozbuild.frontend.reader import BuildReader

from Preprocessor import Preprocessor


log_manager = LoggingManager()


def config_status(topobjdir = '.', topsrcdir = '.',
                  defines = [], non_global_defines = [], substs = [],
                  files = [], headers = []):
    '''Main function, providing config.status functionality.

    Contrary to config.status, it doesn't use CONFIG_FILES or CONFIG_HEADERS
    variables, but like config.status from autoconf 2.6, single files may be
    generated with the --file and --header options. Several such options can
    be given to generate several files at the same time.

    Without the -n option, this program acts as config.status and considers
    the current directory as the top object directory, even when config.status
    is in a different directory. It will, however, treat the directory
    containing config.status as the top object directory with the -n option,
    while files given to the --file and --header arguments are considered
    relative to the current directory.

    The --recheck option, like with the original config.status, runs configure
    again, with the options given in the "ac_configure_args" subst.

    The options to this function are passed when creating the
    ConfigEnvironment, except for files and headers, which contain the list
    of files and headers to be generated by default. These lists, as well as
    the actual wrapper script around this function, are meant to be generated
    by configure. See build/autoconf/config.status.m4.

    Unlike config.status behaviour with CONFIG_FILES and CONFIG_HEADERS,
    but like config.status behaviour with --file and --header, providing
    files or headers on the command line inhibits the default generation of
    files when given headers and headers when given files.

    Unlike config.status, the FILE:TEMPLATE syntax is not supported for
    files and headers. The template is always the filename suffixed with
    '.in', in the corresponding directory under the top source directory.
    '''

    if 'CONFIG_FILES' in os.environ:
        raise Exception, 'Using the CONFIG_FILES environment variable is not supported. Use --file instead.'
    if 'CONFIG_HEADERS' in os.environ:
        raise Exception, 'Using the CONFIG_HEADERS environment variable is not supported. Use --header instead.'

    parser = OptionParser()
    parser.add_option('--recheck', dest='recheck', action='store_true',
                      help='update config.status by reconfiguring in the same conditions')
    parser.add_option('--file', dest='files', metavar='FILE', action='append',
                      help='instantiate the configuration file FILE')
    parser.add_option('--header', dest='headers', metavar='FILE', action='append',
                      help='instantiate the configuration header FILE')
    parser.add_option('-v', '--verbose', dest='verbose', action='store_true',
                      help='display verbose output')
    parser.add_option('-n', dest='not_topobjdir', action='store_true',
                      help='do not consider current directory as top object directory')
    (options, args) = parser.parse_args()

    # Without -n, the current directory is meant to be the top object directory
    if not options.not_topobjdir:
        topobjdir = os.path.abspath('.')

    env = ConfigEnvironment(topsrcdir, topobjdir, defines=defines,
            non_global_defines=non_global_defines, substs=substs)

    reader = BuildReader(env)
    emitter = TreeMetadataEmitter(env)
    backend = RecursiveMakeBackend(env)
    # This won't actually do anything because of the magic of generators.
    definitions = emitter.emit(reader.read_topsrcdir())

    if options.recheck:
        # Execute configure from the top object directory
        if not os.path.isabs(topsrcdir):
            topsrcdir = relpath(topsrcdir, topobjdir)
        os.chdir(topobjdir)
        os.execlp('sh', 'sh', '-c', ' '.join([os.path.join(topsrcdir, 'configure'), env.substs['ac_configure_args'], '--no-create', '--no-recursion']))

    if options.files:
        files = options.files
        headers = []
    if options.headers:
        headers = options.headers
        if not options.files:
            files = []
    # Default to display messages when giving --file or --headers on the
    # command line.
    log_level = logging.INFO

    if options.files or options.headers or options.verbose:
        log_level = logging.DEBUG

    log_manager.add_terminal_logging(level=log_level)
    log_manager.enable_unstructured()

    if not options.files and not options.headers:
        print('Checking for beer in the fridge...', file=sys.stderr)
        summary = backend.consume(definitions)

        for line in summary.summaries():
            print(line, file=sys.stderr)

        files = [os.path.join(topobjdir, f) for f in files]
        headers = [os.path.join(topobjdir, f) for f in headers]

    for file in files:
        env.create_config_file(file)
    for header in headers:
        env.create_config_header(header)
