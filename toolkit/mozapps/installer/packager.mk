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
# The Original Code is Mozilla Communicator client code, released
# March 31, 1998.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Benjamin Smedberg <bsmedberg@covad.net>
#   Arthur Wiebe <artooro@gmail.com>
#   Mark Mentovai <mark@moxienet.com>
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

include $(MOZILLA_DIR)/toolkit/mozapps/installer/package-name.mk

# This is how we create the binary packages we release to the public.

ifndef MOZ_PKG_FORMAT
ifneq (,$(filter mac cocoa,$(MOZ_WIDGET_TOOLKIT)))
MOZ_PKG_FORMAT  = DMG
else
ifeq (,$(filter-out OS2 WINNT WINCE BeOS, $(OS_ARCH)))
MOZ_PKG_FORMAT  = ZIP
else
ifeq (,$(filter-out SunOS, $(OS_ARCH)))
   MOZ_PKG_FORMAT  = BZ2
else
   ifeq ($(MOZ_WIDGET_TOOLKIT),gtk2)
      MOZ_PKG_FORMAT  = BZ2
   else
      MOZ_PKG_FORMAT  = TGZ
   endif
endif
endif
endif
endif # MOZ_PKG_FORMAT

ifeq ($(OS_ARCH),OS2)
INSTALLER_DIR   = os2
else
ifneq (,$(filter WINNT WINCE,$(OS_ARCH)))
INSTALLER_DIR   = windows
else
ifneq (cocoa,$(MOZ_WIDGET_TOOLKIT))
INSTALLER_DIR   = unix
endif
endif
endif

PACKAGE       = $(PKG_PATH)$(PKG_BASENAME)$(PKG_SUFFIX)

# By default, the SDK uses the same packaging type as the main bundle,
# but on mac it is a .tar.bz2
SDK_PATH      = $(PKG_PATH)
ifeq ($(MOZ_APP_NAME),xulrunner)
SDK_PATH = sdk/
endif
SDK_SUFFIX    = $(PKG_SUFFIX)
SDK           = $(SDK_PATH)$(PKG_BASENAME).sdk$(SDK_SUFFIX)

