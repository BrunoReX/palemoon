#!/usr/bin/python

import urllib
import os
import os.path
import shutil
import tarfile
import subprocess

def download_uri(uri):
    fname = uri.split('/')[-1]
    if (os.path.exists(fname)):
        return fname
    urllib.urlretrieve(uri, fname)
    return fname

def extract(tar, path):
    t = tarfile.open(tar)
    t.extractall(path)

def check_run(args):
    r = subprocess.call(args)
    assert r == 0

def run_in(path, args):
    d = os.getcwd()
    os.chdir(path)
    check_run(args)
    os.chdir(d)

def patch(patch, plevel, srcdir):
    patch = os.path.realpath(patch)
    check_run(['patch', '-d', srcdir, '-p%s' % plevel, '-i', patch, '--fuzz=0',
               '-s'])

def build_package(package_source_dir, package_build_dir, configure_args):
    os.mkdir(package_build_dir)
    run_in(package_build_dir,
           ["%s/configure" % package_source_dir] + configure_args)
    run_in(package_build_dir, ["make", "-j8"])
    run_in(package_build_dir, ["make", "install"])

def build_tar(base_dir, tar_inst_dir):
    tar_build_dir = base_dir + '/tar_build'
    build_package(tar_source_dir, tar_build_dir,
                  ["--prefix=%s" % tar_inst_dir])

def with_env(env, f):
    old_env = os.environ.copy()
    os.environ.update(env)
    f()
    os.environ.clear()
    os.environ.update(old_env)

def build_glibc(env, stage_dir, inst_dir):
    def f():
        build_glibc_aux(stage_dir, inst_dir)
    with_env(env, f)

def build_glibc_aux(stage_dir, inst_dir):
    glibc_build_dir = stage_dir + '/glibc'
    build_package(glibc_source_dir, glibc_build_dir,
                  ["--disable-profile",
                   "--enable-add-ons=nptl",
                   "--without-selinux",
                   "--enable-kernel=2.6.18",
                   "--libdir=%s/lib64" % inst_dir,
                   "--prefix=%s" % inst_dir])

def build_one_stage(env, stage_dir, is_stage_one):
    def f():
        build_one_stage_aux(stage_dir, is_stage_one)
    with_env(env, f)

def build_one_stage_aux(stage_dir, is_stage_one):
    os.mkdir(stage_dir)

    lib_inst_dir = stage_dir + '/libinst'

    gmp_build_dir = stage_dir + '/gmp'
    build_package(gmp_source_dir, gmp_build_dir,
                  ["--prefix=%s" % lib_inst_dir, "--disable-shared"])
    mpfr_build_dir = stage_dir + '/mpfr'
    build_package(mpfr_source_dir, mpfr_build_dir,
                  ["--prefix=%s" % lib_inst_dir, "--disable-shared",
                   "--with-gmp=%s" % lib_inst_dir])
    mpc_build_dir = stage_dir + '/mpc'
    build_package(mpc_source_dir, mpc_build_dir,
                  ["--prefix=%s" % lib_inst_dir, "--disable-shared",
                   "--with-gmp=%s" % lib_inst_dir,
                   "--with-mpfr=%s" % lib_inst_dir])

    tool_inst_dir = stage_dir + '/inst'

    binutils_build_dir = stage_dir + '/binutils'
    build_package(binutils_source_dir, binutils_build_dir,
                  ["--prefix=%s" % tool_inst_dir])

    gcc_build_dir = stage_dir + '/gcc'
    gcc_configure_args = ["--prefix=%s" % tool_inst_dir,
                          "--enable-__cxa_atexit",
                          "--with-gmp=%s" % lib_inst_dir,
                          "--with-mpfr=%s" % lib_inst_dir,
                          "--with-mpc=%s" % lib_inst_dir,
                          "--disable-bootstrap"]
    if is_stage_one:
        gcc_configure_args.append("--enable-languages=c")
        gcc_configure_args.append("--disable-multilib")
        # We build the stage1 gcc without shared libraries. Otherwise its
        # libgcc.so would depend on the system libc.so, which causes problems
        # when it tries to use that libgcc.so and the libc we are about to
        # build.
        gcc_configure_args.append("--disable-shared")
    else:
        gcc_configure_args.append("--enable-languages=c,c++")

    build_package(gcc_source_dir, gcc_build_dir, gcc_configure_args)

    if is_stage_one:
        # The glibc build system uses -lgcc_eh, but at least in this setup
        # libgcc.a has all it needs.
        d = tool_inst_dir + "/lib/gcc/x86_64-unknown-linux-gnu/4.5.2/"
        os.symlink(d + "libgcc.a", d + "libgcc_eh.a")

    build_glibc({"CC"  : tool_inst_dir + "/bin/gcc",
                 "CXX" : tool_inst_dir + "/bin/g++"},
                stage_dir, tool_inst_dir)

