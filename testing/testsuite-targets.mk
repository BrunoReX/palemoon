# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Mozilla Test Harnesses
#
# The Initial Developer of the Original Code is
# The Mozilla Foundation
# Portions created by the Initial Developer are Copyright (C) 2008
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#	Serge Gautherie <sgautherie.bz@free.fr>
#	Ted Mielczarek <ted.mielczarek@gmail.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****


# Shortcut for mochitest* and xpcshell-tests targets,
# replaces 'EXTRA_TEST_ARGS=--test-path=...'.
ifdef TEST_PATH
TEST_PATH_ARG := --test-path=$(TEST_PATH)
else
TEST_PATH_ARG :=
endif

# include automation-build.mk to get the path to the binary
TARGET_DEPTH = $(DEPTH)
include $(topsrcdir)/build/binary-location.mk

SYMBOLS_PATH := --symbols-path=$(DIST)/crashreporter-symbols

# Usage: |make [TEST_PATH=...] [EXTRA_TEST_ARGS=...] mochitest*|.
MOCHITESTS := mochitest-plain mochitest-chrome mochitest-a11y mochitest-ipcplugins
mochitest:: $(MOCHITESTS)

RUN_MOCHITEST = \
	rm -f ./$@.log && \
	$(PYTHON) _tests/testing/mochitest/runtests.py --autorun --close-when-done \
	  --console-level=INFO --log-file=./$@.log --file-level=INFO \
	  $(SYMBOLS_PATH) $(TEST_PATH_ARG) $(EXTRA_TEST_ARGS)

RUN_MOCHITEST_REMOTE = \
	rm -f ./$@.log && \
	$(PYTHON) _tests/testing/mochitest/runtestsremote.py --autorun --close-when-done \
	  --console-level=INFO --log-file=./$@.log --file-level=INFO $(DM_FLAGS) --dm_trans=$(DM_TRANS) \
	  --app=$(ANDROID_PACKAGE_NAME) --deviceIP=${TEST_DEVICE} --xre-path=${MOZ_HOST_BIN} \
	  $(SYMBOLS_PATH) $(TEST_PATH_ARG) $(EXTRA_TEST_ARGS)

ifndef NO_FAIL_ON_TEST_ERRORS
define CHECK_TEST_ERROR
  @errors=`grep "TEST-UNEXPECTED-" $@.log` ;\
  if test "$$errors" ; then \
	  echo "$@ failed:"; \
	  echo "$$errors"; \
	  exit 1; \
  else \
	  echo "$@ passed"; \
  fi
endef
endif

mochitest-remote: DM_TRANS?=adb
mochitest-remote:
	@if test -f ${MOZ_HOST_BIN}/xpcshell && [ "${TEST_DEVICE}" != "usb" -o "$(DM_TRANS)" = "adb" ]; \
          then $(RUN_MOCHITEST_REMOTE); \
        else \
          echo "please prepare your host with environment variables for TEST_DEVICE and MOZ_HOST_BIN"; \
        fi

mochitest-plain:
	$(RUN_MOCHITEST)
	$(CHECK_TEST_ERROR)

# Allow mochitest-1 ... mochitest-5 for developer ease
mochitest-1 mochitest-2 mochitest-3 mochitest-4 mochitest-5: mochitest-%:
	echo "mochitest: $* / 5"
	$(RUN_MOCHITEST) --chunk-by-dir=4 --total-chunks=5 --this-chunk=$*
	$(CHECK_TEST_ERROR)

mochitest-chrome:
	$(RUN_MOCHITEST) --chrome
	$(CHECK_TEST_ERROR)

mochitest-a11y:
	$(RUN_MOCHITEST) --a11y
	$(CHECK_TEST_ERROR)

mochitest-ipcplugins:
ifeq (Darwin,$(OS_ARCH))
ifeq (i386,$(TARGET_CPU))
	$(RUN_MOCHITEST) --setpref=dom.ipc.plugins.enabled.i386.test.plugin=false --test-path=dom/plugins/test
endif
ifeq (x86_64,$(TARGET_CPU))
	$(RUN_MOCHITEST) --setpref=dom.ipc.plugins.enabled.x86_64.test.plugin=false --test-path=dom/plugins/test
endif
ifeq (powerpc,$(TARGET_CPU))
	$(RUN_MOCHITEST) --setpref=dom.ipc.plugins.enabled.ppc.test.plugin=false --test-path=dom/plugins/test
endif
else
	$(RUN_MOCHITEST) --setpref=dom.ipc.plugins.enabled=false --test-path=dom/plugins/test
endif
	$(CHECK_TEST_ERROR)

# Usage: |make [EXTRA_TEST_ARGS=...] *test|.
RUN_REFTEST = rm -f ./$@.log && $(PYTHON) _tests/reftest/runreftest.py \
  $(SYMBOLS_PATH) $(EXTRA_TEST_ARGS) $(1) | tee ./$@.log

