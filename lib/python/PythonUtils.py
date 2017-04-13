#!/usr/bin/env python

import os, sys, os.path, time
from subprocess import Popen, PIPE

def Split_Version_Revision(version_with_revision):
	r = version_with_revision.split('-')[-1]
	import re
	rev_format = 'r([0-9]+p([0-9]+))?'
	rev_format_c = re.compile(rev_format)
	m = rev_format_c.match(r)
	if m:
		return version_with_revision[:-len(r)-1],r
	else:
		return version_with_revision,''

def Join_Version_Revision(v, r):
	if r and r != 'r'+str(sys.maxint):
	    return v+'-'+r
	else:
	    return v

def any(S):
	for x in S:
		if x:
			return True
	return False

def all(S):
	for x in S:
		if not x:
			return False
	return True

def Get_Dir(mode, p, v=''):
	cmd=""". ScriptFunctions
Import Directories
Get_Dir \"%s\" \"%s\" \"%s\""""%(mode,p,v)
	return bash(cmd)

colorGray="\033[1;30m"
colorBoldBlue="\033[1;34m"
colorBrown="\033[33m"
colorYellow="\033[1;33m"
colorBoldGreen="\033[1;32m"
colorBoldRed="\033[1;31m"
colorCyan="\033[36m"
colorBoldCyan="\033[1;36m"
colorRedWhite="\033[41;37m"
colorNormal="\033[0m"

persistentScriptName = ""
def Log(message, scriptName, color):
	global persistentScriptName
	if scriptName:
		persistentScriptName = scriptName
	for line in message.split('\n'):
		sys.stderr.write("\n" + colorGray+persistentScriptName+':'+colorNormal+" "+color+line+colorNormal)

def Log_Error(message, scriptName = None):   Log(message, scriptName, colorBoldRed)
def Log_Normal(message, scriptName = None):  Log(message, scriptName, colorCyan)
def Log_Terse(message, scriptName = None):   Log(message, scriptName, colorBoldCyan)
def Log_Verbose(message, scriptName = None): Log(message, scriptName, colorNormal)
def Log_Debug(message, scriptName = None):   Log(message, scriptName, colorRedWhite)

def Question_Line(): sys.stderr.write(colorNormal+colorNormal)

def Log_Question(message, scriptName = None):
	Question_Line()
	Log(scriptName, message, colorBoldCyan)

def Ask_Option(message, scriptName = None):
	Log_Question(scriptName, message)
	while True:
		reply = sys.stdin.readline().strip()
		if reply:
			return reply.lower()
	return None

def Ask(message, scriptName = None):
	reply = Ask_Option(scriptName, message + ' [Y/n]')
	if reply == 'n':
		return False
	else:
		return True

def getGoboVariable(name, filename = 'GoboPath', isList = False):

	if filename != 'GoboPath':
		goboUserSettings = getGoboVariable('goboUserSettings')
		goboSettings = getGoboVariable('goboSettings')
		goboPrograms = getGoboVariable('goboPrograms')
		goboScriptsDefaults = goboPrograms+'/Scripts/Current/Resources/Defaults/Settings/'
		goboManagerDefaults = goboPrograms+'/Manager/Current/Resources/Defaults/Settings/'
		files = [ goboUserSettings+"/"+filename, goboSettings + "/"+filename, filename]
	else:
		files = [ filename]
	if name:
		if not isList:
			try:
				return os.environ[name]
			except:
				for file in files:
					value = bash(". \"%s\" 2> /dev/null; echo -n \"$%s\""%(file,name))
					if value:
						break
				os.environ[name] = value
				return value
		else:
			try:
				return os.environ[name].split()
			except:
				for file in files:
					os.environ[name] = bash(". \"" + file +"\" 2> /dev/null; for i in \"${"+name+"[@]}\"; do echo \"$i\"; done")
					if os.environ[name]:
						break
				return os.environ[name].split()
	else:
		#if name is not empty, assuming the whole file is going to be returned as a list of lines
		for file in files:
			ret = bash("cat \"" + file +"\" 2> /dev/null")
			if ret:
				return [ line.split('#')[0].strip() for line in ret.split('\n') if line.split('#')[0].strip() ]
		return []

