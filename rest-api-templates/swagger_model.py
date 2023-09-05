
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

"""Swagger data model objects.

These objects should map directly to the Swagger api-docs, without a lot of
additional fields. In the process of translation, it should also validate the
model for consistency against the Swagger spec (i.e., fail if fields are
missing, or have incorrect values).

See https://github.com/wordnik/swagger-core/wiki/API-Declaration for the spec.
"""

from __future__ import print_function
import json
import os.path
import pprint
import re
import sys
import traceback

# We don't fully support Swagger 1.2, but we need it for subtyping
SWAGGER_VERSIONS = ["1.1", "1.2"]

SWAGGER_PRIMITIVES = [
    'void',
    'string',
    'boolean',
    'number',
    'int',
    'long',
    'double',
    'float',
    'Date',
]


class Stringify(object):
    """Simple mix-in to make the repr of the model classes more meaningful.
    """
    def __repr__(self):
        return "%s(%s)" % (self.__class__, pprint.saferepr(self.__dict__))


def compare_versions(lhs, rhs):
    '''Performs a lexicographical comparison between two version numbers.

    This properly handles simple major.minor.whatever.sure.why.not version
    numbers, but fails miserably if there's any letters in there.

    For reference:
      1.0 == 1.0
      1.0 < 1.0.1
      1.2 < 1.10

    @param lhs Left hand side of the comparison
    @param rhs Right hand side of the comparison
    @return  < 0 if lhs  < rhs
    @return == 0 if lhs == rhs
    @return  > 0 if lhs  > rhs
    '''
    lhs = [int(v) for v in lhs.split('.')]
    rhs = [int(v) for v in rhs.split('.')]
    return (lhs > rhs) - (lhs < rhs)


class ParsingContext(object):
    """Context information for parsing.

    This object is immutable. To change contexts (like adding an item to the
    stack), use the next() and next_stack() functions to build a new one.
    """

    def __init__(self, swagger_version, stack):
        self.__swagger_version = swagger_version
        self.__stack = stack

    def __repr__(self):
        return "ParsingContext(swagger_version=%s, stack=%s)" % (
            self.swagger_version, self.stack)

    def get_swagger_version(self):
        return self.__swagger_version

    def get_stack(self):
        return self.__stack

    swagger_version = property(get_swagger_version)

    stack = property(get_stack)

    def version_less_than(self, ver):
        return compare_versions(self.swagger_version, ver) < 0

    def next_stack(self, json, id_field):
        """Returns a new item pushed to the stack.

        @param json: Current JSON object.
        @param id_field: Field identifying this object.
        @return New context with additional item in the stack.
        """
        if not id_field in json:
            raise SwaggerError("Missing id_field: %s" % id_field, self)
        new_stack = self.stack + ['%s=%s' % (id_field, str(json[id_field]))]
        return ParsingContext(self.swagger_version, new_stack)

    def next(self, version=None, stack=None):
        if version is None:
            version = self.version
        if stack is None:
            stack = self.stack
        return ParsingContext(version, stack)


class SwaggerError(Exception):
    """Raised when an error is encountered mapping the JSON objects into the
    model.
    """

    def __init__(self, msg, context, cause=None):
        """Ctor.

        @param msg: String message for the error.
        @param context: ParsingContext object
        @param cause: Optional exception that caused this one.
        """
        super(Exception, self).__init__(msg, context, cause)


class SwaggerPostProcessor(object):
    """Post processing interface for model objects. This processor can add
    fields to model objects for additional information to use in the
    templates.
    """
    def process_resource_api(self, resource_api, context):
        """Post process a ResourceApi object.

        @param resource_api: ResourceApi object.
        @param context: Current context in the API.
        """
        pass

    def process_api(self, api, context):
        """Post process an Api object.

        @param api: Api object.
        @param context: Current context in the API.
        """
        pass

    def process_operation(self, operation, context):
        """Post process a Operation object.

        @param operation: Operation object.
        @param context: Current context in the API.
        """
        pass

    def process_parameter(self, parameter, context):
        """Post process a Parameter object.

        @param parameter: Parameter object.
        @param context: Current context in the API.
        """
        pass

    def process_model(self, model, context):
        """Post process a Model object.

        @param model: Model object.
        @param context: Current context in the API.
        """
        pass

    def process_property(self, property, context):
        """Post process a Property object.

        @param property: Property object.
        @param context: Current context in the API.
        """
        pass

    def process_type(self, swagger_type, context):
        """Post process a SwaggerType object.

        @param swagger_type: ResourceListing object.
        @param context: Current context in the API.
        """
        pass

    def process_resource_listing(self, resource_listing, context):
        """Post process the overall ResourceListing object.

        @param resource_listing: ResourceListing object.
        @param context: Current context in the API.
        """
        pass