MAKE_PACKAGE	= $(error What is a $(MOZ_PKG_FORMAT) package format?);
MAKE_CAB	= $(error Don't know how to make a CAB!);

ifdef WINCE
ifndef WINCE_WINDOWS_MOBILE
CABARGS += -s
endif
ifdef MOZ_FASTSTART
CABARGS += -faststart
endif
VSINSTALLDIR ?= $(error VSINSTALLDIR not set, must be set to the Visual Studio install directory)
MAKE_CAB	= $(PYTHON) $(MOZILLA_DIR)/build/package/wince/make_wince_cab.py \
	$(CABARGS) "$(VSINSTALLDIR)/SmartDevices/SDK/SDKTools/cabwiz.exe" \
	"$(MOZ_PKG_DIR)" "$(MOZ_APP_DISPLAYNAME)" "$(PKG_PATH)$(PKG_BASENAME).cab"
endif

CREATE_FINAL_TAR = $(TAR) -c --owner=0 --group=0 --numeric-owner \
  --mode="go-w" -f
UNPACK_TAR       = tar -xf-

ifeq ($(MOZ_PKG_FORMAT),TAR)
PKG_SUFFIX	= .tar
MAKE_PACKAGE 	= $(CREATE_FINAL_TAR) - $(MOZ_PKG_DIR) > $(PACKAGE)
UNMAKE_PACKAGE	= $(UNPACK_TAR) < $(UNPACKAGE)
MAKE_SDK = $(CREATE_FINAL_TAR) - $(MOZ_APP_NAME)-sdk > $(SDK)
endif
ifeq ($(MOZ_PKG_FORMAT),TGZ)
PKG_SUFFIX	= .tar.gz
MAKE_PACKAGE 	= $(CREATE_FINAL_TAR) - $(MOZ_PKG_DIR) | gzip -vf9 > $(PACKAGE)
UNMAKE_PACKAGE	= gunzip -c $(UNPACKAGE) | $(UNPACK_TAR)
MAKE_SDK = $(CREATE_FINAL_TAR) - $(MOZ_APP_NAME)-sdk | gzip -vf9 > $(SDK)
endif
ifeq ($(MOZ_PKG_FORMAT),BZ2)
PKG_SUFFIX	= .tar.bz2
MAKE_PACKAGE 	= $(CREATE_FINAL_TAR) - $(MOZ_PKG_DIR) | bzip2 -vf > $(PACKAGE)
UNMAKE_PACKAGE	= bunzip2 -c $(UNPACKAGE) | $(UNPACK_TAR)
MAKE_SDK = $(CREATE_FINAL_TAR) - $(MOZ_APP_NAME)-sdk | bzip2 -vf > $(SDK)
endif
ifeq ($(MOZ_PKG_FORMAT),ZIP)
PKG_SUFFIX	= .zip
MAKE_PACKAGE	= $(ZIP) -r9D $(PACKAGE) $(MOZ_PKG_DIR)
UNMAKE_PACKAGE	= $(UNZIP) $(UNPACKAGE)
MAKE_SDK = $(ZIP) -r9D $(SDK) $(MOZ_APP_NAME)-sdk
endif
ifeq ($(MOZ_PKG_FORMAT),CAB)
PKG_SUFFIX	= .cab
MAKE_PACKAGE	= $(MAKE_CAB)
UNMAKE_PACKAGE	= $(error Unpacking CAB files is not supported)
endif
ifeq ($(MOZ_PKG_FORMAT),DMG)
ifndef _APPNAME
ifdef MOZ_DEBUG
_APPNAME	= $(MOZ_APP_DISPLAYNAME)Debug.app
else
_APPNAME	= $(MOZ_APP_DISPLAYNAME).app
endif
endif
ifndef _BINPATH
_BINPATH	= /$(_APPNAME)/Contents/MacOS
endif # _BINPATH
PKG_SUFFIX	= .dmg
PKG_DMG_FLAGS	=
ifneq (,$(MOZ_PKG_MAC_DSSTORE))
PKG_DMG_FLAGS += --copy "$(MOZ_PKG_MAC_DSSTORE):/.DS_Store"
endif
ifneq (,$(MOZ_PKG_MAC_BACKGROUND))
PKG_DMG_FLAGS += --mkdir /.background --copy "$(MOZ_PKG_MAC_BACKGROUND):/.background"
endif
ifneq (,$(MOZ_PKG_MAC_ICON))
PKG_DMG_FLAGS += --icon "$(MOZ_PKG_MAC_ICON)"
endif
ifneq (,$(MOZ_PKG_MAC_RSRC))
PKG_DMG_FLAGS += --resource "$(MOZ_PKG_MAC_RSRC)"
endif
ifneq (,$(MOZ_PKG_MAC_EXTRA))
PKG_DMG_FLAGS += $(MOZ_PKG_MAC_EXTRA)
endif
_ABS_MOZSRCDIR = $(shell cd $(MOZILLA_DIR) && pwd)
ifdef UNIVERSAL_BINARY
STAGEPATH = universal/
endif
ifndef PKG_DMG_SOURCE
PKG_DMG_SOURCE = $(STAGEPATH)$(MOZ_PKG_DIR)
endif
MAKE_PACKAGE	= $(_ABS_MOZSRCDIR)/build/package/mac_osx/pkg-dmg \
  --source "$(PKG_DMG_SOURCE)" --target "$(PACKAGE)" \
  --volname "$(MOZ_APP_DISPLAYNAME)" $(PKG_DMG_FLAGS)
_ABS_DIST = $(call core_abspath,$(DIST))
UNMAKE_PACKAGE	= \
  set -ex; \
  rm -rf $(_ABS_DIST)/unpack.tmp; \
  mkdir -p $(_ABS_DIST)/unpack.tmp; \
  $(_ABS_MOZSRCDIR)/build/package/mac_osx/unpack-diskimage $(UNPACKAGE) /tmp/$(MOZ_PKG_APPNAME)-unpack $(_ABS_DIST)/unpack.tmp; \
  rsync -a "$(_ABS_DIST)/unpack.tmp/$(_APPNAME)" $(MOZ_PKG_DIR); \
  test -n "$(MOZ_PKG_MAC_DSSTORE)" && \
    rsync -a "$(_ABS_DIST)/unpack.tmp/.DS_Store" "$(MOZ_PKG_MAC_DSSTORE)"; \
  test -n "$(MOZ_PKG_MAC_BACKGROUND)" && \
    rsync -a "$(_ABS_DIST)/unpack.tmp/.background/`basename "$(MOZ_PKG_MAC_BACKGROUND)"`" "$(MOZ_PKG_MAC_BACKGROUND)"; \
  test -n "$(MOZ_PKG_MAC_ICON)" && \
    rsync -a "$(_ABS_DIST)/unpack.tmp/.VolumeIcon.icns" "$(MOZ_PKG_MAC_ICON)"; \
  rm -rf $(_ABS_DIST)/unpack.tmp; \
  if test -n "$(MOZ_PKG_MAC_RSRC)" ; then \
    cp $(UNPACKAGE) $(MOZ_PKG_APPNAME).tmp.dmg && \
    hdiutil unflatten $(MOZ_PKG_APPNAME).tmp.dmg && \
    { /Developer/Tools/DeRez -skip plst -skip blkx $(MOZ_PKG_APPNAME).tmp.dmg > "$(MOZ_PKG_MAC_RSRC)" || { rm -f $(MOZ_PKG_APPNAME).tmp.dmg && false; }; } && \
    rm -f $(MOZ_PKG_APPNAME).tmp.dmg; \
  fi; \
  $(NULL)
# The plst and blkx resources are skipped because they belong to each
# individual dmg and are created by hdiutil.
SDK_SUFFIX = .tar.bz2
SDK = $(MOZ_PKG_APPNAME)-$(MOZ_PKG_VERSION).$(AB_CD).mac-$(TARGET_CPU).sdk$(SDK_SUFFIX)
ifeq ($(MOZ_APP_NAME),xulrunner)
SDK = $(SDK_PATH)$(MOZ_APP_NAME)-$(MOZ_PKG_VERSION).$(AB_CD).mac-$(TARGET_CPU).sdk$(SDK_SUFFIX)
endif
MAKE_SDK = $(CREATE_FINAL_TAR) - $(MOZ_APP_NAME)-sdk | bzip2 -vf > $(SDK)
endif

# dummy macro if we don't have PSM built
SIGN_NSS		=
ifneq (1_,$(if $(CROSS_COMPILE),1,0)_$(UNIVERSAL_BINARY))
ifdef MOZ_PSM
SIGN_NSS		= @echo signing nss libraries;

NSS_DLL_SUFFIX	= $(DLL_SUFFIX)
ifdef UNIVERSAL_BINARY
NATIVE_ARCH	= $(shell uname -p | sed -e s/powerpc/ppc/)
NATIVE_DIST	= $(DIST:$(DEPTH)/%=$(DEPTH)/../$(NATIVE_ARCH)/%)
SIGN_CMD	= $(NATIVE_DIST)/bin/run-mozilla.sh $(NATIVE_DIST)/bin/shlibsign -v -i
else
ifeq ($(OS_ARCH),OS2)
# uppercase extension to get the correct output file from shlibsign
NSS_DLL_SUFFIX	= .DLL
SIGN_CMD	= $(MOZILLA_DIR)/toolkit/mozapps/installer/os2/sign.cmd $(DIST)
else
SIGN_CMD	= $(RUN_TEST_PROGRAM) $(DIST)/bin/shlibsign -v -i
endif
endif

SOFTOKN		= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)softokn3$(NSS_DLL_SUFFIX)
NSSDBM		= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)nssdbm3$(NSS_DLL_SUFFIX)
FREEBL		= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl3$(NSS_DLL_SUFFIX)
FREEBL_32FPU	= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl_32fpu_3$(DLL_SUFFIX)
FREEBL_32INT	= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl_32int_3$(DLL_SUFFIX)
FREEBL_32INT64	= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl_32int64_3$(DLL_SUFFIX)
FREEBL_64FPU	= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl_64fpu_3$(DLL_SUFFIX)
FREEBL_64INT	= $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)/$(DLL_PREFIX)freebl_64int_3$(DLL_SUFFIX)

