#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2013, Digium, Inc.
#
# David M. Lee, II <dlee@digium.com>
#
# See http://www.asterisk.org for more information about
# the Asterisk project. Please do not directly contact
# any of the maintainers of this project for assistance;
# the project provides a web site, mailing lists and IRC
# channels for your use.
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.
#

import filecmp
import os.path
import pystache
import shutil
import tempfile
import sys

if sys.version_info[0] == 3:
    def unicode(v):
        return str(v)


class Transform(object):
    """Transformation for template to code.
    """
    def __init__(self, template_file, dest_file_template_str, overwrite=True):
        """Ctor.

        @param template_file: Filename of the mustache template.
        @param dest_file_template_str: Destination file name. This is a
            mustache template, so each resource can write to a unique file.
        @param overwrite: If True, destination file is overwritten if it exists.
        """
        template_str = unicode(open(template_file, "r").read())
        self.template = pystache.parse(template_str)
        dest_file_template_str = unicode(dest_file_template_str)
        self.dest_file_template = pystache.parse(dest_file_template_str)
        self.overwrite = overwrite

    def render(self, renderer, model, dest_dir):
        """Render a model according to this transformation.

        @param render: Pystache renderer.
        @param model: Model object to render.
        @param dest_dir: Destination directory to write generated code.
        """
        dest_file = pystache.render(self.dest_file_template, model)
        dest_file = os.path.join(dest_dir, dest_file)
        dest_exists = os.path.exists(dest_file)
        if dest_exists and not self.overwrite:
            return
        with tempfile.NamedTemporaryFile(mode='w+') as out:
            out.write(renderer.render(self.template, model))
            out.flush()

            if not dest_exists or not filecmp.cmp(out.name, dest_file):
                print("Writing %s" % dest_file)
                shutil.copyfile(out.name, dest_file)