class AllowableRange(Stringify):
    """Model of a allowableValues of type RANGE

    See https://github.com/wordnik/swagger-core/wiki/datatypes#complex-types
    """
    def __init__(self, min_value, max_value):
        self.min_value = min_value
        self.max_value = max_value

    def to_wiki(self):
        return "Allowed range: Min: {0}; Max: {1}".format(self.min_value, self.max_value)


class AllowableList(Stringify):
    """Model of a allowableValues of type LIST

    See https://github.com/wordnik/swagger-core/wiki/datatypes#complex-types
    """
    def __init__(self, values):
        self.values = values

    def to_wiki(self):
        return "Allowed values: {0}".format(", ".join(self.values))


def load_allowable_values(json, context):
    """Parse a JSON allowableValues object.

    This returns None, AllowableList or AllowableRange, depending on the
    valueType in the JSON. If the valueType is not recognized, a SwaggerError
    is raised.
    """
    if not json:
        return None

    if not 'valueType' in json:
        raise SwaggerError("Missing valueType field", context)

    value_type = json['valueType']

    if value_type == 'RANGE':
        if not 'min' in json and not 'max' in json:
            raise SwaggerError("Missing fields min/max", context)
        return AllowableRange(json.get('min'), json.get('max'))
    if value_type == 'LIST':
        if not 'values' in json:
            raise SwaggerError("Missing field values", context)
        return AllowableList(json['values'])
    raise SwaggerError("Unkown valueType %s" % value_type, context)


class Parameter(Stringify):
    """Model of an operation's parameter.

    See https://github.com/wordnik/swagger-core/wiki/parameters
    """

    required_fields = ['name', 'paramType', 'dataType']

    def __init__(self):
        self.param_type = None
        self.name = None
        self.description = None
        self.data_type = None
        self.required = None
        self.allowable_values = None
        self.allow_multiple = None

    def load(self, parameter_json, processor, context):
        context = context.next_stack(parameter_json, 'name')
        validate_required_fields(parameter_json, self.required_fields, context)
        self.name = parameter_json.get('name')
        self.param_type = parameter_json.get('paramType')
        self.description = parameter_json.get('description') or ''
        self.data_type = parameter_json.get('dataType')
        self.required = parameter_json.get('required') or False
        self.default_value = parameter_json.get('defaultValue')
        self.allowable_values = load_allowable_values(
            parameter_json.get('allowableValues'), context)
        self.allow_multiple = parameter_json.get('allowMultiple') or False
        processor.process_parameter(self, context)
        if parameter_json.get('allowedValues'):
            raise SwaggerError(
                "Field 'allowedValues' invalid; use 'allowableValues'",
                context)
        return self

    def is_type(self, other_type):
        return self.param_type == other_type


class ErrorResponse(Stringify):
    """Model of an error response.

    See https://github.com/wordnik/swagger-core/wiki/errors
    """

    required_fields = ['code', 'reason']

    def __init__(self):
        self.code = None
        self.reason = None

    def load(self, err_json, processor, context):
        context = context.next_stack(err_json, 'code')
        validate_required_fields(err_json, self.required_fields, context)
        self.code = err_json.get('code')
        self.reason = err_json.get('reason')
        return self


class SwaggerType(Stringify):
    """Model of a data type.
    """

    def __init__(self):
        self.name = None
        self.is_discriminator = None
        self.is_list = None
        self.singular_name = None
        self.lc_singular_name = None
        self.is_primitive = None
        self.is_binary = None

    def load(self, type_name, processor, context):
        # Some common errors
        if type_name == 'integer':
            raise SwaggerError("The type for integer should be 'int'", context)

        self.name = type_name
        type_param = get_list_parameter_type(self.name)
        self.is_list = type_param is not None
        if self.is_list:
            self.singular_name = type_param
            self.lc_singular_name = type_param.lower()
        else:
            self.singular_name = self.name
            self.lc_singular_name = self.name.lower()
        self.is_primitive = self.singular_name in SWAGGER_PRIMITIVES
        self.is_binary = (self.singular_name == 'binary')
        processor.process_type(self, context)
        return self