def build_tar_package(tar, name, base, directory):
    name = os.path.realpath(name)
    run_in(base, [tar, "-cf", name, "--mtime=2012-01-01", "--owner=root",
                  directory])

##############################################

# The directories end up in the debug info, so the easy way of getting
# a reproducible build is to run it in a know absolute directory.
# We use a directory in /builds/slave because the mozilla infrastructure
# cleans it up automatically.
base_dir = "/builds/slave/moz-toolschain"

source_dir = base_dir + "/src"
build_dir  = base_dir + "/build"

def build_source_dir(prefix, version):
    return source_dir + '/' + prefix + version

binutils_version = "2.21.1"
glibc_version = "2.13" #FIXME: should probably use 2.5.1
tar_version = "1.26"
gcc_version = "4.5.2"
mpfr_version = "2.4.2"
gmp_version = "5.0.1"
mpc_version = "0.8.1"

binutils_source_uri = "http://ftp.gnu.org/gnu/binutils/binutils-%sa.tar.bz2" % \
    binutils_version
glibc_source_uri = "http://ftp.gnu.org/gnu/glibc/glibc-%s.tar.bz2" % \
    glibc_version
tar_source_uri = "http://ftp.gnu.org/gnu/tar/tar-%s.tar.bz2" % \
    tar_version
gcc_source_uri = "http://ftp.gnu.org/gnu/gcc/gcc-%s/gcc-%s.tar.bz2" % \
    (gcc_version, gcc_version)
mpfr_source_uri = "http://www.mpfr.org/mpfr-%s/mpfr-%s.tar.bz2" % \
    (mpfr_version, mpfr_version)
gmp_source_uri = "http://ftp.gnu.org/gnu/gmp/gmp-%s.tar.bz2" % gmp_version
mpc_source_uri = "http://www.multiprecision.org/mpc/download/mpc-%s.tar.gz" % \
    mpc_version

binutils_source_tar = download_uri(binutils_source_uri)
glibc_source_tar = download_uri(glibc_source_uri)
tar_source_tar = download_uri(tar_source_uri)
mpc_source_tar = download_uri(mpc_source_uri)
mpfr_source_tar = download_uri(mpfr_source_uri)
gmp_source_tar = download_uri(gmp_source_uri)
gcc_source_tar = download_uri(gcc_source_uri)

binutils_source_dir  = build_source_dir('binutils-', binutils_version)
glibc_source_dir  = build_source_dir('glibc-', glibc_version)
tar_source_dir  = build_source_dir('tar-', tar_version)
mpc_source_dir  = build_source_dir('mpc-', mpc_version)
mpfr_source_dir = build_source_dir('mpfr-', mpfr_version)
gmp_source_dir  = build_source_dir('gmp-', gmp_version)
gcc_source_dir  = build_source_dir('gcc-', gcc_version)

if not os.path.exists(source_dir):
    os.makedirs(source_dir)
    extract(binutils_source_tar, source_dir)
    patch('binutils-deterministic.patch', 1, binutils_source_dir)
    extract(glibc_source_tar, source_dir)
    patch('glibc-deterministic.patch', 1, glibc_source_dir)
    extract(tar_source_tar, source_dir)
    extract(mpc_source_tar, source_dir)
    extract(mpfr_source_tar, source_dir)
    extract(gmp_source_tar, source_dir)
    extract(gcc_source_tar, source_dir)
    patch('plugin_finish_decl.diff', 0, gcc_source_dir)
    patch('pr49911.diff', 1, gcc_source_dir)
    patch('r159628-r163231-r171807.patch', 1, gcc_source_dir)

if os.path.exists(build_dir):
    shutil.rmtree(build_dir)
os.makedirs(build_dir)

tar_inst_dir = build_dir + '/tar_inst'
build_tar(build_dir, tar_inst_dir)

stage1_dir = build_dir + '/stage1'
build_one_stage({"CC": "gcc", "CXX" : "g++"}, stage1_dir, True)

stage1_tool_inst_dir = stage1_dir + '/inst'
stage2_dir = build_dir + '/stage2'
build_one_stage({"CC"     : stage1_tool_inst_dir + "/bin/gcc",
                 "CXX"    : stage1_tool_inst_dir + "/bin/g++",
                 "AR"     : stage1_tool_inst_dir + "/bin/ar",
                 "RANLIB" : "true" },
                stage2_dir, False)

build_tar_package(tar_inst_dir + "/bin/tar",
                  "toolchain.tar", stage2_dir, "inst")
