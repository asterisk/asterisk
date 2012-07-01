import sys
import os

REMOVE_THESE = ["-I/usr/include", "-I/usr/include/", "-L/usr/lib", "-L/usr/lib/"]

class Pkg:
	def __init__(self, pkg_name):
		self.name = pkg_name
		self.priority = 0
		self.vars = {}

	def parse(self, pkg_config_path):
		f = None
		for pkg_path in pkg_config_path.split(':'):
			if pkg_path[-1] != '/':
				pkg_path += '/'
			fname = pkg_path + self.name + '.pc'
			try:
				f = open(fname, "r")
				break
			except:
				continue
		if not f:
		  	#sys.stderr.write("pkgconfig.py: unable to find %s.pc in %s\n" % (self.name, pkg_config_path))
			return False

		for line in f.readlines():
			line = line.strip()
			if not line:
				continue
			if line[0]=='#':
				continue
			pos1 = line.find('=')
			pos2 = line.find(':')
			if pos1 > 0 and (pos1 < pos2 or pos2 < 0):
				pos = pos1
			elif pos2 > 0 and (pos2 < pos1 or pos1 < 0):
				pos = pos2
			else:
				continue
			name = line[:pos].lower()
			value = line[pos+1:]
			self.vars[name] = value
		f.close()

		for name in self.vars.keys():
			value = self.vars[name]
			while True:
				pos1 = value.find("${")
				if pos1 < 0:
					break
				pos2 = value.find("}")
				if pos2 < 0:
					break
				value = value.replace(value[pos1:pos2+1], self.vars[value[pos1+2:pos2]])
			self.vars[name] = value
		return True

	def requires(self):
		if not 'requires' in self.vars:
			return []
		deps = []
		req_list = self.vars['requires']
		for req_item in req_list.split(','):
			req_item = req_item.strip()
			for i in range(len(req_item)):
				if "=<>".find(req_item[i]) >= 0:
					deps.append(req_item[:i].strip())
					break
		return deps

	def libs(self):
		if not 'libs' in self.vars:
			return []
		return self.vars['libs'].split(' ')

	def cflags(self):
		if not 'cflags' in self.vars:
			return []
		return self.vars['cflags'].split(' ')


def calculate_pkg_priority(pkg, pkg_dict, loop_cnt):
	if loop_cnt > 10:
		sys.stderr.write("Circular dependency with pkg %s\n" % (pkg))
		return 0
	reqs = pkg.requires()
	prio = 1
	for req in reqs:
		if not req in pkg_dict:
			continue
		req_pkg = pkg_dict[req]
		prio += calculate_pkg_priority(req_pkg, pkg_dict, loop_cnt+1)
	return prio

if __name__ == "__main__":
	pkg_names = []
	pkg_dict = {}
	commands = []
	exist_check = False

	for i in range(1,len(sys.argv)):
		if sys.argv[i][0] == '-':
			cmd = sys.argv[i]
			commands.append(cmd)
			if cmd=='--exists':
				exist_check = True
			elif cmd=="--help":
				print "This is not very helpful, is it"
				sys.exit(0)
			elif cmd=="--version":
				print "0.1"
				sys.exit(0)
		else:
			pkg_names.append(sys.argv[i])
			
	# Fix search path
	PKG_CONFIG_PATH = os.getenv("PKG_CONFIG_PATH", "").strip()
	if not PKG_CONFIG_PATH:
		PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/pkgconfig"
	PKG_CONFIG_PATH = PKG_CONFIG_PATH.replace(";", ":")

	# Parse files
	for pkg_name in pkg_names:
		pkg = Pkg(pkg_name)
		if not pkg.parse(PKG_CONFIG_PATH):
			sys.exit(1)
		pkg_dict[pkg_name] = pkg

	if exist_check:
		sys.exit(0)

	# Calculate priority based on dependency
	for pkg_name in pkg_dict.keys():
		pkg = pkg_dict[pkg_name]
		pkg.priority = calculate_pkg_priority(pkg, pkg_dict, 1)
	
	# Sort package based on dependency
	pkg_names = sorted(pkg_names, key=lambda pkg_name: pkg_dict[pkg_name].priority, reverse=True)

	# Get the options
	opts = []
	for cmd in commands:
		if cmd=='--libs':
			for pkg_name in pkg_names:
				libs = pkg_dict[pkg_name].libs()
				for lib in libs:
					opts.append(lib)
					if lib[:2]=="-l":
						break
			for pkg_name in pkg_names:
				opts += pkg_dict[pkg_name].libs()
		elif cmd=='--cflags':
			for pkg_name in pkg_names:
				opts += pkg_dict[pkg_name].cflags()
		elif cmd[0]=='-':
			sys.stderr.write("pkgconfig.py: I don't know how to handle " + sys.argv[i] + "\n")
	
	filtered_opts = []
	for opt in opts:
		opt = opt.strip()
		if not opt:
			continue
		if REMOVE_THESE.count(opt) != 0:
			continue
		if filtered_opts.count(opt) != 0:
			continue
		filtered_opts.append(opt)

	print ' '.join(filtered_opts)