class Operation(Stringify):
    """Model of an operation on an API

    See https://github.com/wordnik/swagger-core/wiki/API-Declaration#apis
    """

    required_fields = ['httpMethod', 'nickname', 'responseClass', 'summary']

    def __init__(self):
        self.http_method = None
        self.nickname = None
        self.response_class = None
        self.parameters = []
        self.summary = None
        self.notes = None
        self.error_responses = []

    def load(self, op_json, processor, context):
        context = context.next_stack(op_json, 'nickname')
        validate_required_fields(op_json, self.required_fields, context)
        self.http_method = op_json.get('httpMethod')
        self.nickname = op_json.get('nickname')
        response_class = op_json.get('responseClass')
        self.response_class = response_class and SwaggerType().load(
            response_class, processor, context)

        # Specifying WebSocket URL's is our own extension
        self.is_websocket = op_json.get('upgrade') == 'websocket'
        self.is_req = not self.is_websocket

        if self.is_websocket:
            self.websocket_protocol = op_json.get('websocketProtocol')
            if self.http_method != 'GET':
                raise SwaggerError(
                    "upgrade: websocket is only valid on GET operations",
                    context)

        params_json = op_json.get('parameters') or []
        self.parameters = [
            Parameter().load(j, processor, context) for j in params_json]
        self.query_parameters = [
            p for p in self.parameters if p.is_type('query')]
        self.has_query_parameters = self.query_parameters and True
        self.path_parameters = [
            p for p in self.parameters if p.is_type('path')]
        self.has_path_parameters = self.path_parameters and True
        self.header_parameters = [
            p for p in self.parameters if p.is_type('header')]
        self.has_header_parameters = self.header_parameters and True
        self.has_parameters = self.has_query_parameters or \
            self.has_path_parameters or self.has_header_parameters
        self.is_binary_response = self.response_class.is_binary

        # Body param is different, since there's at most one
        self.body_parameter = [
            p for p in self.parameters if p.is_type('body')]
        if len(self.body_parameter) > 1:
            raise SwaggerError("Cannot have more than one body param", context)
        self.body_parameter = self.body_parameter and self.body_parameter[0]
        self.has_body_parameter = self.body_parameter and True

        self.summary = op_json.get('summary')
        self.notes = op_json.get('notes')
        err_json = op_json.get('errorResponses') or []
        self.error_responses = [
            ErrorResponse().load(j, processor, context) for j in err_json]
        self.has_error_responses = self.error_responses != []
        processor.process_operation(self, context)
        return self


class Api(Stringify):
    """Model of a single API in an API declaration.

    See https://github.com/wordnik/swagger-core/wiki/API-Declaration
    """

    required_fields = ['path', 'operations']

    def __init__(self,):
        self.path = None
        self.description = None
        self.operations = []

    def load(self, api_json, processor, context):
        context = context.next_stack(api_json, 'path')
        validate_required_fields(api_json, self.required_fields, context)
        self.path = api_json.get('path')
        self.description = api_json.get('description')
        op_json = api_json.get('operations')
        self.operations = [
            Operation().load(j, processor, context) for j in op_json]
        self.has_websocket = any(op.is_websocket for op in self.operations)
        processor.process_api(self, context)
        return self


def get_list_parameter_type(type_string):
    """Returns the type parameter if the given type_string is List[].

    @param type_string: Type string to parse
    @returns Type parameter of the list, or None if not a List.
    """
    list_match = re.match('^List\[(.*)\]$', type_string)
    return list_match and list_match.group(1)


class Property(Stringify):
    """Model of a Swagger property.

    See https://github.com/wordnik/swagger-core/wiki/datatypes
    """

    required_fields = ['type']

    def __init__(self, name):
        self.name = name
        self.type = None
        self.description = None
        self.required = None

    def load(self, property_json, processor, context):
        validate_required_fields(property_json, self.required_fields, context)
        # Bit of a hack, but properties do not self-identify
        context = context.next_stack({'name': self.name}, 'name')
        self.description = property_json.get('description') or ''
        self.required = property_json.get('required') or False

        type = property_json.get('type')
        self.type = type and SwaggerType().load(type, processor, context)

        processor.process_property(self, context)
        return self