SIGN_NSS	+= $(SIGN_CMD) $(SOFTOKN); \
	$(SIGN_CMD) $(NSSDBM); \
	if test -f $(FREEBL); then $(SIGN_CMD) $(FREEBL); fi; \
	if test -f $(FREEBL_32FPU); then $(SIGN_CMD) $(FREEBL_32FPU); fi; \
	if test -f $(FREEBL_32INT); then $(SIGN_CMD) $(FREEBL_32INT); fi; \
	if test -f $(FREEBL_32INT64); then $(SIGN_CMD) $(FREEBL_32INT64); fi; \
	if test -f $(FREEBL_64FPU); then $(SIGN_CMD) $(FREEBL_64FPU); fi; \
	if test -f $(FREEBL_64INT); then $(SIGN_CMD) $(FREEBL_64INT); fi;

endif # MOZ_PSM
endif # !CROSS_COMPILE

NO_PKG_FILES += \
	core \
	bsdecho \
	gtscc \
	js \
	js-config \
	jscpucfg \
	nsinstall \
	viewer \
	TestGtkEmbed \
	bloaturls.txt \
	codesighs* \
	elf-dynstr-gc \
	mangle* \
	maptsv* \
	mfc* \
	mkdepend* \
	msdump* \
	msmap* \
	nm2tsv* \
	nsinstall* \
	rebasedlls* \
	res/samples \
	res/throbber \
	shlibsign* \
	ssltunnel* \
	certutil* \
	pk12util* \
	winEmbed.exe \
	chrome/chrome.rdf \
	chrome/app-chrome.manifest \
	chrome/overlayinfo \
	components/compreg.dat \
	components/xpti.dat \
	content_unit_tests \
	necko_unit_tests \
	*.dSYM \
	$(NULL)

