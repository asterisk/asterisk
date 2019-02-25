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

"""Implementation of SwaggerPostProcessor which adds fields needed to generate
Asterisk RESTful HTTP binding code.
"""

import os
import re

from swagger_model import Stringify, SwaggerError, SwaggerPostProcessor

try:
    from collections import OrderedDict
except ImportError:
    from odict import OrderedDict


def simple_name(name):
    """Removes the {markers} from a path segement.

    @param name: Swagger path segement, with {pathVar} markers.
    """
    if name.startswith('{') and name.endswith('}'):
        return name[1:-1]
    return name


def wikify(str):
    """Escapes a string for the wiki.

    @param str: String to escape
    """
    # Replace all line breaks with line feeds
    str = re.sub(r'<br\s*/?>', '\n', str)
    return re.sub(r'([{}\[\]])', r'\\\1', str)


def snakify(name):
    """Helper to take a camelCase or dash-seperated name and make it
    snake_case.
    """
    r = ''
    prior_lower = False
    for c in name:
        if c.isupper() and prior_lower:
            r += "_"
        if c is '-':
            c = '_'
        prior_lower = c.islower()
        r += c.lower()
    return r


class PathSegment(Stringify):
    """Tree representation of a Swagger API declaration.
    """
    def __init__(self, name, parent):
        """Ctor.

        @param name: Name of this path segment. May have {pathVar} markers.
        @param parent: Parent PathSegment.
        """
        #: Segment name, with {pathVar} markers removed
        self.name = simple_name(name)
        #: True if segment is a {pathVar}, else None.
        self.is_wildcard = None
        #: Underscore seperated name all ancestor segments
        self.full_name = None
        #: Dictionary of child PathSegements
        self.__children = OrderedDict()
        #: List of operations on this segement
        self.operations = []

        if self.name != name:
            self.is_wildcard = True

        if not self.name:
            assert(not parent)
            self.full_name = ''
        if not parent or not parent.name:
            self.full_name = name
        else:
            self.full_name = "%s_%s" % (parent.full_name, self.name)

    def get_child(self, path):
        """Walks decendents to get path, creating it if necessary.

        @param path: List of path names.
        @return: PageSegment corresponding to path.
        """
        assert simple_name(path[0]) == self.name
        if (len(path) == 1):
            return self
        child = self.__children.get(path[1])
        if not child:
            child = PathSegment(path[1], self)
            self.__children[path[1]] = child
        return child.get_child(path[1:])

    def children(self):
        """Gets list of children.
        """
        return self.__children.values()

    def num_children(self):
        """Gets count of children.
        """
        return len(self.__children)


class AsteriskProcessor(SwaggerPostProcessor):
    """A SwaggerPostProcessor which adds fields needed to generate Asterisk
    RESTful HTTP binding code.
    """

    #: How Swagger types map to C.
    type_mapping = {
        'string': 'const char *',
        'boolean': 'int',
        'number': 'int',
        'int': 'int',
        'long': 'long',
        'double': 'double',
        'float': 'float',
    }

    #: String conversion functions for string to C type.
    convert_mapping = {
        'string': '',
        'int': 'atoi',
        'long': 'atol',
        'double': 'atof',
        'boolean': 'ast_true',
    }

    #: JSON conversion functions
    json_convert_mapping = {
        'string': 'ast_json_string_get',
        'int': 'ast_json_integer_get',
        'long': 'ast_json_integer_get',
        'double': 'ast_json_real_get',
        'boolean': 'ast_json_is_true',
    }

    def __init__(self, wiki_prefix):
        self.wiki_prefix = wiki_prefix

    def process_resource_api(self, resource_api, context):
        resource_api.wiki_prefix = self.wiki_prefix
        # Derive a resource name from the API declaration's filename
        resource_api.name = re.sub('\..*', '',
                                   os.path.basename(resource_api.path))
        # Now in all caps, for include guard
        resource_api.name_caps = resource_api.name.upper()
        resource_api.name_title = resource_api.name.capitalize()
        resource_api.c_name = snakify(resource_api.name)
        # Construct the PathSegement tree for the API.
        if resource_api.api_declaration:
            resource_api.root_path = PathSegment('', None)
            for api in resource_api.api_declaration.apis:
                segment = resource_api.root_path.get_child(api.path.split('/'))
                for operation in api.operations:
                    segment.operations.append(operation)
                api.full_name = segment.full_name

            # Since every API path should start with /[resource], root should
            # have exactly one child.
            if resource_api.root_path.num_children() != 1:
                raise SwaggerError(
                    "Should not mix resources in one API declaration", context)
            # root_path isn't needed any more
            resource_api.root_path = list(resource_api.root_path.children())[0]
            if resource_api.name != resource_api.root_path.name:
                raise SwaggerError(
                    "API declaration name should match", context)
            resource_api.root_full_name = resource_api.root_path.full_name

    def process_api(self, api, context):
        api.wiki_path = wikify(api.path)

    def process_operation(self, operation, context):
        # Nicknames are camelCase, Asterisk coding is snake case
        operation.c_nickname = snakify(operation.nickname)
        operation.c_http_method = 'AST_HTTP_' + operation.http_method
        if not operation.summary.endswith("."):
            raise SwaggerError("Summary should end with .", context)
        operation.wiki_summary = wikify(operation.summary or "")
        operation.wiki_notes = wikify(operation.notes or "")
        for error_response in operation.error_responses:
            error_response.wiki_reason = wikify(error_response.reason or "")
        operation.parse_body = (operation.body_parameter or operation.has_query_parameters) and True

    def process_parameter(self, parameter, context):
        if parameter.param_type == 'body':
            parameter.is_body_parameter = True;
            parameter.c_data_type = 'struct ast_json *'
        else:
            parameter.is_body_parameter = False;
            if not parameter.data_type in self.type_mapping:
                raise SwaggerError(
                    "Invalid parameter type %s" % parameter.data_type, context)
            # Type conversions
            parameter.c_data_type = self.type_mapping[parameter.data_type]
            parameter.c_convert = self.convert_mapping[parameter.data_type]
            parameter.json_convert = self.json_convert_mapping[parameter.data_type]

        # Parameter names are camelcase, Asterisk convention is snake case
        parameter.c_name = snakify(parameter.name)
        # You shouldn't put a space between 'char *' and the variable
        if parameter.c_data_type.endswith('*'):
            parameter.c_space = ''
        else:
            parameter.c_space = ' '
        parameter.wiki_description = wikify(parameter.description)
        if parameter.allowable_values:
            parameter.wiki_allowable_values = parameter.allowable_values.to_wiki()
        else:
            parameter.wiki_allowable_values = None

    def process_model(self, model, context):
        model.description_dox = model.description.replace('\n', '\n * ')
        model.description_dox = re.sub(' *\n', '\n', model.description_dox)
        model.wiki_description = wikify(model.description)
        model.c_id = snakify(model.id)
        return model

    def process_property(self, prop, context):
        if "-" in prop.name:
            raise SwaggerError("Property names cannot have dashes", context)
        if prop.name != prop.name.lower():
            raise SwaggerError("Property name should be all lowercase",
                               context)
        prop.wiki_description = wikify(prop.description)

    def process_type(self, swagger_type, context):
        swagger_type.c_name = snakify(swagger_type.name)
        swagger_type.c_singular_name = snakify(swagger_type.singular_name)
        swagger_type.wiki_name = wikify(swagger_type.name)
