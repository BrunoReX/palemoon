# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import print_function, unicode_literals

import collections
import json
import os
import sys

import writeMakefile

def extractLines(fp):
    lines = []
    watch = False
    for line in fp:
        if line == '@@@ @@@ Failures\n':
            watch = True
        elif watch:
            watch = False
            idx = line.index('@@@')
            lines.append((line[:idx], line[idx + 3:]))
    return lines

def ensuredir(path):
    dir = path[:path.rfind('/')]
    if not os.path.exists(dir):
        os.makedirs(dir)

def dumpFailures(lines):
    files = []
    for url, objstr in lines:
        if objstr == '{}\n':
            continue

        # Avoid overly large diffs.
        if 'editing/' in url:
            sep = ':'
        else:
            sep = ': '

        jsonpath = 'failures/' + url + '.json'
        files.append(jsonpath)
        ensuredir(jsonpath)
        obj = json.loads(objstr, object_pairs_hook=collections.OrderedDict)
        formattedobj = json.dumps(obj, indent=2, separators=(',', sep))
        fp = open(jsonpath, 'w')
        fp.write(formattedobj + '\n')
        fp.close()
    return files

def writeMakefiles(files):
    pathmap = {}
    for path in files:
        dirp, leaf = path.rsplit('/', 1)
        pathmap.setdefault(dirp, []).append(leaf)

    for k, v in pathmap.items():
        result = writeMakefile.substMakefile('parseFailures.py', [], v)

        fp = open(k + '/Makefile.in', 'w')
        fp.write(result)
        fp.close()

def main(logPath):
    fp = open(logPath, 'r')
    lines = extractLines(fp)
    fp.close()

    files = dumpFailures(lines)
    writeMakefiles(files)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Please pass the path to the logfile from which failures should be extracted.")
    main(sys.argv[1])