# browser/locales/Makefile uses this makefile for its variable defs, but
# doesn't want the libs:: rule.
ifndef PACKAGER_NO_LIBS
libs:: make-package
endif

DEFINES += -DDLL_PREFIX=$(DLL_PREFIX) -DDLL_SUFFIX=$(DLL_SUFFIX) -DBIN_SUFFIX=$(BIN_SUFFIX)

ifdef MOZ_PKG_REMOVALS
MOZ_PKG_REMOVALS_GEN = removed-files

$(MOZ_PKG_REMOVALS_GEN): $(MOZ_PKG_REMOVALS) $(GLOBAL_DEPS)
	$(PYTHON) $(MOZILLA_DIR)/config/Preprocessor.py -Fsubstitution $(DEFINES) $(ACDEFINES) $(MOZ_PKG_REMOVALS) > $(MOZ_PKG_REMOVALS_GEN)

GARBAGE += $(MOZ_PKG_REMOVALS_GEN)
endif

GARBAGE		+= $(DIST)/$(PACKAGE) $(PACKAGE)

ifeq ($(OS_ARCH),IRIX)
STRIP_FLAGS	= -f
endif
ifeq ($(OS_ARCH),BeOS)
STRIP_FLAGS	= -g
PLATFORM_EXCLUDE_LIST = ! -name "*.stub" ! -name "$(MOZ_PKG_APPNAME)-bin"
endif
ifeq ($(OS_ARCH),OS2)
STRIP		= $(MOZILLA_DIR)/toolkit/mozapps/installer/os2/strip.cmd
STRIP_FLAGS	=
PLATFORM_EXCLUDE_LIST = ! -name "*.ico" ! -name "$(MOZ_PKG_APPNAME).exe"
endif

