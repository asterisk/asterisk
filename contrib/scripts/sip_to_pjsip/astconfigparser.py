"""
Copyright (C) 2016, Digium, Inc.

This program is free software, distributed under the terms of
the GNU General Public License Version 2.
"""

import re
import glob
import itertools

from astdicts import OrderedDict
from astdicts import MultiOrderedDict


def merge_values(left, right, key):
    """Merges values from right into left."""
    if isinstance(left, list):
        vals0 = left
    else:  # assume dictionary
        vals0 = left[key] if key in left else []
    vals1 = right[key] if key in right else []

    return vals0 + [i for i in vals1 if i not in vals0]

###############################################################################


class Section(MultiOrderedDict):
    """
    A Section is a MultiOrderedDict itself that maintains a list of
    key/value options.  However, in the case of an Asterisk config
    file a section may have other defaults sections that is can pull
    data from (i.e. templates).  So when an option is looked up by key
    it first checks the base section and if not found looks in the
    added default sections. If not found at that point then a 'KeyError'
    exception is raised.
    """
    count = 0

    def __init__(self, defaults=None, templates=None):
        MultiOrderedDict.__init__(self)
        # track an ordered id of sections
        Section.count += 1
        self.id = Section.count
        self._defaults = [] if defaults is None else defaults
        self._templates = [] if templates is None else templates

    def __cmp__(self, other):
        """
        Use self.id as means of determining equality
        """
        return (self.id > other.id) - (self.id < other.id)

    def __eq__(self, other):
        """
        Use self.id as means of determining equality
        """
        return self.id == other.id

    def __lt__(self, other):
        """
        Use self.id as means of determining equality
        """
        return self.id < other.id

    def __gt__(self, other):
        """
        Use self.id as means of determining equality
        """
        return self.id > other.id

    def __le__(self, other):
        """
        Use self.id as means of determining equality
        """
        return self.id <= other.id

    def __ge__(self, other):
        """
        Use self.id as means of determining equality
        """
        return self.id >= other.id

    def get(self, key, from_self=True, from_templates=True,
            from_defaults=True):
        """
        Get the values corresponding to a given key. The parameters to this
        function form a hierarchy that determines priority of the search.
        from_self takes priority over from_templates, and from_templates takes
        priority over from_defaults.

        Parameters:
        from_self - If True, search within the given section.
        from_templates - If True, search in this section's templates.
        from_defaults - If True, search within this section's defaults.
        """
        if from_self and key in self:
            return MultiOrderedDict.__getitem__(self, key)

        if from_templates:
            if self in self._templates:
                return []
            for t in self._templates:
                try:
                    # fail if not found on the search - doing it this way
                    # allows template's templates to be searched.
                    return t.get(key, True, from_templates, from_defaults)
                except KeyError:
                    pass

        if from_defaults:
            for d in self._defaults:
                try:
                    return d.get(key, True, from_templates, from_defaults)
                except KeyError:
                    pass

        raise KeyError(key)

    def __getitem__(self, key):
        """
        Get the value for the given key. If it is not found in the 'self'
        then check inside templates and defaults before declaring raising
        a KeyError exception.
        """
        return self.get(key)

    def keys(self, self_only=False):
        """
        Get the keys from this section. If self_only is True, then
        keys from this section's defaults and templates are not
        included in the returned value
        """
        res = MultiOrderedDict.keys(self)
        if self_only:
            return res

        for d in self._templates:
            for key in d.keys():
                if key not in res:
                    res.append(key)

        for d in self._defaults:
            for key in d.keys():
                if key not in res:
                    res.append(key)
        return res

    def add_defaults(self, defaults):
        """
        Add a list of defaults to the section. Defaults are
        sections such as 'general'
        """
        defaults.sort()
        for i in defaults:
            self._defaults.insert(0, i)

    def add_templates(self, templates):
        """
        Add a list of templates to the section.
        """
        templates.sort()
        for i in templates:
            self._templates.insert(0, i)

    def get_merged(self, key):
        """Return a list of values for a given key merged from default(s)"""
        # first merge key/values from defaults together
        merged = []
        for i in reversed(self._defaults):
            if not merged:
                merged = i
                continue
            merged = merge_values(merged, i, key)

        for i in reversed(self._templates):
            if not merged:
                merged = i
                continue
            merged = merge_values(merged, i, key)

        # then merge self in
        return merge_values(merged, self, key)

###############################################################################

COMMENT = ';'
COMMENT_START = ';--'
COMMENT_END = '--;'

