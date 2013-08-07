#!/usr/bin/env python
# header.py - Generate C++ header files from IDL.
#
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
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
#   Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2011
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Olli Pettay <Olli.Pettay@helsinki.fi>
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

from codegen import *
import sys, os.path, re, xpidl, itertools

# --makedepend-output support.
make_dependencies = []
make_targets = []

def strip_begin(text, suffix):
    if not text.startswith(suffix):
        return text
    return text[len(suffix):]

def strip_end(text, suffix):
    if not text.endswith(suffix):
        return text
    return text[:-len(suffix)]

# Copied from dombindingsgen.py
def writeMakeDependOutput(filename):
    print "Creating makedepend file", filename
    f = open(filename, 'w')
    try:
        if len(make_targets) > 0:
            f.write("%s:" % makeQuote(make_targets[0]))
            for filename in make_dependencies:
                f.write(' \\\n\t\t%s' % makeQuote(filename))
            f.write('\n\n')
            for filename in make_targets[1:]:
                f.write('%s: %s\n' % (makeQuote(filename), makeQuote(make_targets[0])))
    finally:
        f.close()

def findIDL(includePath, interfaceFileName):
    for d in includePath:
        # Not os.path.join: we need a forward slash even on Windows because
        # this filename ends up in makedepend output.
        path = d + '/' + interfaceFileName
        if os.path.exists(path):
            return path
    raise BaseException("No IDL file found for interface %s "
                        "in include path %r"
                        % (interfaceFileName, includePath))

def loadIDL(parser, includePath, filename):
    idlFile = findIDL(includePath, filename)
    if not idlFile in make_dependencies:
        make_dependencies.append(idlFile)
    idl = p.parse(open(idlFile).read(), idlFile)
    idl.resolve(includePath, p)
    return idl

class Configuration:
    def __init__(self, filename):
        config = {}
        execfile(filename, config)
        self.dictionaries = config.get('dictionaries', [])
        self.special_includes = config.get('special_includes', [])
        self.exclude_automatic_type_include = config.get('exclude_automatic_type_include', [])

def readConfigFile(filename):
    return Configuration(filename)

def firstCap(str):
    return str[0].upper() + str[1:]

def attributeGetterName(a):
    binaryname = a.binaryname is not None and a.binaryname or firstCap(a.name)
    return "Get%s" % (binaryname)

def attributeParamlist(prefix, a):
    if a.realtype.nativeType('in').endswith('*'):
        l = ["getter_AddRefs(%s%s)" % (prefix, a.name.strip('* '))]
    elif a.realtype.nativeType('in').count("nsAString"):
        l = ["%s%s" % (prefix, a.name)]
    else:
        l = ["&(%s%s)" % (prefix, a.name)]

    if a.implicit_jscontext:
        l.insert(0, "aCx")

    return ", ".join(l)

def attributeVariableTypeAndName(a):
    if a.realtype.nativeType('in').endswith('*'):
        l = ["nsCOMPtr<%s> %s" % (a.realtype.nativeType('in').strip('* '),
                   a.name)]
    elif a.realtype.nativeType('in').count("nsAString"):
        l = ["nsAutoString %s" % a.name]
    elif a.realtype.nativeType('in').count("JS::Value"):
        l = ["JS::Value %s" % a.name]
    else:
        l = ["%s%s" % (a.realtype.nativeType('in'),
                       a.name)]

    return ", ".join(l)

def dict_name(iface):
    return "%s" % strip_begin(iface, "nsI")

def print_header(idl, fd, conf, dictname, dicts):
    for p in idl.productions:
        if p.kind == 'interface' and p.name == dictname:
            interfaces = []
            base = p.base
            baseiface = p.idl.getName(p.base, p.location)
            while base != "nsISupports" and not base in dicts:
                dicts.append(base)
                interfaces.append(baseiface)
                base = baseiface.base
                baseiface = baseiface.idl.getName(baseiface.base, baseiface.location)

            interfaces.reverse()
            for iface in interfaces:
                write_header(iface, fd)

            if not p.name in dicts:
                dicts.append(p.name)
                write_header(p, fd)

def print_header_file(fd, conf):
    fd.write("/* THIS FILE IS AUTOGENERATED - DO NOT EDIT */\n\n"
             "#ifndef _gen_mozilla_idl_dictionary_helpers_h_\n"
             "#define _gen_mozilla_idl_dictionary_helpers_h_\n\n")

    fd.write("#include \"jsapi.h\"\n"
             "#include \"nsString.h\"\n"
             "#include \"nsCOMPtr.h\"\n\n")

    forwards = []
    attrnames = []
    for d in conf.dictionaries:
        idl = loadIDL(p, options.incdirs, d[1])
        collect_names_and_non_primitive_attribute_types(idl, d[0], attrnames, forwards)
    
    for c in forwards:
        fd.write("class %s;\n" % c)


    fd.write("\n"
             "namespace mozilla {\n"
             "namespace dom {\n\n")

    dicts = []
    for d in conf.dictionaries:
        if not d[0] in set(dicts):
            idl = loadIDL(p, options.incdirs, d[1])
            print_header(idl, fd, conf, d[0], dicts)
    fd.write("}\n"
             "}\n"
             "#endif\n")