def bash(command, mode='o'):
	"""Execute a bash command. Return the output, the return value, or both.

	mode may be o (output), v (return value), or ov (tuple (output, value).
	"""
	if 'v' == mode:
		return os.spawnl(os.P_WAIT, '/bin/bash', 'bash', '-c', command)
	else:
		return _bash2(command, mode)

def _bash2(command, mode='o'):
	stdout_r, stdout_w = None, None
	(stdin_r, stdin_w) = os.pipe()
	if 'o' in mode:
		(stdout_r, stdout_w) = os.pipe()
	p = Popen('/bin/bash', stdin=stdin_r, stdout=stdout_w)
	os.write(stdin_w, command + '\nexit $?\n')
	os.close(stdin_w)
	output = []
	ret = p.wait()
	os.close(stdin_r)
	if stdout_r:
		os.close(stdout_w)
		out = os.read(stdout_r, 32768)
		while out != '':
			output.append(out)
			out = os.read(stdout_r, 32768)
		output = (''.join(output)).strip()
		os.close(stdout_r)
	if 'ov' == mode:
		return (output, ret)
	elif 'o' in mode:
		return output
	elif 'v' in mode:
		return ret
	return None

def getCompileOptions():
	import os

	goboUserSettings = getGoboVariable('goboUserSettings')
	goboSettings = getGoboVariable('goboSettings')
	goboPrograms = getGoboVariable('goboPrograms')
	goboCompileDefaults = goboPrograms+'/Compile/Current/Resources/Defaults/Settings/'
	goboSettingsFallback = '/System/Settings'
	compileSettingsFiles = [ goboUserSettings + "/Compile/Compile.conf", goboSettings + "/Compile/Compile.conf",  goboCompileDefaults+"/Compile/Compile.conf", goboSettingsFallback+"/Compile/Compile.conf" ]

	try:
		compileRecipeDirs = os.environ['compileRecipeDirs'].strip('\n').split()
	except:
		for file in compileSettingsFiles:
			compileRecipeDirs = bash(". \"" +file+"\" 2> /dev/null; for i in \"${compileRecipeDirs[@]}\"; do echo \"$i\"; done").split('\n')
			if compileRecipeDirs and compileRecipeDirs[0]:
				break

	try:
		getRecipeStores = os.environ['getRecipeStores'].strip('\n').split('\n')
	except:
		for file in compileSettingsFiles:
			getRecipeStores = bash(". \"" + file +"\" 2> /dev/null; for i in \"${getRecipeStores[@]}\"; do echo \"$i\"; done").split('\n')
			if getRecipeStores and getRecipeStores[0]:
				break

	#import sys
	#sys.stderr.write(str(compileRecipeDirs))
	#sys.stderr.write(str(getRecipeStores))

	import os.path
	return (map(os.path.expanduser,compileRecipeDirs), getRecipeStores)

currentString = ''
def consoleProgressHook(label, i = 0, n = 0, showWhenChanged = False, scriptName=None ):
	global spin
	global currentString
	if not hasattr(consoleProgressHook, 'endString'):
		consoleProgressHook.endString = '\n'
	if not label:
		sys.stderr.write('\n')
		return

	if label != currentString:
		if currentString:
			sys.stderr.write('\n')
		Log_Normal(label+'...  ', scriptName=scriptName)
		currentString = label

	if i >= n:
		sys.stderr.write(consoleProgressHook.endString)
		currentString = ''
		return

	if  i % 4 == 0:
		sys.stderr.write('\b'+colorCyan+'|'+colorNormal)
	elif i % 4 == 1:
		sys.stderr.write('\b'+colorCyan+'/'+colorNormal)
	elif i % 4 == 2:
		sys.stderr.write(colorCyan+'\b-'+colorNormal)
	elif i % 4 == 3:
		sys.stderr.write(colorCyan+'\b\\'+colorNormal)