ifneq (,$(filter WINNT WINCE OS2,$(OS_ARCH)))
PKGCP_OS = dos
else
PKGCP_OS = unix
endif

# The following target stages files into three directories: one directory for
# locale-independent files, one for locale-specific files, and one for optional
# extensions based on the information in the MOZ_PKG_MANIFEST file and the
# following vars:
# MOZ_NONLOCALIZED_PKG_LIST
# MOZ_LOCALIZED_PKG_LIST
# MOZ_OPTIONAL_PKG_LIST

PKG_ARG = , "$(pkg)"

# Define packager macro to work around make 3.81 backslash issue (bug #339933)
define PACKAGER_COPY
$(PERL) -I$(MOZILLA_DIR)/xpinstall/packager -e 'use Packager; \
       Packager::Copy($1,$2,$3,$4,$5,$6,$7);'
endef

installer-stage: $(MOZ_PKG_MANIFEST)
ifndef MOZ_PKG_MANIFEST
	$(error MOZ_PKG_MANIFEST unspecified!)
endif
	@rm -rf $(DEPTH)/installer-stage $(DIST)/xpt
	@echo "Staging installer files..."
	@$(NSINSTALL) -D $(DEPTH)/installer-stage/nonlocalized
	@$(NSINSTALL) -D $(DEPTH)/installer-stage/localized
	@$(NSINSTALL) -D $(DEPTH)/installer-stage/optional
	@$(NSINSTALL) -D $(DIST)/xpt
	$(call PACKAGER_COPY, "$(DIST)",\
		"$(DEPTH)/installer-stage/nonlocalized", \
		"$(MOZ_PKG_MANIFEST)", "$(PKGCP_OS)", 1, 0, 1 \
		$(foreach pkg,$(MOZ_NONLOCALIZED_PKG_LIST),$(PKG_ARG)) )
	$(call PACKAGER_COPY, "$(DIST)",\
		"$(DEPTH)/installer-stage/localized", \
		"$(MOZ_PKG_MANIFEST)", "$(PKGCP_OS)", 1, 0, 1 \
		$(foreach pkg,$(MOZ_LOCALIZED_PKG_LIST),$(PKG_ARG)) )
ifdef MOZ_OPTIONAL_PKG_LIST
	$(call PACKAGER_COPY, "$(DIST)",\
		"$(DEPTH)/installer-stage/optional", \
		"$(MOZ_PKG_MANIFEST)", "$(PKGCP_OS)", 1, 0, 1 \
		$(foreach pkg,$(MOZ_OPTIONAL_PKG_LIST),$(PKG_ARG)) )
endif
	$(PERL) $(MOZILLA_DIR)/xpinstall/packager/xptlink.pl -s $(DIST) -d $(DIST)/xpt -f $(DEPTH)/installer-stage/nonlocalized/components -v -x "$(XPIDL_LINK)"

stage-package: $(MOZ_PKG_MANIFEST) $(MOZ_PKG_REMOVALS_GEN)
	@rm -rf $(DIST)/$(MOZ_PKG_DIR) $(DIST)/$(PKG_PATH)$(PKG_BASENAME).tar $(DIST)/$(PKG_PATH)$(PKG_BASENAME).dmg $@ $(EXCLUDE_LIST)