def collect_names_and_non_primitive_attribute_types(idl, dictname, attrnames, forwards):
    for p in idl.productions:
        if p.kind == 'interface' and p.name == dictname:
            interfaces = []
            base = p.base
            baseiface = p.idl.getName(p.base, p.location)
            while base != "nsISupports":
                interfaces.append(baseiface)
                base = baseiface.base
                baseiface = baseiface.idl.getName(baseiface.base, baseiface.location)    

            interfaces.reverse()
            interfaces.append(p)

            for iface in interfaces:
                collect_names_and_non_primitive_attribute_types_from_interface(iface, attrnames, forwards)

def collect_names_and_non_primitive_attribute_types_from_interface(iface, attrnames, forwards):
    for member in iface.members:
        if isinstance(member, xpidl.Attribute):
            if not member.name in attrnames:
                attrnames.append(member.name)
            if member.realtype.nativeType('in').endswith('*'):
                t = member.realtype.nativeType('in').strip('* ')
                if not t in forwards:
                    forwards.append(t)

def print_cpp(idl, fd, conf, dictname, dicts):
    for p in idl.productions:
        if p.kind == 'interface' and p.name == dictname:
            interfaces = []
            base = p.base
            baseiface = p.idl.getName(p.base, p.location)
            while base != "nsISupports" and not base in dicts:
                dicts.append(base)
                interfaces.append(baseiface)
                base = baseiface.base
                baseiface = baseiface.idl.getName(baseiface.base, baseiface.location)

            interfaces.reverse()
            for iface in interfaces:
                write_cpp(iface, fd)

            if not p.name in dicts:
                dicts.append(p.name)
                write_cpp(p, fd)

def get_jsid(name):
    return ("gDictionary_id_%s" % name)

def print_cpp_file(fd, conf):
    fd.write("/* THIS FILE IS AUTOGENERATED - DO NOT EDIT */\n\n")
    fd.write('#include "DictionaryHelpers.h"\n')

    includes = []
    for s in conf.special_includes:
        if not s in includes:
            includes.append(strip_end(s, ".h"))
    
    for d in conf.dictionaries:
        if not d[1] in includes:
            includes.append(strip_end(d[1], ".idl"))

    attrnames = []
    for d in conf.dictionaries:
        idl = loadIDL(p, options.incdirs, d[1])
        collect_names_and_non_primitive_attribute_types(idl, d[0], attrnames, includes)
    
    for c in includes:
      if not c in conf.exclude_automatic_type_include:
            fd.write("#include \"%s.h\"\n" % c)

    fd.write("\nusing namespace mozilla::dom;\n\n")

    for a in attrnames:
        fd.write("static jsid %s = JSID_VOID;\n"% get_jsid(a))

    fd.write("\n"
             "static bool\n"
             "DefineStaticJSVal(JSContext* aCx, jsid &id, const char* aString)\n"
             "{\n"
             "  if (JSString* str = JS_InternString(aCx, aString)) {\n"
             "    id = INTERNED_STRING_TO_JSID(aCx, str);\n"
             "    return true;\n"
             "  }\n"
             "  return false;\n"
             "}\n\n"
             "bool\n"
             "DefineStaticDictionaryJSVals(JSContext* aCx)\n"
             "{\n"
             "  JSAutoRequest ar(aCx);\n"
             "  return\n")
    for a in attrnames:
        fd.write("    DefineStaticJSVal(aCx, %s, \"%s\") &&\n"
                 % (get_jsid(a), a))

    fd.write("    true;\n")
    fd.write("}\n\n")

    dicts = []
    for d in conf.dictionaries:
        if not d[0] in set(dicts):
            idl = p.parse(open(findIDL(options.incdirs, d[1])).read(), d[1])
            idl.resolve(options.incdirs, p)
            print_cpp(idl, fd, conf, d[0], dicts)

def init_value(attribute):
    realtype = attribute.realtype.nativeType('in')
    realtype = realtype.strip(' ')
    if realtype.endswith('*'):
        return "nsnull"
    if realtype == "bool":
        return "false"
    if realtype.count("nsAString"):
        return "EmptyString()"
    if realtype.count("nsACString"):
        return "EmptyCString()"
    if realtype.count("JS::Value"):
        return "JSVAL_VOID"
    return "0"

