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
# The Original Code is the Mozilla build system.
#
# The Initial Developer of the Original Code is
# the Mozilla Foundation <http://www.mozilla.org/>.
# Portions created by the Initial Developer are Copyright (C) 2006
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Benjamin Smedberg <benjamin@smedbergs.us> (Initial Code)
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

ifdef LIBXUL_SDK
$(error toolkit-tiers.mk is not compatible with --enable-libxul-sdk=)
endif

include $(topsrcdir)/config/nspr/build.mk
include $(topsrcdir)/config/js/build.mk
include $(topsrcdir)/xpcom/build.mk
include $(topsrcdir)/netwerk/build.mk

TIERS += \
	external \
	gecko \
	toolkit \
	$(NULL)

#
# tier "external" - 3rd party individual libraries
#

ifndef MOZ_NATIVE_JPEG
tier_external_dirs	+= jpeg
endif

# Installer needs standalone libjar, hence standalone zlib
ifdef MOZ_INSTALLER
tier_external_dirs	+= modules/zlib/standalone
endif

ifdef MOZ_UPDATER
ifndef MOZ_NATIVE_BZ2
tier_external_dirs += modules/libbz2
endif
tier_external_dirs += modules/libmar
tier_external_dirs += other-licenses/bsdiff
endif

tier_external_dirs	+= gfx/qcms

ifeq ($(OS_ARCH),WINCE)
tier_external_dirs += modules/lib7z
endif

#
# tier "gecko" - core components
#

ifdef MOZ_IPC
tier_gecko_dirs += ipc
endif

tier_gecko_dirs += \
		js/src/xpconnect \
		js/ctypes \
		intl/chardet \
		$(NULL)

ifdef BUILD_CTYPES
ifndef _MSC_VER
tier_gecko_staticdirs += \
		js/ctypes/libffi \
		$(NULL)
endif
endif

ifdef MOZ_ENABLE_GTK2
ifdef MOZ_X11
tier_gecko_dirs     += widget/src/gtkxtbin
endif
endif

tier_gecko_dirs	+= \
		modules/libutil \
		modules/libjar \
		db \
		$(NULL)

ifdef MOZ_PERMISSIONS
tier_gecko_dirs += \
		extensions/cookie \
		extensions/permissions \
		$(NULL)
endif

ifdef MOZ_STORAGE
tier_gecko_dirs += storage
endif

ifdef MOZ_RDF
tier_gecko_dirs += rdf
endif

ifdef MOZ_JSDEBUGGER
tier_gecko_dirs += js/jsd
endif

ifdef MOZ_OGG
tier_gecko_dirs += \
		media/libfishsound \
		media/libogg \
		media/liboggplay \
		media/liboggz \
		media/libtheora \
		media/libvorbis \
		$(NULL)
endif

ifdef MOZ_SYDNEYAUDIO
tier_gecko_dirs += \
		media/libsydneyaudio \
		$(NULL)
endif

tier_gecko_dirs	+= \
		uriloader \
		modules/libimg \
		caps \
		parser \
		gfx \
		modules/libpr0n \
		modules/plugin \
		dom \
		view \
		widget \
		content \
		editor \
		layout \
		docshell \
		webshell \
		embedding \
		xpfe/appshell \
		$(NULL)

# Java Embedding Plugin
ifneq (,$(filter mac cocoa,$(MOZ_WIDGET_TOOLKIT)))
tier_gecko_dirs += plugin/oji/JEP
endif

ifdef MOZ_XMLEXTRAS
tier_gecko_dirs += extensions/xmlextras
endif

ifdef MOZ_WEBSERVICES
tier_gecko_dirs += extensions/webservices
endif

ifdef MOZ_UNIVERSALCHARDET
tier_gecko_dirs += extensions/universalchardet
endif

ifdef MOZ_OJI
tier_gecko_dirs	+= \
		js/src/liveconnect \
		sun-java \
		modules/oji \
		$(NULL)
endif

ifdef ACCESSIBILITY
tier_gecko_dirs    += accessible
endif

# 
# tier "toolkit" - xpfe & toolkit
#
# The division of "gecko" and "toolkit" is somewhat arbitrary, and related
# to history where "gecko" wasn't forked between seamonkey/firefox but
# "toolkit" was.
#

tier_toolkit_dirs += chrome profile

# This must preceed xpfe
ifdef MOZ_JPROF
tier_toolkit_dirs        += tools/jprof
endif

tier_toolkit_dirs	+= \
	xpfe \
	toolkit/components \
	$(NULL)

ifdef MOZ_ENABLE_XREMOTE
tier_toolkit_dirs += widget/src/xremoteclient
endif

ifdef MOZ_SPELLCHECK
tier_toolkit_dirs	+= extensions/spellcheck
endif

tier_toolkit_dirs	+= toolkit

ifdef MOZ_XPINSTALL
tier_toolkit_dirs     +=  xpinstall
endif

ifdef MOZ_PSM
tier_toolkit_dirs	+= security/manager
else
tier_toolkit_dirs	+= security/manager/boot/public security/manager/ssl/public
endif

ifdef MOZ_PREF_EXTENSIONS
tier_toolkit_dirs += extensions/pref
endif

# JavaXPCOM JNI code is compiled into libXUL
ifdef MOZ_JAVAXPCOM
tier_toolkit_dirs += extensions/java/xpcom/src
endif

ifndef BUILD_STATIC_LIBS
ifneq (,$(MOZ_ENABLE_GTK2))
tier_toolkit_dirs += embedding/browser/gtk
endif
endif

ifndef BUILD_STATIC_LIBS
tier_toolkit_dirs += toolkit/library
endif

ifdef MOZ_ENABLE_LIBXUL
tier_toolkit_dirs += xpcom/stub
endif

ifdef NS_TRACE_MALLOC
tier_toolkit_dirs += tools/trace-malloc
endif

ifdef MOZ_ENABLE_GNOME_COMPONENT
tier_toolkit_dirs    += toolkit/system/gnome
endif

ifndef MOZ_ENABLE_LIBCONIC
# if libconic is present, it will do its own network monitoring
ifdef MOZ_ENABLE_DBUS
tier_toolkit_dirs    += toolkit/system/dbus
endif
endif

ifdef MOZ_LEAKY
tier_toolkit_dirs        += tools/leaky
endif

ifdef MOZ_MAPINFO
tier_toolkit_dirs	+= tools/codesighs
endif

ifdef ENABLE_TESTS
tier_toolkit_dirs	+= testing/mochitest
endif

ifdef MOZ_TREE_FREETYPE
tier_external_dirs	+= modules/freetype2
endif
