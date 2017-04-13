#!/usr/bin/env python
# A script and module for manipulating Alien package managers.
# Copyright 2009 Michael Homer. Released under the GNU GPL 2 or later.

import os
import subprocess
import CheckDependencies
from PythonUtils import *

alien_manager_states = {}
alien_package_states = {}

def parse_rule(rule):
    alientype, alienpkg  = split(rule['program'])
    lowerbound = ''
    upperbound = ''
    for comp, val in rule['versions']:
        if comp == '>=':
            lowerbound = val
        elif comp == '<':
            upperbound = val
        elif comp == '=' or comp == '==':
            lowerbound = val
            upperbound = val
    return alientype, alienpkg, lowerbound, upperbound


def dependencymet(rule):
    """Return True if rule['program'] is installed and meets the
       rule['versions'] requirements (if any).  The lowerbound is
       inclusive and the upperbound is exclusive, except if lowerbound
       and upperbound are equal then that indicates an exact match."""
    alientype, alienpkg, lowerbound, upperbound = parse_rule(rule)
    if 0 == subprocess.call(['Alien-' + alientype, '--met', alienpkg,
                             lowerbound, upperbound]):
        return True
    return False

    
def getinstallversion(rule):
    """Return version of rule['program'] to install.  If rule['versions']
       is specified then returned version should meet those
       requirements (as described by the dependencymet() function)."""
    alientype, alienpkg, lowerbound, upperbound = parse_rule(rule)
    p = subprocess.Popen(['Alien-' + alientype, '--getinstallversion',
                          alienpkg, lowerbound, upperbound], stdout=subprocess.PIPE)
    return p.stdout.read().strip()


def split(program):
    return program.split(':', 1)


def havealienmanager(alientype):
    if alientype in alien_manager_states:
        return alien_manager_states[alientype]
    res = 0 == subprocess.call(['Alien-' + alientype, '--have-manager'])
    alien_manager_states[alientype] = res
    return res


def getmanagerrule(alientype):
    p = subprocess.Popen(['Alien-' + alientype, '--get-manager-rule'],
                         stdout=subprocess.PIPE)
    rulestr = p.stdout.read().strip()
    rule = CheckDependencies.interpret_dependency_line(rulestr)
    return rule

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print "Usage: Alien --<mode> AlienType:alienpkg [...]\n"
        print "Valid options for <mode> are:"
        print "    --get-version"
        print "    --getinstallversion"
        print "    --greater-than"
        print "    --met|--within-range|--interval"
        print "    --have-manager"
        print "    --get-manager-rule"
        print "    --install\n"
        print "Valid options for AlienType are:"
        print "    CPAN"
        print "    LuaRocks"
        print "    PIP"
        print "    RubyGems\n"
        print "Example:"
        print "    Alien --install CPAN:XML::Parser"
        print "    Alien --install PIP:burn"
        exit(1)
    mode = sys.argv[1]
    prog = sys.argv[2]
    try:
        alientype, alienpkg = split(prog)
    except ValueError:
        print "Error: missing program name"
        exit(1)
    args = ['Alien-' + alientype, mode, alienpkg] + sys.argv[3:]
    exit(subprocess.call(args))

