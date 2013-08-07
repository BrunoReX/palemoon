#!/usr/bin/perl
# -*- Mode: Perl; tab-width: 2; indent-tabs-mode: nil; -*-
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
# The Original Code is Mozilla MathML Project.
#
# The Initial Developer of the Original Code is
# Frederic Wang <fred.wang@free.fr>.
# Portions created by the Initial Developer are Copyright (C) 2010
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
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

use XML::XSLT;
use XML::DOM;

# output files
$FILE_DIFFERENCES = "differences.txt";
$FILE_NEW_DICTIONARY = "new_dictionary.txt";
$FILE_SYNTAX_ERRORS = "syntax_errors.txt";
$FILE_JS = "tests/stretchy-and-large-operators.js";

# our dictionary (property file)
$MOZ_DICTIONARY = "mathfont.properties";

# dictionary provided in "XML Entity Definitions for Characters"
# The file unicode.xml is very large (> 5Mb), so it is expected that you
# provide instead the XML file transformed by operatorDictionary.xsl.
# > xsltproc -o dictionary.xml operatorDictionary.xsl unicode.xml
$WG_DICTIONARY = "dictionary.xml";

if (!($#ARGV >= 0 &&
      ((($ARGV[0] eq "compare") && $#ARGV <= 1) ||
       (($ARGV[0] eq "check") && $#ARGV <= 0) ||
       (($ARGV[0] eq "make-js") && $#ARGV <= 0)))) {
    &usage;
}

if ($ARGV[0] eq "compare" && $#ARGV == 1) {
    $WG_DICTIONARY = $ARGV[1];
}

################################################################################
# structure of the dictionary used by this script:
# - key: same as in mathfont.properties
# - table:
#    index | value
#      0   | description
#      1   | lspace
#      2   | rspace
#      3   | minsize
#      4   | largeop
#      5   | movablelimits
#      6   | stretchy
#      7   | separator
#      8   | accent
#      9   | fence
#     10   | symmetric
#     11   | priority
#     12   | linebreakstyle
#     13   | direction
#     14   | integral
#     15   | mirrorable

# 1) build %moz_hash from $MOZ_DICTIONARY

print "loading $MOZ_DICTIONARY...\n";
open($file, $MOZ_DICTIONARY) || die ("Couldn't open $MOZ_DICTIONARY!");

print "building dictionary...\n";
while (<$file>) {
    next unless (m/^operator\.(.*)$/);
    (m/^([\w|\.|\\]*)\s=\s(.*)\s#\s(.*)$/);

    # 1.1) build the key
    $key = $1;

    # 1.2) build the array
    $_ = $2;
    @value = ();
    $value[0] = $3;
    if (m/^(.*)lspace:(\d)(.*)$/) { $value[1] = $2; } else { $value[1] = "5"; }
    if (m/^(.*)rspace:(\d)(.*)$/) { $value[2] = $2; } else { $value[2] = "5"; }
    if (m/^(.*)minsize:(\d)(.*)$/) { $value[3] = $2; } else { $value[3] = "1"; }
    $value[4] = (m/^(.*)largeop(.*)$/);
    $value[5] = (m/^(.*)movablelimits(.*)$/);
    $value[6] = (m/^(.*)stretchy(.*)$/);
    $value[7] = (m/^(.*)separator(.*)$/);
    $value[8] = (m/^(.*)accent(.*)$/);
    $value[9] = (m/^(.*)fence(.*)$/);
    $value[10] = (m/^(.*)symmetric(.*)$/);
    $value[11] = ""; # we don't store "priority" in our dictionary
    $value[12] = ""; # we don't store "linebreakstyle" in our dictionary
    if (m/^(.*)direction:([a-z]*)(.*)$/) { $value[13] = $2; }
    else { $value[13] = ""; }
    $value[14] = (m/^(.*)integral(.*)$/);
    $value[15] = (m/^(.*)mirrorable(.*)$/);

    # 1.3) save the key and value
    $moz_hash{$key} = [ @value ];
}

close($file);

################################################################################
# 2) If mode "make-js", generate tests/stretchy-and-large-operators.js and quit.
#    If mode "check", verify validity of our operator dictionary and quit.
#    If mode "compare", go to step 3)

if ($ARGV[0] eq "make-js") {
    print "generating file $FILE_JS...\n";
    open($file_js, ">$FILE_JS") ||
        die ("Couldn't open $FILE_JS!");
    print $file_js "// This file is automatically generated. Do not edit.\n";
    print $file_js "var stretchy_and_large_operators = [";
    @moz_keys = (keys %moz_hash);
    while ($key = pop(@moz_keys)) {
        @moz = @{ $moz_hash{$key} };

        $_ = $key;
        (m/^operator\.([\w|\.|\\]*)\.(prefix|infix|postfix)$/);
        $opname = "\\$1.$2: ";

        if (@moz[4]) {
            print $file_js "['$opname', '$1','l','$2'],";
        }

        if (@moz[6]) {
            $_ = substr(@moz[13], 0, 1);
            print $file_js "['$opname', '$1','$_','$2'],";
        }
    }
    print $file_js "];\n";
    close($file_js);
    exit 0;
}

if ($ARGV[0] eq "check") {
    print "checking operator dictionary...\n";
    open($file_syntax_errors, ">$FILE_SYNTAX_ERRORS") ||
        die ("Couldn't open $FILE_SYNTAX_ERRORS!");

    $nb_errors = 0;
    $nb_warnings = 0;
    @moz_keys = (keys %moz_hash);
    # check the validity of our private data
    while ($key = pop(@moz_keys)) {
        @moz = @{ $moz_hash{$key} };
        $entry = &generateEntry($key, @moz);
        $valid = 1;

        if (!(@moz[13] eq "" ||
              @moz[13] eq "horizontal" ||
              @moz[13] eq "vertical")) {
            $valid = 0;
            $nb_errors++;
            print $file_syntax_errors "error: invalid direction \"$moz[13]\"\n";
        }

        if (!@moz[4] && @moz[14]) {
            $valid = 0;
            $nb_warnings++;
            print $file_syntax_errors "warning: operator is integral but not largeop\n";
        }
        
        $_ = @moz[0];
        if ((m/^(.*)[iI]ntegral(.*)$/) && !@moz[14]) {
            $valid = 0;
            $nb_warnings++;
            print $file_syntax_errors "warning: operator contains the term \"integral\" in its comment, but is not integral\n";
        }

        if (!$valid) {
            print $file_syntax_errors $entry;
            print $file_syntax_errors "\n";
        }
    }

    # check that all forms have the same direction.
    @moz_keys = (keys %moz_hash);
    while ($key = pop(@moz_keys)) {

        if (@{ $moz_hash{$key} }) {
            # the operator has not been removed from the hash table yet.

            $_ = $key;
            (m/^([\w|\.|\\]*)\.(prefix|infix|postfix)$/);
            $key_prefix = "$1.prefix";
            $key_infix = "$1.infix";
            $key_postfix = "$1.postfix";
            @moz_prefix = @{ $moz_hash{$key_prefix} };
            @moz_infix = @{ $moz_hash{$key_infix} };
            @moz_postfix = @{ $moz_hash{$key_postfix} };

            $same_direction = 1;

            if (@moz_prefix) {
                if (@moz_infix &&
                    !($moz_infix[13] eq $moz_prefix[13])) {
                    $same_direction = 0;
                }
                if (@moz_postfix &&
                    !($moz_postfix[13] eq $moz_prefix[13])) {
                    $same_direction = 0;
                }
            }
            if (@moz_infix) {
                if (@moz_postfix &&
                    !($moz_postfix[13] eq $moz_infix[13])) {
                    $same_direction = 0;
                }
            }

            if (!$same_direction) {
                $nb_errors++;
                print  $file_syntax_errors
                    "error: operator has a stretchy form, but all forms";
                print  $file_syntax_errors
                    " have not the same direction\n";
                if (@moz_prefix) {
                    $_ = &generateEntry($key_prefix, @moz_prefix);
                    print $file_syntax_errors $_;
                }
                if (@moz_infix) {
                    $_ = &generateEntry($key_infix, @moz_infix);
                    print $file_syntax_errors $_;
                }
                if (@moz_postfix) {
                    $_ = &generateEntry($key_postfix, @moz_postfix);
                    print $file_syntax_errors $_;
                }
                print $file_syntax_errors "\n";
            }
            
            if (@moz_prefix) {
                delete $moz_hash{$key.prefix};
            }
            if (@moz_infix) {
                delete $moz_hash{$key_infix};
            }
            if (@moz_postfix) {
                delete $moz_hash{$key_postfix};
            }
        }
    }

    close($file_syntax_errors);
    print "\n";
    if ($nb_errors > 0 || $nb_warnings > 0) {
        print "$nb_errors error(s) found\n";
        print "$nb_warnings warning(s) found\n";
        print "See output file $FILE_SYNTAX_ERRORS.\n\n";
    } else {
        print "No error found.\n\n";
    }

    exit 0;
}

################################################################################
# 3) build %wg_hash and @wg_keys from the page $WG_DICTIONARY

print "loading $WG_DICTIONARY...\n";
$parser = new XML::DOM::Parser;
$doc = $parser->parsefile($WG_DICTIONARY)->getDocumentElement;

print "building dictionary...\n";
@wg_keys = ();
$entries = $doc->getElementsByTagName("entry");
$n = $entries->getLength;

for ($i = 0; $i < $n; $i++) {
    $entry = $entries->item($i);
    
    # 3.1) build the key
    $key = "operator.";

    $_ = $entry->getAttribute("unicode");
    $_ = "$_-";
    while (m/^U?0(\w*)-(.*)$/) {
        # Concatenate .\uNNNN
        $key = "$key\\u$1";
        $_ = $2;
    }

    $_ = $entry->getAttribute("form"); # "Form"
    $key = "$key.$_";

    # 3.2) build the array
    @value = ();
    $value[0] = lc($entry->getAttribute("description"));
    $value[1] = $entry->getAttribute("lspace");
    if ($value[1] eq "") { $value[1] = "5"; }
    $value[2] = $entry->getAttribute("rspace");
    if ($value[2] eq "") { $value[2] = "5"; }
    $value[3] = $entry->getAttribute("minsize");
    if ($value[3] eq "") { $value[3] = "1"; }

    $_ = $entry->getAttribute("properties");
    $value[4] = (m/^(.*)largeop(.*)$/);
    $value[5] = (m/^(.*)movablelimits(.*)$/);
    $value[6] = (m/^(.*)stretchy(.*)$/);
    $value[7] = (m/^(.*)separator(.*)$/);
    $value[8] = (m/^(.*)accent(.*)$/);
    $value[9] = (m/^(.*)fence(.*)$/);
    $value[10] = (m/^(.*)symmetric(.*)$/);
    $value[11] = $entry->getAttribute("priority");
    $value[12] = $entry->getAttribute("linebreakstyle");

    # not stored in the WG dictionary
    $value[13] = ""; # direction
    $value[14] = ""; # integral
    $value[15] = ""; # mirrorable

    # 3.3) save the key and value
    push(@wg_keys, $key);
    $wg_hash{$key} = [ @value ];
}
$doc->dispose;
@wg_keys = reverse(@wg_keys);

################################################################################
# 4) Compare the two dictionaries and output the result

print "comparing dictionaries...\n";
open($file_differences, ">$FILE_DIFFERENCES") ||
    die ("Couldn't open $FILE_DIFFERENCES!");
open($file_new_dictionary, ">$FILE_NEW_DICTIONARY") ||
    die ("Couldn't open $FILE_NEW_DICTIONARY!");

$conflicting = 0; $conflicting_stretching = 0;
$new = 0; $new_stretching = 0;
$obsolete = 0; $obsolete_stretching = 0;
$unchanged = 0;

# 4.1) look to the entries of the WG dictionary
while ($key = pop(@wg_keys)) {

    @wg = @{ $wg_hash{$key} };
    delete $wg_hash{$key};
    $wg_value = &generateCommon(@wg);

    if (exists($moz_hash{$key})) {
        # entry is in both dictionary
        @moz = @{ $moz_hash{$key} };
        delete $moz_hash{$key};
        $moz_value = &generateCommon(@moz);
        if ($moz_value ne $wg_value) {
            # conflicting entry
            print $file_differences "[conflict]";
            $conflicting++;
            if ($moz[6] != $wg[6]) {
                print $file_differences "[stretching]";
                $conflicting_stretching++;
            }
            print $file_differences " - $key ($wg[0])\n";
            print $file_differences "-$moz_value\n+$wg_value\n\n";
            $_ = &completeCommon($wg_value, $key, @moz, @wg);
            print $file_new_dictionary $_;
        } else {
            # unchanged entry
            $unchanged++;
            $_ = &completeCommon($wg_value, $key, @moz, @wg);
            print $file_new_dictionary $_;
        }
    } else {
        # we don't have this entry in our dictionary yet
        print $file_differences "[new entry]";
        $new++;
        if ($wg[6]) {
            print $file_differences "[stretching]";
            $new_stretching++;
        }
        print $file_differences " - $key ($wg[0])\n";
        print $file_differences "-\n+$wg_value\n\n";
        $_ = &completeCommon($wg_value, $key, (), @wg);
        print $file_new_dictionary $_;
    }
}

print $file_new_dictionary
    "\n# Entries below are not part of the official MathML dictionary\n\n";
# 4.2) look in our dictionary the remaining entries
@moz_keys = (keys %moz_hash);
@moz_keys = reverse(sort(@moz_keys));

while ($key = pop(@moz_keys)) {
    @moz = @{ $moz_hash{$key} };
    $moz_value = &generateCommon(@moz);
    print $file_differences "[obsolete entry]";
    $obsolete++;
    if ($moz[6]) {
        print $file_differences "[stretching]";
        $obsolete_stretching++;
    }
    print $file_differences " - $key ($moz[0])\n";
    print $file_differences "-$moz_value\n+\n\n";
    $_ = &completeCommon($moz_value, $key, (), @moz);
    print $file_new_dictionary $_;
}

close($file_differences);
close($file_new_dictionary);

print "\n";
print "- $obsolete obsolete entries ";
print "($obsolete_stretching of them are related to stretching)\n";
print "- $unchanged unchanged entries\n";
print "- $conflicting conflicting entries ";
print "($conflicting_stretching of them are related to stretching)\n";
print "- $new new entries ";
print "($new_stretching of them are related to stretching)\n";
print "\nSee output files $FILE_DIFFERENCES and $FILE_NEW_DICTIONARY.\n\n";
print "After having modified the dictionary, please run";
print "./updateOperatorDictionary check\n\n";
exit 0;

################################################################################
sub usage {
    # display the accepted command syntax and quit
    print "usage:\n";
    print "  ./updateOperatorDictionary.pl compare [dictionary]\n";
    print "  ./updateOperatorDictionary.pl check\n";
    print "  ./updateOperatorDictionary.pl make-js\n";
    exit 0;
}

sub generateCommon {
    # helper function to generate the string of data shared by both dictionaries
    my(@v) = @_;
    $entry = "lspace:$v[1] rspace:$v[2]";
    if ($v[3] ne "1") { $entry = "$entry minsize:$v[3]"; }
    if ($v[4]) { $entry = "$entry largeop"; }
    if ($v[5]) { $entry = "$entry movablelimits"; }
    if ($v[6]) { $entry = "$entry stretchy"; }
    if ($v[7]) { $entry = "$entry separator"; }
    if ($v[8]) { $entry = "$entry accent"; }
    if ($v[9]) { $entry = "$entry fence"; }
    if ($v[10]) { $entry = "$entry symmetric"; }
    return $entry;
}

sub completeCommon {
    # helper to add key and private data to generateCommon
    my($entry, $key, @v_moz, @v_wg) = @_;
    
    $entry = "$key = $entry";

    if ($v_moz[13]) { $entry = "$entry direction:$v_moz[13]"; }
    if ($v_moz[14]) { $entry = "$entry integral"; }
    if ($v_moz[15]) { $entry = "$entry mirrorable"; }

    if ($v_moz[0]) {
        # keep our previous comment
        $entry = "$entry # $v_moz[0]";
    } else {
        # otherwise use the description given by the WG
        $entry = "$entry # $v_wg[0]";
    }

    $entry = "$entry\n";
    return $entry;
}

sub generateEntry {
    # helper function to generate an entry of our operator dictionary
    my($key, @moz) = @_;
    $entry = &generateCommon(@moz);
    $entry = &completeCommon($entry, $key, @moz, @moz);
    return $entry;
}
