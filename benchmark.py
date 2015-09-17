#!python
# this program is intended to randomly generate a set of commands 
#
# Author: Kuei (Jack) Sun 
# Email: kuei.sun@utoronto.ca

import sys
import time
import random
import string

WRITE_MAX_LENGTH = 320
FILE_MAX_LENGTH = 8
MAX_NUM_ENTRIES = 16
ALNUM_CHARS = string.ascii_letters + string.digits

actions_in_curdir = 0
curr_num_entries = 0

def get_rand_name():
	length = random.randint(3, FILE_MAX_LENGTH-1)
	name = [random.choice(string.ascii_letters)]
	for n in xrange(length):
		name.append(random.choice(ALNUM_CHARS))
	return "".join(name)

def chdir(f, curdir):
	global actions_in_curdir
	dirent = random.choice(curdir['dir'])
	if dirent[1] != curdir:
		if actions_in_curdir > 0 or dirent[0] != "..":
			f.write("cd %s\n"%dirent[0])
			actions_in_curdir = -1
			return dirent[1]
	return curdir

def mkdir(f, curdir):
	global curr_num_entries
	if curr_num_entries < MAX_NUM_ENTRIES:
		name = get_rand_name()
		newdir = { 'file':[], 'dir':[("..", curdir)] }
		curdir['dir'].append((name, newdir))
		f.write("mkdir %s\n"%name)
		curr_num_entries = curr_num_entries + 1
	return curdir
	
def touch(f, curdir):
	global curr_num_entries
	if curr_num_entries < MAX_NUM_ENTRIES:
		name = get_rand_name()
		if name not in curdir['file']:
			curdir['file'].append(name)
			f.write("touch %s\n"%name)
			curr_num_entries = curr_num_entries + 1
	return curdir	

def write(f, curdir):
	if len(curdir['file']) > 0:
		which = random.choice(curdir['file'])
		length = random.randint(1, WRITE_MAX_LENGTH)
		data = []
		for n in xrange(length):
			data.append(random.choice(ALNUM_CHARS))
		f.write("write %s %s\n"%("".join(data), which))
	return curdir

WHATEVER = 0
FILE = 1
DIRECTORY = 2

def remove(f, curdir, what=WHATEVER):
	global curr_num_entries
	if what == 0:
		what = random.randint(FILE, DIRECTORY)
	if what == FILE:
		if len(curdir['file']) > 0:
			which = random.choice(curdir['file'])
			curdir['file'].remove(which)
			f.write("rm %s\n"%which)
			curr_num_entries = curr_num_entries - 1
		else:
			curdir = remove(f, curdir, DIRECTORY)
	else:
		if len(curdir['dir']) <= 1:
			if len(curdir['file']) > 0:
				curdir = remove(f, curdir, FILE)
		else:
			name = ".."
			while name == "..":
				dirent = random.choice(curdir['dir'])
				name = dirent[0]
			if len(dirent[1]['dir']) <= 1 and \
				len(dirent[1]['file']) == 0:
				curdir['dir'].remove(dirent)
				f.write("rm %s\n"%dirent[0])
				curr_num_entries = curr_num_entries - 1
			else:
				curdir = chdir(f, curdir)
				curdir = remove(f, curdir)
	return curdir
	
CMDS = [ chdir, touch, mkdir, write, remove, remove ]

if __name__ == "__main__":
	root = { 'file':[], 'dir':[] }
	root['dir'].append(("..", root))
	pwd = root
	num_cmds = 300000
	if len(sys.argv) == 2:
		num_cmds = int(sys.argv[1])
	print "Generating %d commands"%num_cmds
	f = open("script.txt", "wb")
	for n in xrange(num_cmds):
		pwd = random.choice(CMDS)(f, pwd)
		actions_in_curdir = actions_in_curdir + 1
	f.write("checkfs\n")
	f.close()
	
