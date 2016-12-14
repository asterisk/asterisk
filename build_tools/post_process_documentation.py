#! /usr/bin/env python
# vin: sw=3 et:
'''
Copyright (C) 2012, Digium, Inc.
Matt Jordan <mjordan@digium.com>

This program is free software, distributed under the terms of
the GNU General Public License Version 2.
'''

import sys
import optparse

from xml.dom.minidom import parse


def merge_parameter_information(managerEvent):
    ''' Merge the parameter information across all managerEventInstances
    within a managerEvent node '''

    def __swap_parameter_documentation(one, two):
        # See who has the better documentation and use it
        if (one.hasChildNodes() and not two.hasChildNodes()):
            two.parentNode.replaceChild(one.cloneNode(True), two)
        elif (two.hasChildNodes() and not one.hasChildNodes()):
            one.parentNode.replaceChild(two.cloneNode(True), one)

    def __merge_parameter(param, other_instances):
        # Compare the parameter to every other instance's set of parameters
        for other in other_instances:
            other_parameters = other.getElementsByTagName("parameter")
            match = [p for p in other_parameters
                     if p.getAttribute('name') == param.getAttribute('name')]
            if (match):
                # See who has the better documentation and use it
                __swap_parameter_documentation(param, match[0])

    instances = managerEvent.getElementsByTagName("managerEventInstance")
    merged = []
    for instance in instances:
        others = [i for i in instances if i != instance]
        parameters = instance.getElementsByTagName("parameter")
        for parameter in parameters:
            if parameter not in merged:
                merged.append(parameter)
                __merge_parameter(parameter, others)


def collapse_event_pair(managerEventOne, managerEventTwo):
    # Move all children of managerEventTwo to managerEventOne
    for node in managerEventTwo.childNodes:
        managerEventOne.appendChild(node.cloneNode(True))

    return managerEventOne


def collapse_manager_events(rootNode, managerEvents):
    events = {}
    for managerEvent in managerEvents:
        if (managerEvent.parentNode.nodeName == 'list-elements'
            or managerEvent.parentNode.nodeName == 'responses'):
            continue
        managerEvent.parentNode.removeChild(managerEvent)
        attr = managerEvent.getAttribute('name')
        if attr in events:
            # match, collapse the two managerEvents
            events[attr] = collapse_event_pair(events[attr], managerEvent)
        else:
            events[attr] = managerEvent

    # Combine parameter information and re-add the manager Events
    for k, event in events.items():
        merge_parameter_information(event)
        rootNode.appendChild(event)
    return


def main(argv=None):

    if argv is None:
        argv = sys.argv

    parser = optparse.OptionParser()
    parser.add_option('-i', '--input', dest='input_file',
                      default='doc/core-full-en_US.xml',
                      help='The XML file to process')
    parser.add_option('-o', '--output', dest='output_file',
                      default='doc/core-en_US.xml',
                      help='The XML file to create')
    (options, args) = parser.parse_args(argv)

    dom = parse(options.input_file)

    datasource = open(options.output_file, 'w')
    docs = dom.getElementsByTagName("docs")[0]
    managerEvents = dom.getElementsByTagName("managerEvent")
    if (managerEvents):
        collapse_manager_events(docs, managerEvents)

    dom.writexml(datasource)
    datasource.close()

    return 0

if __name__ == "__main__":
    sys.exit(main() or 0)
