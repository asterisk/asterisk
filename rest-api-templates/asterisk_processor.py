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

import re

from swagger_model import *


def simple_name(name):
    """Removes the {markers} from a path segement.

    @param name: Swagger path segement, with {pathVar} markers.
    """
    if name.startswith('{') and name.endswith('}'):
        return name[1:-1]
    return name


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
        'const char *': '',
        'int': 'atoi',
        'long': 'atol',
        'double': 'atof',
    }

    def process_api(self, resource_api, context):
        # Derive a resource name from the API declaration's filename
        resource_api.name = re.sub('\..*', '',
                                   os.path.basename(resource_api.path))
        # Now in all caps, from include guard
        resource_api.name_caps = resource_api.name.upper()
        # Construct the PathSegement tree for the API.
        if resource_api.api_declaration:
            resource_api.root_path = PathSegment('', None)
            for api in resource_api.api_declaration.apis:
                segment = resource_api.root_path.get_child(api.path.split('/'))
                for operation in api.operations:
                    segment.operations.append(operation)
            resource_api.api_declaration.has_events = False
            for model in resource_api.api_declaration.models:
                if model.id == "Event":
                    resource_api.api_declaration.has_events = True
                    break
            if resource_api.api_declaration.has_events:
                resource_api.api_declaration.events = \
                    [self.process_model(model, context) for model in \
                        resource_api.api_declaration.models if model.id != "Event"]
            else:
                resource_api.api_declaration.events = []

            # Since every API path should start with /[resource], root should
            # have exactly one child.
            if resource_api.root_path.num_children() != 1:
                raise SwaggerError(
                    "Should not mix resources in one API declaration", context)
            # root_path isn't needed any more
            resource_api.root_path = resource_api.root_path.children()[0]
            if resource_api.name != resource_api.root_path.name:
                raise SwaggerError(
                    "API declaration name should match", context)
            resource_api.root_full_name = resource_api.root_path.full_name

    def process_operation(self, operation, context):
        # Nicknames are camelcase, Asterisk coding is snake case
        operation.c_nickname = snakify(operation.nickname)
        operation.c_http_method = 'AST_HTTP_' + operation.http_method
        if not operation.summary.endswith("."):
            raise SwaggerError("Summary should end with .", context)

    def process_parameter(self, parameter, context):
        if not parameter.data_type in self.type_mapping:
            raise SwaggerError(
                "Invalid parameter type %s" % paramter.data_type, context)
        # Parameter names are camelcase, Asterisk convention is snake case
        parameter.c_name = snakify(parameter.name)
        parameter.c_data_type = self.type_mapping[parameter.data_type]
        parameter.c_convert = self.convert_mapping[parameter.c_data_type]
        # You shouldn't put a space between 'char *' and the variable
        if parameter.c_data_type.endswith('*'):
            parameter.c_space = ''
        else:
            parameter.c_space = ' '

    def process_model(self, model, context):
        model.c_id = snakify(model.id)
        model.channel = False
        model.channel_desc = ""
        model.bridge = False
        model.bridge_desc = ""
        model.properties = [self.process_property(model, prop, context) for prop in model.properties]
        model.properties = [prop for prop in model.properties if prop]
	model.has_properties = (len(model.properties) != 0)
        return model

    def process_property(self, model, prop, context):
        # process channel separately since it will be pulled out
        if prop.name == 'channel' and prop.type == 'Channel':
            model.channel = True
            model.channel_desc = prop.description or ""
            return None

        # process bridge separately since it will be pulled out
        if prop.name == 'bridge' and prop.type == 'Bridge':
            model.bridge = True
            model.bridge_desc = prop.description or ""
            return None

	prop.c_name = snakify(prop.name)
        if prop.type in self.type_mapping:
            prop.c_type = self.type_mapping[prop.type]
            prop.c_convert = self.convert_mapping[prop.c_type]
        else:
            prop.c_type = "Property type %s not mappable to a C type" % (prop.type)
            prop.c_convert = "Property type %s not mappable to a C conversion" % (prop.type)
            #raise SwaggerError(
            #    "Invalid property type %s" % prop.type, context)
        # You shouldn't put a space between 'char *' and the variable
        if prop.c_type.endswith('*'):
            prop.c_space = ''
        else:
            prop.c_space = ' '
        return prop