def write_header(iface, fd):
    attributes = []
    for member in iface.members:
        if isinstance(member, xpidl.Attribute):
            attributes.append(member)
    
    fd.write("class %s" % dict_name(iface.name))
    if iface.base != "nsISupports":
        fd.write(" : public %s" % dict_name(iface.base))
    fd.write("\n{\npublic:\n")
    fd.write("  %s()" % dict_name(iface.name))

    if iface.base != "nsISupports" or len(attributes) > 0:
        fd.write(" :\n")
    
    if iface.base != "nsISupports":
        fd.write("    %s()" % dict_name(iface.base))
        if len(attributes) > 0:
            fd.write(",\n")

    for i in range(len(attributes)):
        fd.write("    %s(%s)" % (attributes[i].name, init_value(attributes[i])))
        if i < (len(attributes) - 1):
            fd.write(",")
        fd.write("\n")

    fd.write("  {}\n\n")

    fd.write("  // If aCx or aVal is null, NS_OK is returned and \n"
             "  // dictionary will use the default values. \n"
             "  nsresult Init(JSContext* aCx, const jsval* aVal);\n")
    
    fd.write("\n")

    for member in attributes:
        fd.write("  %s;\n" % attributeVariableTypeAndName(member))

    fd.write("};\n\n")

def write_cpp(iface, fd):
    attributes = []
    for member in iface.members:
        if isinstance(member, xpidl.Attribute):
            attributes.append(member)

    fd.write("static nsresult\n%s_InitInternal(%s& aDict, %s* aIfaceObject, JSContext* aCx, JSObject* aObj)\n" %
             (dict_name(iface.name), dict_name(iface.name), iface.name))
    fd.write("{\n")
    if iface.base != "nsISupports":
        fd.write("  nsresult rv = %s_InitInternal(aDict, aIfaceObject, aCx, aObj);\n" %
                 dict_name(iface.base))
        fd.write("  NS_ENSURE_SUCCESS(rv, rv);\n")

    fd.write("  JSBool found = PR_FALSE;\n")
    for a in attributes:
        fd.write("  NS_ENSURE_STATE(JS_HasPropertyById(aCx, aObj, %s, &found));\n"
                 % get_jsid(a.name))
        fd.write("  if (found) {\n")
        fd.write("    nsresult rv = aIfaceObject->%s(" % attributeGetterName(a))
        fd.write("%s" % attributeParamlist("aDict.", a))
        fd.write(");\n")
        fd.write("    NS_ENSURE_SUCCESS(rv, rv);\n")
        fd.write("  }\n")
    fd.write("  return NS_OK;\n")
    fd.write("}\n\n")
    
    fd.write("nsresult\n%s::Init(JSContext* aCx, const jsval* aVal)\n" % dict_name(iface.name))
    fd.write("{\n"
             "  if (!aCx || !aVal) {\n"
             "    return NS_OK;\n"
             "  }\n"
             "  NS_ENSURE_STATE(aVal->isObject());\n\n"
             "  JSObject* obj = &aVal->toObject();\n"
             "  nsCxPusher pusher;\n"
             "  NS_ENSURE_STATE(pusher.Push(aCx, false));\n"
             "  JSAutoRequest ar(aCx);\n"
             "  JSAutoEnterCompartment ac;\n"
             "  NS_ENSURE_STATE(ac.enter(aCx, obj));\n")
    fd.write("  nsCOMPtr<%s> dict;\n" % iface.name)
    fd.write("  nsContentUtils::XPConnect()->WrapJS(aCx, obj,\n")
    fd.write("                                      NS_GET_IID(%s),\n" % iface.name)
    fd.write("                                      getter_AddRefs(dict));\n")
    fd.write("  NS_ENSURE_TRUE(dict, NS_OK);\n")

    fd.write("  return %s_InitInternal(*this, dict, aCx, obj);\n}\n\n" %
                 dict_name(iface.name))


if __name__ == '__main__':
    from optparse import OptionParser
    o = OptionParser(usage="usage: %prog [options] configfile")
    o.add_option('-I', action='append', dest='incdirs', default=['.'],
                 help="Directory to search for imported files")
    o.add_option('-o', "--stub-output",
                 type='string', dest='stub_output', default=None,
                 help="Quick stub C++ source output file", metavar="FILE")
    o.add_option('--header-output', type='string', default=None,
                 help="Quick stub header output file", metavar="FILE")
    o.add_option('--makedepend-output', type='string', default=None,
                 help="gnumake dependencies output file", metavar="FILE")
    o.add_option('--cachedir', dest='cachedir', default='',
                 help="Directory in which to cache lex/parse tables.")
    (options, filenames) = o.parse_args()
    if len(filenames) != 1:
        o.error("Exactly one config filename is needed.")
    filename = filenames[0]

    if options.cachedir is not None:
        if not os.path.isdir(options.cachedir):
            os.mkdir(options.cachedir)
        sys.path.append(options.cachedir)

    # Instantiate the parser.
    p = xpidl.IDLParser(outputdir=options.cachedir)

    conf = readConfigFile(filename)

    if options.header_output is not None:
        outfd = open(options.header_output, 'w')
        print_header_file(outfd, conf)
        outfd.close()
    if options.stub_output is not None:
        make_targets.append(options.stub_output)
        outfd = open(options.stub_output, 'w')
        print_cpp_file(outfd, conf)
        outfd.close()
        if options.makedepend_output is not None:
            writeMakeDependOutput(options.makedepend_output)