def caseinsensitive_sort(stringList):
	"""case-insensitive string comparison sort
	doesn't do locale-specific compare
	though that would be a nice addition
	usage: stringList = caseinsensitive_sort(stringList)"""

	tupleList = [(x.lower(), x) for x in stringList]
	tupleList.sort()
	return [x[1] for x in tupleList]

class KeyInsensitiveDict:
	"""Dictionary, that has case-insensitive keys.

	Keys are retained in their original form
	when queried with .keys() or .items().

	Implementation: An internal dictionary maps lowercase
	keys to (key,value) pairs. All key lookups are done
	against the lowercase keys, but all methods that expose
	keys to the user retrieve the original keys."""

	def __init__(self, src=None):
		"""Create an empty dictionary, or update from 'src'"""
		self._dict = {}
		if src:
			self.update(src)

	def __getitem__(self, key):
		"""Retrieve the value associated with 'key' (in any case)."""
		k = key.lower()
		return self._dict[k][1]

	def __setitem__(self, key, value):
		"""Associate 'value' with 'key'. If 'key' already exists, but
		in different case, it will be replaced."""
		k = key.lower()
		self._dict[k] = (key, value)

	def __iter__(self):
		return iter((v[0] for v in self._dict.values()))

	def __iterkeys__(self):
		return iter((v[0] for v in self._dict.values()))

	def has_key(self, key):
		"""Case insensitive test whether 'key' exists."""
		k = key.lower()
		return self._dict.has_key(k)
	__contains__ = has_key

	def keys(self):
		"""List of keys in their original case."""
		return [v[0] for v in self._dict.values()]

	def values(self):
		"""List of values."""
		return [v[1] for v in self._dict.values()]

	def items(self):
		"""List of (key,value) pairs."""
		return self._dict.values()

	def get(self, key, default=None):
		"""Retrieve value associated with 'key' or return default value
		if 'key' doesn't exist."""
		try:
			return self[key]
		except KeyError:
			return default

	def setdefault(self, key, default):
		"""If 'key' doesn't exists, associate it with the 'default' value.
		Return value associated with 'key'."""
		if not self.has_key(key):
			self[key] = default
		return self[key]

	def update(self, src):
		"""Copy (key,value) pairs from 'src' (either a dict-like object or an
		iterable of 2-sequences."""
		if hasattr(src, 'items'):
			for k,v in dict.items():
				self[k] = v
		else: # Is an iterator, generator, list, etc
			for k,v in src:
				self[k] = v

	def __repr__(self):
		"""String representation of the dictionary."""
		items = ", ".join([("%r: %r" % (k,v)) for k,v in self.items()])
		return "{%s}" % items

	def __str__(self):
		"""String representation of the dictionary."""
		return repr(self)

	def __len__(self):
		"""Size of the dictionary, implements len()"""
		return len(self._dict)

class TextHarvester:
	def __init__(self, data, stripTags = False):
		self.c = 0
		self.data = data
		self.stripTags = stripTags

	def skipUntilNext(self, s):
		self.c = self.data.index(s,self.c) + len(s)

	def getUntilNext(self, s):
		begin = self.c
		end = self.data.index(s, begin)
		self.c = end
		if self.stripTags:
			return self.doStripTags(self.data[begin:end])
		else:
			return self.data[begin:end]

	def getUntilEnd(self):
		x = self.data[self.c:]
		self.c = len(self.data)
		return x

	def contains(self, s):
		return self.data.find(s, self.c) > -1

	def getLinesUntil(self, s):
		begin = self.c
		end = self.data.index(s, begin)
		self.c = end
		if self.stripTags:
			return self.doStripTags(self.data[begin:end].split('\n'))
		else:
			return self.data[begin:end].split('\n')

	def doStripTags(self, s):
		while True:
			beg = s.find('<')
			end = s.find('>')
			#print beg, end
			if beg > -1 and end > -1 and beg < end:
				s1 = s[:beg]
				s2 = s[end+1:]
				s = s1 + s2
			else:
				return s

class ParseError(Exception):
    def __init__(self, txt):
        Exception.__init__(self, txt)