REMOTE_REFTEST = rm -f ./$@.log && $(PYTHON) _tests/reftest/remotereftest.py \
  --dm_trans=$(DM_TRANS) --ignore-window-size \
  --app=$(ANDROID_PACKAGE_NAME) --deviceIP=${TEST_DEVICE} --xre-path=${MOZ_HOST_BIN} \
  $(SYMBOLS_PATH) $(EXTRA_TEST_ARGS) $(1) | tee ./$@.log

ifeq ($(OS_ARCH),WINNT) #{
# GPU-rendered shadow layers are unsupported here
OOP_CONTENT = --setpref=browser.tabs.remote=true --setpref=layers.acceleration.disabled=true
GPU_RENDERING =
else
OOP_CONTENT = --setpref=browser.tabs.remote=true
GPU_RENDERING = --setpref=layers.acceleration.force-enabled=true
endif #}

reftest: TEST_PATH?=layout/reftests/reftest.list
reftest:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH))
	$(CHECK_TEST_ERROR)

reftest-remote: TEST_PATH?=layout/reftests/reftest.list
reftest-remote: DM_TRANS?=adb
reftest-remote:
	@if test -f ${MOZ_HOST_BIN}/xpcshell && [ "${TEST_DEVICE}" != "" -o "$(DM_TRANS)" = "adb" ]; \
	  then ln -s $(abspath $(topsrcdir)) _tests/reftest/tests;$(call REMOTE_REFTEST,tests/$(TEST_PATH)); $(CHECK_TEST_ERROR); \
        else \
          echo "please prepare your host with environment variables for TEST_DEVICE and MOZ_HOST_BIN"; \
        fi

reftest-ipc: TEST_PATH?=layout/reftests/reftest.list
reftest-ipc:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH) $(OOP_CONTENT))
	$(CHECK_TEST_ERROR)

reftest-ipc-gpu: TEST_PATH?=layout/reftests/reftest.list
reftest-ipc-gpu:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH) $(OOP_CONTENT) $(GPU_RENDERING))
	$(CHECK_TEST_ERROR)

crashtest: TEST_PATH?=testing/crashtest/crashtests.list
crashtest:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH))
	$(CHECK_TEST_ERROR)

crashtest-ipc: TEST_PATH?=testing/crashtest/crashtests.list
crashtest-ipc:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH) $(OOP_CONTENT))
	$(CHECK_TEST_ERROR)

crashtest-ipc-gpu: TEST_PATH?=testing/crashtest/crashtests.list
crashtest-ipc-gpu:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH) $(OOP_CONTENT) $(GPU_RENDERING))
	$(CHECK_TEST_ERROR)

jstestbrowser: TEST_PATH?=js/src/tests/jstests.list
jstestbrowser:
	$(call RUN_REFTEST,$(topsrcdir)/$(TEST_PATH) --extra-profile-file=$(topsrcdir)/js/src/tests/user.js)
	$(CHECK_TEST_ERROR)

GARBAGE += $(addsuffix .log,$(MOCHITESTS) reftest crashtest jstestbrowser)

# Execute all xpcshell tests in the directories listed in the manifest.
# See also config/rules.mk 'xpcshell-tests' target for local execution.
# Usage: |make [TEST_PATH=...] [EXTRA_TEST_ARGS=...] xpcshell-tests|.
xpcshell-tests:
	$(PYTHON) -u $(topsrcdir)/config/pythonpath.py \
	  -I$(topsrcdir)/build \
	  $(topsrcdir)/testing/xpcshell/runxpcshelltests.py \
	  --manifest=$(DEPTH)/_tests/xpcshell/xpcshell.ini \
	  --build-info-json=$(DEPTH)/mozinfo.json \
	  --no-logfiles \
          $(SYMBOLS_PATH) \
	  $(TEST_PATH_ARG) $(EXTRA_TEST_ARGS) \
	  $(LIBXUL_DIST)/bin/xpcshell

REMOTE_XPCSHELL = \
	rm -f ./$@.log && \
	$(PYTHON) -u $(topsrcdir)/config/pythonpath.py \
	  -I$(topsrcdir)/build \
	  -I$(topsrcdir)/build/mobile \
	  $(topsrcdir)/testing/xpcshell/remotexpcshelltests.py \
	  --manifest=$(DEPTH)/_tests/xpcshell/xpcshell.ini \
	  --build-info-json=$(DEPTH)/mozinfo.json \
	  --no-logfiles \
	  --dm_trans=$(DM_TRANS) \
	  --deviceIP=${TEST_DEVICE} \
	  --objdir=$(DEPTH) \
	  $(SYMBOLS_PATH) \
	  $(TEST_PATH_ARG) $(EXTRA_TEST_ARGS)

xpcshell-tests-remote: DM_TRANS?=adb
xpcshell-tests-remote:
	@if [ "${TEST_DEVICE}" != "" -o "$(DM_TRANS)" = "adb" ]; \
          then $(call REMOTE_XPCSHELL); $(CHECK_TEST_ERROR); \
        else \
          echo "please prepare your host with environment variables for TEST_DEVICE"; \
        fi