# NOTE: this must be a tar now that dist links into the tree so that we
# do not strip the binaries actually in the tree.
	@echo "Creating package directory..."
	@mkdir $(DIST)/$(MOZ_PKG_DIR)
ifdef MOZ_PKG_MANIFEST
	$(RM) -rf $(DIST)/xpt
	$(call PACKAGER_COPY, "$(call core_abspath,$(DIST))",\
		 "$(call core_abspath,$(DIST)/$(MOZ_PKG_DIR))", \
		"$(MOZ_PKG_MANIFEST)", "$(PKGCP_OS)", 1, 0, 1)
	$(PERL) $(MOZILLA_DIR)/xpinstall/packager/xptlink.pl -s $(DIST) -d $(DIST)/xpt -f $(DIST)/$(MOZ_PKG_DIR)/$(_BINPATH)/components -v -x "$(XPIDL_LINK)"
else # !MOZ_PKG_MANIFEST
ifeq ($(MOZ_PKG_FORMAT),DMG)
# If UNIVERSAL_BINARY, the package will be made from an already-prepared
# STAGEPATH
ifndef UNIVERSAL_BINARY
ifndef STAGE_SDK
	@cd $(DIST) && rsync -auv --copy-unsafe-links $(_APPNAME) $(MOZ_PKG_DIR)
else
	@cd $(DIST)/bin && tar $(TAR_CREATE_FLAGS) - * | (cd ../$(MOZ_PKG_DIR); tar -xf -)
