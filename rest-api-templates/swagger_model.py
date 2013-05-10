
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

import json
import os.path
import pprint
import sys
import traceback

try:
    from collections import OrderedDict
except ImportError:
    from odict import OrderedDict


SWAGGER_VERSION = "1.1"


class SwaggerError(Exception):
    """Raised when an error is encountered mapping the JSON objects into the
    model.
    """

    def __init__(self, msg, context, cause=None):
        """Ctor.

        @param msg: String message for the error.
        @param context: Array of strings for current context in the API.
        @param cause: Optional exception that caused this one.
        """
        super(Exception, self).__init__(msg, context, cause)


class SwaggerPostProcessor(object):
    """Post processing interface for model objects. This processor can add
    fields to model objects for additional information to use in the
    templates.
    """
    def process_api(self, resource_api, context):
        """Post process a ResourceApi object.

        @param resource_api: ResourceApi object.
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


class Stringify(object):
    """Simple mix-in to make the repr of the model classes more meaningful.
    """
    def __repr__(self):
        return "%s(%s)" % (self.__class__, pprint.saferepr(self.__dict__))


class AllowableRange(Stringify):
    """Model of a allowableValues of type RANGE

    See https://github.com/wordnik/swagger-core/wiki/datatypes#complex-types
    """
    def __init__(self, min_value, max_value):
        self.min_value = min_value
        self.max_value = max_value


class AllowableList(Stringify):
    """Model of a allowableValues of type LIST

    See https://github.com/wordnik/swagger-core/wiki/datatypes#complex-types
    """
    def __init__(self, values):
        self.values = values


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
        if not 'min' in json:
            raise SwaggerError("Missing field min", context)
        if not 'max' in json:
            raise SwaggerError("Missing field max", context)
        return AllowableRange(json['min'], json['max'])
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
        context = add_context(context, parameter_json, 'name')
        validate_required_fields(parameter_json, self.required_fields, context)
        self.name = parameter_json.get('name')
        self.param_type = parameter_json.get('paramType')
        self.description = parameter_json.get('description') or ''
        self.data_type = parameter_json.get('dataType')
        self.required = parameter_json.get('required') or False
        self.allowable_values = load_allowable_values(
            parameter_json.get('allowableValues'), context)
        self.allow_multiple = parameter_json.get('allowMultiple') or False
        processor.process_parameter(self, context)
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
        context = add_context(context, err_json, 'code')
        validate_required_fields(err_json, self.required_fields, context)
        self.code = err_json.get('code')
        self.reason = err_json.get('reason')
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
        context = add_context(context, op_json, 'nickname')
        validate_required_fields(op_json, self.required_fields, context)
        self.http_method = op_json.get('httpMethod')
        self.nickname = op_json.get('nickname')
        self.response_class = op_json.get('responseClass')
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
        self.summary = op_json.get('summary')
        self.notes = op_json.get('notes')
        err_json = op_json.get('errorResponses') or []
        self.error_responses = [
            ErrorResponse().load(j, processor, context) for j in err_json]
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
        context = add_context(context, api_json, 'path')
        validate_required_fields(api_json, self.required_fields, context)
        self.path = api_json.get('path')
        self.description = api_json.get('description')
        op_json = api_json.get('operations')
        self.operations = [
            Operation().load(j, processor, context) for j in op_json]
        return self


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
        self.type = property_json.get('type')
        self.description = property_json.get('description') or ''
        self.required = property_json.get('required') or False
        return self


class Model(Stringify):
    """Model of a Swagger model.

    See https://github.com/wordnik/swagger-core/wiki/datatypes
    """

    def __init__(self):
        self.id = None
        self.notes = None
        self.description = None
        self.properties = None

    def load(self, id, model_json, processor, context):
        context = add_context(context, model_json, 'id')
        # This arrangement is required by the Swagger API spec
        self.id = model_json.get('id')
        if id != self.id:
            raise SwaggerError("Model id doesn't match name", c)
        self.description = model_json.get('description')
        props = model_json.get('properties').items() or []
        self.properties = [
            Property(k).load(j, processor, context) for (k, j) in props]
        return self


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

    def load_file(self, api_declaration_file, processor, context=[]):
        context = context + [api_declaration_file]
        try:
            return self.__load_file(api_declaration_file, processor, context)
        except SwaggerError:
            raise
        except Exception as e:
            print >> sys.stderr, "Error: ", traceback.format_exc()
            raise SwaggerError(
                "Error loading %s" % api_declaration_file, context, e)

    def __load_file(self, api_declaration_file, processor, context):
        with open(api_declaration_file) as fp:
            self.load(json.load(fp), processor, context)

        expected_resource_path = '/api-docs/' + \
            os.path.basename(api_declaration_file) \
            .replace(".json", ".{format}")

        if self.resource_path != expected_resource_path:
            print "%s != %s" % (self.resource_path, expected_resource_path)
            raise SwaggerError("resourcePath has incorrect value", context)

        return self

    def load(self, api_decl_json, processor, context):
        """Loads a resource from a single Swagger resource.json file.
        """
        # If the version doesn't match, all bets are off.
        self.swagger_version = api_decl_json.get('swaggerVersion')
        if self.swagger_version != SWAGGER_VERSION:
            raise SwaggerError(
                "Unsupported Swagger version %s" % swagger_version, context)

        validate_required_fields(api_decl_json, self.required_fields, context)

        self.author = api_decl_json.get('_author')
        self.copyright = api_decl_json.get('_copyright')
        self.api_version = api_decl_json.get('apiVersion')
        self.base_path = api_decl_json.get('basePath')
        self.resource_path = api_decl_json.get('resourcePath')
        api_json = api_decl_json.get('apis') or []
        self.apis = [
            Api().load(j, processor, context) for j in api_json]
        models = api_decl_json.get('models').items() or []
        self.models = [
            Model().load(k, j, processor, context) for (k, j) in models]

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
        context = add_context(context, api_json, 'path')
        validate_required_fields(api_json, self.required_fields, context)
        self.path = api_json['path']
        self.description = api_json['description']

        if not self.path or self.path[0] != '/':
            raise SwaggerError("Path must start with /", context)
        processor.process_api(self, context)
        return self

    def load_api_declaration(self, base_dir, processor):
        self.file = (base_dir + self.path).replace('{format}', 'json')
        self.api_declaration = ApiDeclaration().load_file(self.file, processor)
        processor.process_api(self, [self.file])


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
        context = [resource_file]
        try:
            return self.__load_file(resource_file, processor, context)
        except SwaggerError:
            raise
        except Exception as e:
            print >> sys.stderr, "Error: ", traceback.format_exc()
            raise SwaggerError(
                "Error loading %s" % resource_file, context, e)

    def __load_file(self, resource_file, processor, context):
        with open(resource_file) as fp:
            return self.load(json.load(fp), processor, context)

    def load(self, resources_json, processor, context):
        # If the version doesn't match, all bets are off.
        self.swagger_version = resources_json.get('swaggerVersion')
        if self.swagger_version != SWAGGER_VERSION:
            raise SwaggerError(
                "Unsupported Swagger version %s" % swagger_version, context)

        validate_required_fields(resources_json, self.required_fields, context)
        self.api_version = resources_json['apiVersion']
        self.base_path = resources_json['basePath']
        apis_json = resources_json['apis']
        self.apis = [
            ResourceApi().load(j, processor, context) for j in apis_json]
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


def add_context(context, json, id_field):
    """Returns a new context with a new item added to it.

    @param context: Old context.
    @param json: Current JSON object.
    @param id_field: Field identifying this object.
    @return New context with additional item.
    """
    if not id_field in json:
        raise SwaggerError("Missing id_field: %s" % id_field, context)
    return context + ['%s=%s' % (id_field, str(json[id_field]))]