# install and run the mozmill tests
$(DEPTH)/_tests/mozmill:
	$(MAKE) -C $(DEPTH)/testing/mozmill install-develop PKG_STAGE=../../_tests
	$(PYTHON) $(topsrcdir)/testing/mozmill/installmozmill.py --develop $(DEPTH)/_tests/mozmill

MOZMILL_TEST_PATH = $(DEPTH)/_tests/mozmill/tests/firefox
mozmill: TEST_PATH?=$(MOZMILL_TEST_PATH)
mozmill: $(DEPTH)/_tests/mozmill
	$(SHELL) $(DEPTH)/_tests/mozmill/mozmill.sh -t $(TEST_PATH) -b $(browser_path) --show-all

MOZMILL_RESTART_TEST_PATH = $(DEPTH)/_tests/mozmill/tests/firefox/restartTests
mozmill-restart: TEST_PATH?=$(MOZMILL_RESTART_TEST_PATH)
mozmill-restart: $(DEPTH)/_tests/mozmill
	$(SHELL) $(DEPTH)/_tests/mozmill/mozmill-restart.sh -t $(TEST_PATH) -b $(browser_path) --show-all

# in order to have `mozmill-all` ignore TEST_PATH, if it is set, we shell out to call make
# again, verbosely overriding the TEST_PATH
# This isn't as neat as having mozmill and mozmill-restart be dependencies, but it 
# seems to be the make idiom
mozmill-all: 
	$(MAKE) mozmill TEST_PATH=$(MOZMILL_TEST_PATH)
	$(MAKE) mozmill-restart TEST_PATH=$(MOZMILL_RESTART_TEST_PATH)

# Package up the tests and test harnesses
include $(topsrcdir)/toolkit/mozapps/installer/package-name.mk

ifndef UNIVERSAL_BINARY
PKG_STAGE = $(DIST)/test-package-stage
package-tests: stage-mochitest stage-reftest stage-xpcshell stage-jstests stage-mozmill stage-jetpack stage-firebug
else
# This staging area has been built for us by universal/flight.mk
PKG_STAGE = $(DIST)/universal/test-package-stage
endif

package-tests:
	@rm -f "$(DIST)/$(PKG_PATH)$(TEST_PACKAGE)"
ifndef UNIVERSAL_BINARY
	$(NSINSTALL) -D $(DIST)/$(PKG_PATH)
else
	#building tests.jar (bug 543800) fails on unify, so we build tests.jar after unify is run
	$(MAKE) -C $(DEPTH)/testing/mochitest stage-chromejar PKG_STAGE=$(DIST)/universal
endif
	cd $(PKG_STAGE) && \
	  zip -r9D "$(call core_abspath,$(DIST)/$(PKG_PATH)$(TEST_PACKAGE))" *

ifeq (Android, $(OS_TARGET))
package-tests: stage-android
endif

make-stage-dir:
	rm -rf $(PKG_STAGE) && $(NSINSTALL) -D $(PKG_STAGE) && $(NSINSTALL) -D $(PKG_STAGE)/bin && $(NSINSTALL) -D $(PKG_STAGE)/bin/components && $(NSINSTALL) -D $(PKG_STAGE)/certs && $(NSINSTALL) -D $(PKG_STAGE)/jetpack && $(NSINSTALL) -D $(PKG_STAGE)/firebug

stage-mochitest: make-stage-dir
	$(MAKE) -C $(DEPTH)/testing/mochitest stage-package

stage-reftest: make-stage-dir
	$(MAKE) -C $(DEPTH)/layout/tools/reftest stage-package

stage-xpcshell: make-stage-dir
	$(MAKE) -C $(DEPTH)/testing/xpcshell stage-package

stage-jstests: make-stage-dir
	$(MAKE) -C $(DEPTH)/js/src/tests stage-package

stage-mozmill: make-stage-dir
	$(MAKE) -C $(DEPTH)/testing/mozmill stage-package

stage-android: make-stage-dir
	$(NSINSTALL) $(DEPTH)/build/mobile/sutagent/android/sutAgentAndroid.apk $(PKG_STAGE)/bin
	$(NSINSTALL) $(DEPTH)/build/mobile/sutagent/android/watcher/Watcher.apk $(PKG_STAGE)/bin
	$(NSINSTALL) $(DEPTH)/build/mobile/sutagent/android/fencp/FenCP.apk $(PKG_STAGE)/bin
	$(NSINSTALL) $(DEPTH)/build/mobile/sutagent/android/ffxcp/FfxCP.apk $(PKG_STAGE)/bin

stage-jetpack: make-stage-dir
	$(NSINSTALL) $(topsrcdir)/testing/jetpack/jetpack-location.txt $(PKG_STAGE)/jetpack

stage-firebug: make-stage-dir
	$(MAKE) -C $(DEPTH)/testing/firebug stage-package
.PHONY: \
  mochitest mochitest-plain mochitest-chrome mochitest-a11y mochitest-ipcplugins \
  reftest crashtest \
  xpcshell-tests \
  jstestbrowser \
  package-tests make-stage-dir stage-mochitest stage-reftest stage-xpcshell stage-jstests stage-mozmill stage-android stage-jetpack stage-firebug
