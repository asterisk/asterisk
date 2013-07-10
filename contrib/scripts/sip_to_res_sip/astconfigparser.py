from astdicts import MultiOrderedDict

def merge_values(left, right, key):
    """Merges values from right into left."""
    if isinstance(left, list):
        vals0 = left
    else: # assume dictionary
        vals0 = left[key] if key in left else []
    vals1 = right[key] if key in right else []

    return vals0 + [i for i in vals1 if i not in vals0]

###############################################################################

class Section(MultiOrderedDict):
    """A Section is a MultiOrderedDict itself that maintains a list of
       key/value options.  However, in the case of an Asterisk config
       file a section may have other defaults sections that is can pull
       data from (i.e. templates).  So when an option is looked up by key
       it first checks the base section and if not found looks in the
       added default sections. If not found at that point then a 'KeyError'
       exception is raised.
    """
    def __init__(self, defaults = []):
        MultiOrderedDict.__init__(self)
        self._defaults = defaults

    def __getitem__(self, key):
        """Get the value for the given key. If it is not found in the 'self'
           then check inside the defaults before declaring unable to locate."""
        if key in self:
            return MultiOrderedDict.__getitem__(self, key)

        for default in self._defaults:
            if key in default:
                return default[key]

        raise KeyError(key)

    def keys(self):
        res = MultiOrderedDict.keys(self)
        for d in self._defaults:
            for key in d.keys():
                if key not in res:
                    res.append(key)
        return res

    def add_default(self, default):
        self._defaults.append(default)

    def get_merged(self, key):
        """Return a list of values for a given key merged from default(s)"""
        # first merge key/values from defaults together
        merged = []
        for i in self._defaults:
            if not merged:
                merged = i
                continue
            merged = merge_values(merged, i, key)
        # then merge self in
        return merge_values(merged, self, key)

###############################################################################

def remove_comment(line):
    """Remove any commented elements from the given line"""
    line = line.partition(COMMENT)[0]
    return line.rstrip()

def try_section(line):
    """Checks to see if the given line is a section. If so return the section
       name, otherwise return 'None'.
    """
    if not line.startswith('['):
        return None

    first, second, third = line.partition(']')
    # TODO - third may contain template, parse to see if it is a template
    #        or is a list of templates...return?
    return first[1:]

def try_option(line):
    """Parses the line as an option, returning the key/value pair."""
    first, second, third = line.partition('=')
    return first.strip(), third.strip()

###############################################################################

def get_value(mdict, key, index=-1):
    """Given a multi-dict, retrieves a value for the given key. If the key only
       holds a single value return that value. If the key holds more than one
       value and an index is given that is greater than or equal to zero then
       return the value at the index. Otherwise return the list of values."""
    vals = mdict[key]
    if len(vals) == 1:
        return vals[0]
    if index >= 0:
        return vals[index]
    return vals

def find_value(mdicts, key, index=-1):
    """Given a list of multi-dicts, try to find value(s) for the given key."""
    if not isinstance(mdicts, list):
        # given a single multi-dict
        return get_value(mdicts, key, index)

    for d in mdicts:
        if key in d:
            return d[key]
    # not found, throw error
    raise KeyError(key)

def find_dict(mdicts, key, val):
    """Given a list of mult-dicts, return the multi-dict that contains
       the given key/value pair."""

    def found(d):
        # just check the first value of the key
        return key in d and d[key][0] == val

    if isinstance(mdicts, list):
        try:
            return [d for d in mdicts if found(d)][0]
        except IndexError:
            pass
    elif found(mdicts):
        return mdicts

    raise LookupError("Dictionary not located for key = %s, value = %s"
                      % (key, val))

###############################################################################

COMMENT = ';'
DEFAULTSECT = 'general'

class MultiOrderedConfigParser:
    def __init__(self):
        self._default = MultiOrderedDict()
        # sections contain dictionaries of dictionaries
        self._sections = MultiOrderedDict()

    def default(self):
        return self._default

    def sections(self):
        return self._sections

    def section(self, section, index=-1):
        """Retrieves a section dictionary for the given section. If the section
           holds only a single section dictionary return that dictionary. If
           the section holds more than one dictionary and an index is given
           that is greater than or equal to zero then return the dictionary at
           the index. Otherwise return the list of dictionaries for the given
           section name."""
        try:
            return get_value(self._sections, section, index)
        except KeyError:
            raise LookupError("section %r not found" % section)

    def add_section(self, section, defaults=[]):
        """Adds a section with the given name and defaults."""
        self._sections[section] = res = Section(defaults)
        return res

    def get(self, key, section=DEFAULTSECT, index=-1):
        """Retrieves a value for the given key from the given section. If the
           key only holds a single value return that value. If the key holds
           more than one value and an index is given that is greater than or
           equal to zero then return the value at the index. Otherwise return
           the list of values."""
        try:
            if section == DEFAULTSECT:
                return get_value(self._default, key, index)

            # search section(s)
            return find_value(self.section(section), key, index)
        except KeyError:
            # check default section if we haven't already
            if section != DEFAULTSECT:
                return self.get(key, DEFAULTSECT, index)
            raise LookupError("key %r not found in section %r"
                              % (key, section))

    def set(self, key, val, section=DEFAULTSECT):
        """Sets an option in the given section."""
        if section == DEFAULTSECT:
            self._default[key] = val
        else:
            # for now only set value in first section
            self.section(section, 0)[key] = val

    def read(self, filename):
        try:
            with open(filename, 'rt') as file:
                self._read(file, filename)
        except IOError:
            print "Could not open file ", filename, " for reading"

    def _read(self, file, filename):
        for line in file:
            line = remove_comment(line)
            if not line:
                # line was empty or was a comment
                continue

            section = try_section(line)
            if section:
                if section == DEFAULTSECT:
                    sect = self._default
                else:
                    self._sections[section] = sect = Section([self._default])
                    # TODO - if section has templates add those
                    #        with sect.add_default
                continue

            key, val = try_option(line)
            sect[key] = val

    def write(self, filename):
        try:
            with open(filename, 'wt') as file:
                self._write(file)
        except IOError:
            print "Could not open file ", filename, " for writing"
        pass

    def _write(self, file):
        # TODO - need to write out default section, but right now in
        #        our case res_sip.conf has not default/general section
        for section, sect_list in self._sections.iteritems():
            # every section contains a list of dictionaries
            for sect in sect_list:
                file.write("[%s]\n" % section)
                for key, val_list in sect.iteritems():
                    # every value is also a list
                    for v in val_list:
                        key_val = key
                        if (v is not None):
                            key_val += " = " + str(v)
                        file.write("%s\n" % (key_val))
                file.write("\n")