DEFAULTSECT = 'general'


def remove_comment(line, is_comment):
    """Remove any commented elements from the line."""
    if not line:
        return line, is_comment

    if is_comment:
        part = line.partition(COMMENT_END)
        if part[1]:
            # found multi-line comment end check string after it
            return remove_comment(part[2], False)
        return "", True

    part = line.partition(COMMENT_START)
    if part[1] and not part[2].startswith('-'):
        # found multi-line comment start check string before
        # it to make sure there wasn't an eol comment in it
        has_comment = part[0].partition(COMMENT)
        if has_comment[1]:
            # eol comment found return anything before it
            return has_comment[0], False

        # check string after it to see if the comment ends
        line, is_comment = remove_comment(part[2], True)
        if is_comment:
            # return possible string data before comment
            return part[0].strip(), True

        # otherwise it was an embedded comment so combine
        return ''.join([part[0].strip(), ' ', line]).rstrip(), False

    # find the first occurence of a comment that is not escaped
    match = re.match(r'.*?([^\\];)', line)

    if match:
         # the end of where the real string is is where the comment starts
         line = line[0:(match.end()-1)]
    if line.startswith(";"):
         # if the line is actually a comment just ignore it all
         line = ""

    return line.replace("\\", "").strip(), False

def try_include(line):
    """
    Checks to see if the given line is an include.  If so return the
    included filename, otherwise None.
    """

    match = re.match('^#include\s*([^;]+).*$', line)
    if match:
        trimmed = match.group(1).rstrip()
        quoted = re.match('^"([^"]+)"$', trimmed)
        if quoted:
            return quoted.group(1)
        bracketed = re.match('^<([^>]+)>$', trimmed)
        if bracketed:
            return bracketed.group(1)
        return trimmed
    return None


def try_section(line):
    """
    Checks to see if the given line is a section. If so return the section
    name, otherwise return 'None'.
    """
    # leading spaces were stripped when checking for comments
    if not line.startswith('['):
        return None, False, []

    section, delim, templates = line.partition(']')
    if not templates:
        return section[1:], False, []

    # strip out the parens and parse into an array
    templates = templates.replace('(', "").replace(')', "").split(',')
    # go ahead and remove extra whitespace
    templates = [i.strip() for i in templates]
    try:
        templates.remove('!')
        return section[1:], True, templates
    except:
        return section[1:], False, templates


def try_option(line):
    """Parses the line as an option, returning the key/value pair."""
    data = re.split('=>?', line, 1)
    # should split in two (key/val), but either way use first two elements
    return data[0].rstrip(), data[1].lstrip()

###############################################################################


def find_dict(mdicts, key, val):
    """
    Given a list of mult-dicts, return the multi-dict that contains
    the given key/value pair.
    """

    def found(d):
        return key in d and val in d[key]

    try:
        return [d for d in mdicts if found(d)][0]
    except IndexError:
        raise LookupError("Dictionary not located for key = %s, value = %s"
                          % (key, val))


def write_dicts(config_file, mdicts):
    """Write the contents of the mdicts to the specified config file"""
    for section, sect_list in mdicts.iteritems():
        # every section contains a list of dictionaries
        for sect in sect_list:
            config_file.write("[%s]\n" % section)
            for key, val_list in sect.iteritems():
                # every value is also a list
                for v in val_list:
                    key_val = key
                    if v is not None:
                        key_val += " = " + str(v)
                        config_file.write("%s\n" % (key_val))
            config_file.write("\n")

###############################################################################