class Model(Stringify):
    """Model of a Swagger model.

    See https://github.com/wordnik/swagger-core/wiki/datatypes
    """

    required_fields = ['description', 'properties']

    def __init__(self):
        self.id = None
        self.id_lc = None
        self.subtypes = []
        self.__subtype_types = []
        self.notes = None
        self.description = None
        self.__properties = None
        self.__discriminator = None
        self.__extends_type = None

    def load(self, id, model_json, processor, context):
        context = context.next_stack(model_json, 'id')
        validate_required_fields(model_json, self.required_fields, context)
        # The duplication of the model's id is required by the Swagger spec.
        self.id = model_json.get('id')
        self.id_lc = self.id.lower() 
        if id != self.id:
            raise SwaggerError("Model id doesn't match name", context)
        self.subtypes = model_json.get('subTypes') or []
        if self.subtypes and context.version_less_than("1.2"):
            raise SwaggerError("Type extension support added in Swagger 1.2",
                               context)
        self.description = model_json.get('description')
        props = model_json.get('properties').items() or []
        self.__properties = [
            Property(k).load(j, processor, context) for (k, j) in props]
        self.__properties = sorted(self.__properties, key=lambda p: p.name)

        discriminator = model_json.get('discriminator')

        if discriminator:
            if context.version_less_than("1.2"):
                raise SwaggerError("Discriminator support added in Swagger 1.2",
                                   context)

            discr_props = [p for p in self.__properties if p.name == discriminator]
            if not discr_props:
                raise SwaggerError(
                    "Discriminator '%s' does not name a property of '%s'" % (
                        discriminator, self.id),
                    context)

            self.__discriminator = discr_props[0]

        self.model_json = json.dumps(model_json,
                                     indent=2, separators=(',', ': '))

        processor.process_model(self, context)
        return self

    def extends(self):
        return self.__extends_type and self.__extends_type.id

    def extends_lc(self):
        return self.__extends_type and self.__extends_type.id_lc

    def set_extends_type(self, extends_type):
        self.__extends_type = extends_type

    def set_subtype_types(self, subtype_types):
        self.__subtype_types = subtype_types

    def discriminator(self):
        """Returns the discriminator, digging through base types if needed.
        """
        return self.__discriminator or \
            self.__extends_type and self.__extends_type.discriminator()

    def properties(self):
        base_props = []
        if self.__extends_type:
            base_props = self.__extends_type.properties()
        return base_props + self.__properties

    def has_properties(self):
        return len(self.properties()) > 0

    def all_subtypes(self):
        """Returns the full list of all subtypes, including sub-subtypes.
        """
        res = self.__subtype_types + \
              [subsubtypes for subtype in self.__subtype_types
               for subsubtypes in subtype.all_subtypes()]
        return sorted(res, key=lambda m: m.id)

    def has_subtypes(self):
        """Returns True if type has any subtypes.
        """
        return len(self.subtypes) > 0


