#! /usr/bin/env python
# vin: sw=3 et:
'''
Copyright (C) 2012, Digium, Inc.
Matt Jordan <mjordan@digium.com>

This program is free software, distributed under the terms of
the GNU General Public License Version 2.
'''

import sys
import xml.dom.minidom


def get_manager_event_method_type(candidate_string):
    if "ast_manager_event_multichan" in candidate_string:
        return "multichan"
    elif "ast_manager_event" in candidate_string:
        return "ast_manager_event"
    elif "manager_event" in candidate_string:
        return "manager_event"
    return ""


def parse_manager_event_instance(xml_fragment):
    ''' Parse the information for a manager event

    Keyword Arguments:
    xml_fragment    The XML fragment comment

    Returns:
    A well-formed XML fragment containing the comments passed in, as well as
    information obtained from the manager_event macro calls
    '''

    def __node_contains_parameter(node, parameter):
        ''' Return whether or not a node contains a given parameter name '''
        return any([n for n in node.getElementsByTagName("parameter")
                    if __node_contains_attribute(n, parameter)])

    def __node_contains_attribute(node, attribute_name):
        ''' Return whether or not a node contains a given attribute name '''
        return any([attr for attr in node.attributes.items()
                    if attr[1] == attribute_name])

    candidate_lines = []
    type = ""

    # Read the manager_event method call, which should occur after
    # the documentation block
    for line in sys.stdin:
        if len(line):
            candidate_lines.append(line)
        if ");" in line:
            break

    candidate_string = ''.join(candidate_lines)
    type = get_manager_event_method_type(candidate_string)
    if not type:
        # Unknown, return what we have
        return ''.join(xml_fragment)

    # strip off the macro name
    first_paren = candidate_string.index("(", 0)
    last_paren = candidate_string.rindex(");")
    candidate_string = candidate_string[first_paren + 1:last_paren]

    # split into parameter tokens
    func_parameter_tokens = candidate_string.split(',')

    if type == "manager_event" or type == "multichan":
        class_level = func_parameter_tokens[0].strip()
        event_type = func_parameter_tokens[1].strip()
    else:
        class_level = func_parameter_tokens[1].strip()
        event_type = func_parameter_tokens[2].strip()

    if type == "manager_event":
        event_parameters = func_parameter_tokens[2].strip()
    elif type == "ast_manager_event":
        event_parameters = func_parameter_tokens[3].strip()
    else:
        event_parameters = func_parameter_tokens[4].strip()

    parameter_tokens = event_parameters.replace("\"", "").split('\\r\\n')

    # Build the top level XML element information.  Note that we temporarily
    # add the xi namespace in case any includes are used
    node_text = '<managerEvent language=\"%s\" name=\"%s\" xmlns:xi=\"%s\">'
    xml_fragment.insert(0, node_text % ('en_US',
                                        event_type.strip().replace("\"", ""),
                                        'http://www.w3.org/2001/XInclude'))
    xml_fragment[1] = "<managerEventInstance class=\"%s\">" % (class_level)
    xml_fragment.insert(len(xml_fragment), "</managerEvent>")

    # Turn the XML into a DOM to manage the rest of the node manipulations
    dom = xml.dom.minidom.parseString(''.join(xml_fragment))

    # Get the syntax node if we have one; otherwise make one
    instance = dom.getElementsByTagName("managerEventInstance")[0]
    syntax = instance.getElementsByTagName("syntax")
    if not syntax:
        syntax = dom.createElement("syntax")
        instance.appendChild(syntax)
        # Move any existing parameter nodes over
        for node in instance.getElementsByTagName("parameter"):
            syntax.appendChild(node.cloneNode(True))
            instance.removeChild(node)
    else:
        syntax = syntax[0]

    # Add parameters found in the method invocation that were not previously
    # documented
    for parameter in parameter_tokens:
        if not len(parameter):
            continue
        index = parameter.find(':')
        if index < 0:
            index = len(parameter)
        parameter = (parameter[:index].strip().replace("\"", ""))
        if ('%s' not in parameter and
            not __node_contains_parameter(syntax, parameter)):
            e = dom.createElement("parameter")
            e.setAttribute('name', parameter)
            syntax.appendChild(e)

    return dom.toxml().replace("<?xml version=\"1.0\" ?>", "").replace(
               'xmlns:xi="http://www.w3.org/2001/XInclude"', '')


def main(argv=None):

    if argv is None:
        argv = sys.argv

    in_doc = False
    xml_fragment = []
    xml = []
    line_number = 0

    for line in sys.stdin:
        # Note: multiple places may have to read a line, so iterating over
        # readlines isn't possible.  Break when a null line is returned
        line_number += 1
        if not line:
            break

        line = line.strip()
        if ("/*** DOCUMENTATION" in line):
            in_doc = True
        elif ("***/" in line and in_doc):
            # Depending on what we're processing, determine if we need to do
            # any additional work
            in_doc = False
            if not xml_fragment:
                # Nothing read, move along
                continue

            if "<managerEventInstance>" in xml_fragment[0]:
                xml.append(parse_manager_event_instance(xml_fragment))
            else:
                xml.append(''.join(xml_fragment))

            xml_fragment = []
        elif (in_doc):
            xml_fragment.append("%s\n" % line)

    sys.stdout.write(''.join(xml))
    return 0

if __name__ == "__main__":
    sys.exit(main() or 0)