class MultiOrderedConfigParser:
    def __init__(self, parent=None):
        self._parent = parent
        self._defaults = MultiOrderedDict()
        self._sections = MultiOrderedDict()
        self._includes = OrderedDict()

    def find_value(self, sections, key):
        """Given a list of sections, try to find value(s) for the given key."""
        # always start looking in the last one added
        sections.sort(reverse=True)
        for s in sections:
            try:
                # try to find in section and section's templates
                return s.get(key, from_defaults=False)
            except KeyError:
                pass

        # wasn't found in sections or a section's templates so check in
        # defaults
        for s in sections:
            try:
                # try to find in section's defaultsects
                return s.get(key, from_self=False, from_templates=False)
            except KeyError:
                pass

        raise KeyError(key)

    def defaults(self):
        return self._defaults

    def default(self, key):
        """Retrieves a list of dictionaries for a default section."""
        return self.get_defaults(key)

    def add_default(self, key, template_keys=None):
        """
        Adds a default section to defaults, returning the
        default Section object.
        """
        if template_keys is None:
            template_keys = []
        return self.add_section(key, template_keys, self._defaults)

    def sections(self):
        return self._sections

    def section(self, key):
        """Retrieves a list of dictionaries for a section."""
        return self.get_sections(key)

    def get_sections(self, key, attr='_sections', searched=None):
        """
        Retrieve a list of sections that have values for the given key.
        The attr parameter can be used to control what part of the parser
        to retrieve values from.
        """
        if searched is None:
            searched = []
        if self in searched:
            return []

        sections = getattr(self, attr)
        res = sections[key] if key in sections else []
        searched.append(self)
        if self._includes:
            res.extend(list(itertools.chain(*[
                incl.get_sections(key, attr, searched)
                for incl in self._includes.itervalues()])))
        if self._parent:
            res += self._parent.get_sections(key, attr, searched)
        return res

    def get_defaults(self, key):
        """
        Retrieve a list of defaults that have values for the given key.
        """
        return self.get_sections(key, '_defaults')

    def add_section(self, key, template_keys=None, mdicts=None):
        """
        Create a new section in the configuration. The name of the
        new section is the 'key' parameter.
        """
        if template_keys is None:
            template_keys = []
        if mdicts is None:
            mdicts = self._sections
        res = Section()
        for t in template_keys:
            res.add_templates(self.get_defaults(t))
        res.add_defaults(self.get_defaults(DEFAULTSECT))
        mdicts.insert(0, key, res)
        return res

    def includes(self):
        return self._includes

    def add_include(self, filename, parser=None):
        """
        Add a new #include file to the configuration.
        """
        if filename in self._includes:
            return self._includes[filename]

        self._includes[filename] = res = \
            MultiOrderedConfigParser(self) if parser is None else parser
        return res

    def get(self, section, key):
        """Retrieves the list of values from a section for a key."""
        try:
            # search for the value in the list of sections
            return self.find_value(self.section(section), key)
        except KeyError:
            pass

        try:
            # section may be a default section so, search
            # for the value in the list of defaults
            return self.find_value(self.default(section), key)
        except KeyError:
            raise LookupError("key %r not found for section %r"
                              % (key, section))

    def multi_get(self, section, key_list):
        """
        Retrieves the list of values from a section for a list of keys.
        This method is intended to be used for equivalent keys. Thus, as soon
        as any match is found for any key in the key_list, the match is
        returned. This does not concatenate the lookups of all of the keys
        together.
        """
        for i in key_list:
            try:
                return self.get(section, i)
            except LookupError:
                pass

        # Making it here means all lookups failed.
        raise LookupError("keys %r not found for section %r" %
                          (key_list, section))

    def set(self, section, key, val):
        """Sets an option in the given section."""
        # TODO - set in multiple sections? (for now set in first)
        # TODO - set in both sections and defaults?
        if section in self._sections:
            self.section(section)[0][key] = val
        else:
            self.defaults(section)[0][key] = val

    def read(self, filename, sect=None):
        """Parse configuration information from a file"""
        try:
            with open(filename, 'rt') as config_file:
                self._read(config_file, sect)
        except IOError:
            print("Could not open file " + filename + " for reading")

    def _read(self, config_file, sect):
        """Parse configuration information from the config_file"""
        is_comment = False  # used for multi-lined comments
        for line in config_file:
            line, is_comment = remove_comment(line, is_comment)
            if not line:
                # line was empty or was a comment
                continue

            include_name = try_include(line)
            if include_name:
                for incl in sorted(glob.iglob(include_name)):
                    parser = self.add_include(incl)
                    parser.read(incl, sect)
                continue

            section, is_template, templates = try_section(line)
            if section:
                if section == DEFAULTSECT or is_template:
                    sect = self.add_default(section, templates)
                else:
                    sect = self.add_section(section, templates)
                continue

            key, val = try_option(line)
            if sect is None:
                raise Exception("Section not defined before assignment")
            sect[key] = val

    def write(self, config_file):
        """Write configuration information out to a file"""
        try:
            for key, val in self._includes.iteritems():
                val.write(key)
                config_file.write('#include "%s"\n' % key)

            config_file.write('\n')
            write_dicts(config_file, self._defaults)
            write_dicts(config_file, self._sections)
        except:
            try:
                with open(config_file, 'wt') as fp:
                    self.write(fp)
            except IOError:
                print("Could not open file " + config_file + " for writing")