endif
endif
else
	@cd $(DIST)/bin && tar $(TAR_CREATE_FLAGS) - * | (cd ../$(MOZ_PKG_DIR); tar -xf -)
	@echo "Linking XPT files..."
	@rm -rf $(DIST)/xpt
	@$(NSINSTALL) -D $(DIST)/xpt
	@($(XPIDL_LINK) $(DIST)/xpt/$(MOZ_PKG_APPNAME).xpt $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)/components/*.xpt && rm -f $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)/components/*.xpt && cp $(DIST)/xpt/$(MOZ_PKG_APPNAME).xpt $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)/components) || echo No *.xpt files found in: $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)/components/.  Continuing...
endif # DMG
endif # MOZ_PKG_MANIFEST
ifndef PKG_SKIP_STRIP
	@echo "Stripping package directory..."
	@cd $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR); find . ! -type d \
			! -name "*.js" \
			! -name "*.xpt" \
			! -name "*.gif" \
			! -name "*.jpg" \
			! -name "*.png" \
			! -name "*.xpm" \
			! -name "*.txt" \
			! -name "*.rdf" \
			! -name "*.sh" \
			! -name "*.properties" \
			! -name "*.dtd" \
			! -name "*.html" \
			! -name "*.xul" \
			! -name "*.css" \
			! -name "*.xml" \
			! -name "*.jar" \
			! -name "*.dat" \
			! -name "*.tbl" \
			! -name "*.src" \
			! -name "*.reg" \
			$(PLATFORM_EXCLUDE_LIST) \
			-exec $(STRIP) $(STRIP_FLAGS) {} >/dev/null 2>&1 \;
	$(SIGN_NSS)
else
ifdef UNIVERSAL_BINARY
# universal binaries will have had their .chk files removed prior to the unify
# step, and if they're also --disable-install-strip then they won't get
# re-signed in the block above.
	$(SIGN_NSS)
endif # UNIVERSAL_BINARY
endif # PKG_SKIP_STRIP
	@echo "Removing unpackaged files..."
ifdef NO_PKG_FILES
	cd $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH); rm -rf $(NO_PKG_FILES)
endif
ifdef MOZ_PKG_REMOVALS
	$(SYSINSTALL) $(IFLAGS1) $(MOZ_PKG_REMOVALS_GEN) $(DIST)/$(STAGEPATH)$(MOZ_PKG_DIR)$(_BINPATH)
endif # MOZ_PKG_REMOVALS

make-package: stage-package $(PACKAGE_XULRUNNER) make-sourcestamp-file
	@echo "Compressing..."
	cd $(DIST) && $(MAKE_PACKAGE)

make-sourcestamp-file::
	$(NSINSTALL) -D $(DIST)/$(PKG_PATH)
	@echo "$(BUILDID)" > $(MOZ_SOURCESTAMP_FILE)
	@echo "$(MOZ_SOURCE_REPO)/rev/$(MOZ_SOURCE_STAMP)" >> $(MOZ_SOURCESTAMP_FILE)

# The install target will install the application to prefix/lib/appname-version
# In addition if INSTALL_SDK is set, it will install the development headers,
# libraries, and IDL files as follows:
# dist/include -> prefix/include/appname-version
# dist/idl -> prefix/share/idl/appname-version
# dist/sdk/lib -> prefix/lib/appname-devel-version/lib
# prefix/lib/appname-devel-version/* symlinks to the above directories
install:: stage-package
ifneq (,$(filter WINNT WINCE,$(OS_ARCH)))
	$(error "make install" is not supported on this platform. Use "make package" instead.)
endif
ifeq (bundle,$(MOZ_FS_LAYOUT))
	$(error "make install" is not supported on this platform. Use "make package" instead.)
endif
	$(NSINSTALL) -D $(DESTDIR)$(installdir)
	(cd $(DIST)/$(MOZ_PKG_DIR) && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DESTDIR)$(installdir) && tar -xf -)
	$(NSINSTALL) -D $(DESTDIR)$(bindir)
	$(RM) -f $(DESTDIR)$(bindir)/$(MOZ_APP_NAME)
	ln -s $(installdir)/$(MOZ_APP_NAME) $(DESTDIR)$(bindir)
ifdef INSTALL_SDK # Here comes the hard part
	$(NSINSTALL) -D $(DESTDIR)$(includedir)
	(cd $(DIST)/include && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DESTDIR)$(includedir) && tar -xf -)
	$(NSINSTALL) -D $(DESTDIR)$(idldir)
	(cd $(DIST)/idl && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DESTDIR)$(idldir) && tar -xf -)
# SDK directory is the libs + a bunch of symlinks
	$(NSINSTALL) -D $(DESTDIR)$(sdkdir)/sdk/lib
	if test -f $(DIST)/include/xpcom-config.h; then \
	  $(SYSINSTALL) $(IFLAGS1) $(DIST)/include/xpcom-config.h $(DESTDIR)$(sdkdir); \
	fi
	(cd $(DIST)/sdk/lib && tar $(TAR_CREATE_FLAGS) - .) | (cd $(DESTDIR)$(sdkdir)/sdk/lib && tar -xf -)
	$(RM) -f $(DESTDIR)$(sdkdir)/lib $(DESTDIR)$(sdkdir)/bin $(DESTDIR)$(sdkdir)/include $(DESTDIR)$(sdkdir)/include $(DESTDIR)$(sdkdir)/sdk/idl $(DESTDIR)$(sdkdir)/idl
	ln -s $(sdkdir)/sdk/lib $(DESTDIR)$(sdkdir)/lib
	ln -s $(installdir) $(DESTDIR)$(sdkdir)/bin
	ln -s $(includedir) $(DESTDIR)$(sdkdir)/include
	ln -s $(idldir) $(DESTDIR)$(sdkdir)/idl
endif # INSTALL_SDK

make-sdk:
	$(MAKE) stage-package UNIVERSAL_BINARY= STAGE_SDK=1 MOZ_PKG_DIR=sdk-stage
	@echo "Packaging SDK..."
	$(RM) -rf $(DIST)/$(MOZ_APP_NAME)-sdk
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/bin
	(cd $(DIST)/sdk-stage && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/bin && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/host/bin
	(cd $(DIST)/host/bin && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/host/bin && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/sdk
	(cd $(DIST)/sdk && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/sdk && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/include
	(cd $(DIST)/include && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/include && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/idl
	(cd $(DIST)/idl && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/idl && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(MOZ_APP_NAME)-sdk/lib
# sdk/lib is the same as sdk/sdk/lib
	(cd $(DIST)/sdk/lib && tar $(TAR_CREATE_FLAGS) - .) | \
	  (cd $(DIST)/$(MOZ_APP_NAME)-sdk/lib && tar -xf -)
	$(NSINSTALL) -D $(DIST)/$(SDK_PATH)
	cd $(DIST) && $(MAKE_SDK)

ifeq ($(OS_TARGET), WINNT)
INSTALLER_PACKAGE = $(DIST)/$(PKG_INST_PATH)$(PKG_INST_BASENAME).exe
endif
ifeq ($(OS_TARGET), WINCE)
INSTALLER_PACKAGE = $(DIST)/$(PKG_PATH)$(PKG_BASENAME).cab
endif

# These are necessary because some of our packages/installers contain spaces
# in their filenames and GNU Make's $(wildcard) function doesn't properly
# deal with them.
empty :=
space = $(empty) $(empty)
QUOTED_WILDCARD = $(if $(wildcard $(subst $(space),?,$(1))),"$(1)")

upload:
	$(PYTHON) $(MOZILLA_DIR)/build/upload.py --base-path $(DIST) \
		$(call QUOTED_WILDCARD,$(DIST)/$(PACKAGE)) \
		$(call QUOTED_WILDCARD,$(INSTALLER_PACKAGE)) \
		$(call QUOTED_WILDCARD,$(DIST)/$(COMPLETE_MAR)) \
		$(call QUOTED_WILDCARD,$(DIST)/$(LANGPACK)) \
		$(call QUOTED_WILDCARD,$(wildcard $(DIST)/$(PARTIAL_MAR))) \
		$(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(TEST_PACKAGE)) \
		$(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(SYMBOL_ARCHIVE_BASENAME).zip) \
		$(call QUOTED_WILDCARD,$(DIST)/$(SDK)) \
		$(call QUOTED_WILDCARD,$(MOZ_SOURCESTAMP_FILE)) \
		$(if $(UPLOAD_EXTRA_FILES), $(foreach f, $(UPLOAD_EXTRA_FILES), $(wildcard $(DIST)/$(f))))

ifndef MOZ_PKG_SRCDIR
MOZ_PKG_SRCDIR = $(topsrcdir)
endif

DIR_TO_BE_PACKAGED ?= ../$(notdir $(topsrcdir))
SRC_TAR_EXCLUDE_PATHS += \
  --exclude=".hg*" \
  --exclude="CVS" \
  --exclude=".cvs*" \
  --exclude=".mozconfig*" \
  --exclude="*.pyc" \
  --exclude="$(MOZILLA_DIR)/Makefile" \
  --exclude="$(MOZILLA_DIR)/dist"
ifdef MOZ_OBJDIR
SRC_TAR_EXCLUDE_PATHS += --exclude="$(MOZ_OBJDIR)"
endif
CREATE_SOURCE_TAR = $(TAR) -c --owner=0 --group=0 --numeric-owner \
  --mode="go-w" $(SRC_TAR_EXCLUDE_PATHS) -f

# source-package creates a source tarball from the files in MOZ_PKG_SRCDIR,
# which is either set to a clean checkout or defaults to $topsrcdir
source-package:
	@echo "Packaging source tarball..."
	mkdir -p $(DIST)/$(PKG_SRCPACK_PATH)
	(cd $(MOZ_PKG_SRCDIR) && $(CREATE_SOURCE_TAR) - $(DIR_TO_BE_PACKAGED)) | bzip2 -vf > $(DIST)/$(PKG_SRCPACK_PATH)$(PKG_SRCPACK_BASENAME).tar.bz2
