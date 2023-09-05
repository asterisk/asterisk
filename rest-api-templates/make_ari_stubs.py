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

from __future__ import print_function
import sys

try:
    import pystache
except ImportError:
    print("Pystache required. Please sudo pip install pystache.", file=sys.stderr)
    sys.exit(1)

import os.path

from asterisk_processor import AsteriskProcessor
from argparse import ArgumentParser as ArgParser
from swagger_model import ResourceListing
from transform import Transform

TOPDIR = os.path.dirname(os.path.abspath(__file__))


def rel(file):
    """Helper to get a file relative to the script's directory

    @parm file: Relative file path.
    """
    return os.path.join(TOPDIR, file)

def main(argv):
    description = (
        'Command line utility to export ARI documentation to markdown'
    )

    parser = ArgParser(description=description)
    parser.add_argument('--resources', type=str, default="rest-api/resources.json",
                        help="resources.json file to process", required=False)
    parser.add_argument('--source-dir', type=str, default=".",
                        help="Asterisk source directory", required=False)
    parser.add_argument('--dest-dir', type=str, default="doc/rest-api",
                        help="Destination directory", required=False)
    parser.add_argument('--docs-prefix', type=str, default="../",
                        help="Prefix to apply to links", required=False)

    args = parser.parse_args()
    if not args:
        return

    renderer = pystache.Renderer(search_dirs=[TOPDIR], missing_tags='strict')
    processor = AsteriskProcessor(wiki_prefix=args.docs_prefix)

    API_TRANSFORMS = [
        Transform(rel('api.wiki.mustache'),
                  '%s/{{name_title}}_REST_API.md' % args.dest_dir),
        Transform(rel('res_ari_resource.c.mustache'),
                  'res/res_ari_{{c_name}}.c'),
        Transform(rel('ari_resource.h.mustache'),
                  'res/ari/resource_{{c_name}}.h'),
        Transform(rel('ari_resource.c.mustache'),
                  'res/ari/resource_{{c_name}}.c', overwrite=False),
    ]

    RESOURCES_TRANSFORMS = [
        Transform(rel('models.wiki.mustache'),
                  '%s/Asterisk_REST_Data_Models.md' % args.dest_dir),
        Transform(rel('ari.make.mustache'), 'res/ari.make'),
        Transform(rel('ari_model_validators.h.mustache'),
                  'res/ari/ari_model_validators.h'),
        Transform(rel('ari_model_validators.c.mustache'),
                  'res/ari/ari_model_validators.c'),
    ]

    # Build the models
    base_dir = os.path.dirname(args.resources)
    resources = ResourceListing().load_file(args.resources, processor)
    for api in resources.apis:
        api.load_api_declaration(base_dir, processor)

    # Render the templates
    for api in resources.apis:
        for transform in API_TRANSFORMS:
            transform.render(renderer, api, args.source_dir)
    for transform in RESOURCES_TRANSFORMS:
        transform.render(renderer, resources, args.source_dir)

if __name__ == "__main__":
    sys.exit(main(sys.argv) or 0)
