#!/usr/bin/env python
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

import sys

try:
    import pystache
except ImportError:
    print >> sys.stderr, "Pystache required. Please sudo pip install pystache."
    sys.exit(1)

import os.path

from asterisk_processor import AsteriskProcessor
from optparse import OptionParser
from swagger_model import *
from transform import Transform

TOPDIR = os.path.dirname(os.path.abspath(__file__))


def rel(file):
    """Helper to get a file relative to the script's directory

    @parm file: Relative file path.
    """
    return os.path.join(TOPDIR, file)

WIKI_PREFIX = 'Asterisk 13'

API_TRANSFORMS = [
    Transform(rel('api.wiki.mustache'),
              'doc/rest-api/%s {{name_title}} REST API.wiki' % WIKI_PREFIX),
    Transform(rel('res_ari_resource.c.mustache'),
              'res/res_ari_{{c_name}}.c'),
    Transform(rel('ari_resource.h.mustache'),
              'res/ari/resource_{{c_name}}.h'),
    Transform(rel('ari_resource.c.mustache'),
              'res/ari/resource_{{c_name}}.c', overwrite=False),
]

RESOURCES_TRANSFORMS = [
    Transform(rel('models.wiki.mustache'),
              'doc/rest-api/%s REST Data Models.wiki' % WIKI_PREFIX),
    Transform(rel('ari.make.mustache'), 'res/ari.make'),
    Transform(rel('ari_model_validators.h.mustache'),
              'res/ari/ari_model_validators.h'),
    Transform(rel('ari_model_validators.c.mustache'),
              'res/ari/ari_model_validators.c'),
]


def main(argv):
    parser = OptionParser(usage="Usage %prog [resources.json] [destdir]")

    (options, args) = parser.parse_args(argv)

    if len(args) != 3:
        parser.error("Wrong number of arguments")

    source = args[1]
    dest_dir = args[2]
    renderer = pystache.Renderer(search_dirs=[TOPDIR], missing_tags='strict')
    processor = AsteriskProcessor(wiki_prefix=WIKI_PREFIX)

    # Build the models
    base_dir = os.path.dirname(source)
    resources = ResourceListing().load_file(source, processor)
    for api in resources.apis:
        api.load_api_declaration(base_dir, processor)

    # Render the templates
    for api in resources.apis:
        for transform in API_TRANSFORMS:
            transform.render(renderer, api, dest_dir)
    for transform in RESOURCES_TRANSFORMS:
        transform.render(renderer, resources, dest_dir)

if __name__ == "__main__":
    sys.exit(main(sys.argv) or 0)