class ApiDeclaration(Stringify):
    """Model class for an API Declaration.

    See https://github.com/wordnik/swagger-core/wiki/API-Declaration
    """

    required_fields = [
        'swaggerVersion', '_author', '_copyright', 'apiVersion', 'basePath',
        'resourcePath', 'apis', 'models'
    ]

    def __init__(self):
        self.swagger_version = None
        self.author = None
        self.copyright = None
        self.api_version = None
        self.base_path = None
        self.resource_path = None
        self.apis = []
        self.models = []

    def load_file(self, api_declaration_file, processor):
        context = ParsingContext(None, [api_declaration_file])
        try:
            return self.__load_file(api_declaration_file, processor, context)
        except SwaggerError:
            raise
        except Exception as e:
            print("Error: ", traceback.format_exc(), file=sys.stderr)
            raise SwaggerError(
                "Error loading %s" % api_declaration_file, context, e)

    def __load_file(self, api_declaration_file, processor, context):
        with open(api_declaration_file) as fp:
            self.load(json.load(fp), processor, context)

        expected_resource_path = '/api-docs/' + \
            os.path.basename(api_declaration_file) \
            .replace(".json", ".{format}")

        if self.resource_path != expected_resource_path:
            print("%s != %s" % (self.resource_path, expected_resource_path),
                file=sys.stderr)
            raise SwaggerError("resourcePath has incorrect value", context)

        return self

    def load(self, api_decl_json, processor, context):
        """Loads a resource from a single Swagger resource.json file.
        """
        # If the version doesn't match, all bets are off.
        self.swagger_version = api_decl_json.get('swaggerVersion')
        context = context.next(version=self.swagger_version)
        if not self.swagger_version in SWAGGER_VERSIONS:
            raise SwaggerError(
                "Unsupported Swagger version %s" % self.swagger_version, context)

        validate_required_fields(api_decl_json, self.required_fields, context)

        self.author = api_decl_json.get('_author')
        self.copyright = api_decl_json.get('_copyright')
        self.api_version = api_decl_json.get('apiVersion')
        self.base_path = api_decl_json.get('basePath')
        self.resource_path = api_decl_json.get('resourcePath')
        self.requires_modules = api_decl_json.get('requiresModules') or []
        api_json = api_decl_json.get('apis') or []
        self.apis = [
            Api().load(j, processor, context) for j in api_json]
        paths = set()
        for api in self.apis:
            if api.path in paths:
                raise SwaggerError("API with duplicated path: %s" % api.path, context)
            paths.add(api.path)
        self.has_websocket = any(api.has_websocket for api in self.apis)
        models = api_decl_json.get('models').items() or []
        self.models = [Model().load(id, json, processor, context)
                       for (id, json) in models]
        self.models = sorted(self.models, key=lambda m: m.id)
        # Now link all base/extended types
        model_dict = dict((m.id, m) for m in self.models)
        for m in self.models:
            def link_subtype(name):
                res = model_dict.get(name)
                if not res:
                    raise SwaggerError("%s has non-existing subtype %s",
                                       m.id, name)
                res.set_extends_type(m)
                return res;
            if m.subtypes:
                m.set_subtype_types([
                    link_subtype(subtype) for subtype in m.subtypes])
        return self


class ResourceApi(Stringify):
    """Model of an API listing in the resources.json file.
    """

    required_fields = ['path', 'description']

    def __init__(self):
        self.path = None
        self.description = None
        self.api_declaration = None

    def load(self, api_json, processor, context):
        context = context.next_stack(api_json, 'path')
        validate_required_fields(api_json, self.required_fields, context)
        self.path = api_json['path'].replace('{format}', 'json')
        self.description = api_json['description']

        if not self.path or self.path[0] != '/':
            raise SwaggerError("Path must start with /", context)
        processor.process_resource_api(self, context)
        return self

    def load_api_declaration(self, base_dir, processor):
        self.file = (base_dir + self.path)
        self.api_declaration = ApiDeclaration().load_file(self.file, processor)
        processor.process_resource_api(self, [self.file])


class ResourceListing(Stringify):
    """Model of Swagger's resources.json file.
    """

    required_fields = ['apiVersion', 'basePath', 'apis']

    def __init__(self):
        self.swagger_version = None
        self.api_version = None
        self.base_path = None
        self.apis = None

    def load_file(self, resource_file, processor):
        context = ParsingContext(None, [resource_file])
        try:
            return self.__load_file(resource_file, processor, context)
        except SwaggerError:
            raise
        except Exception as e:
            print("Error: ", traceback.format_exc(), file=sys.stderr)
            raise SwaggerError(
                "Error loading %s" % resource_file, context, e)

    def __load_file(self, resource_file, processor, context):
        with open(resource_file) as fp:
            return self.load(json.load(fp), processor, context)

    def load(self, resources_json, processor, context):
        # If the version doesn't match, all bets are off.
        self.swagger_version = resources_json.get('swaggerVersion')
        if not self.swagger_version in SWAGGER_VERSIONS:
            raise SwaggerError(
                "Unsupported Swagger version %s" % self.swagger_version, context)

        validate_required_fields(resources_json, self.required_fields, context)
        self.api_version = resources_json['apiVersion']
        self.base_path = resources_json['basePath']
        apis_json = resources_json['apis']
        self.apis = [
            ResourceApi().load(j, processor, context) for j in apis_json]
        processor.process_resource_listing(self, context)
        return self


def validate_required_fields(json, required_fields, context):
    """Checks a JSON object for a set of required fields.

    If any required field is missing, a SwaggerError is raised.

    @param json: JSON object to check.
    @param required_fields: List of required fields.
    @param context: Current context in the API.
    """
    missing_fields = [f for f in required_fields if not f in json]

    if missing_fields:
        raise SwaggerError(
            "Missing fields: %s" % ', '.join(missing_fields), context)
