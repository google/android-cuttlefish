#!/usr/bin/python3
#
# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0(the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Converts Android CTS test_result.xml files to Bazel result xml files.

JUnit XML structure: https://github.com/testmoapp/junitxml

"""

import argparse
import os
import re
import sys

from xml.etree import ElementTree
import xml.dom.minidom

def to_pretty_xml(root, indent="    "):
    return xml.dom.minidom.parseString(ElementTree.tostring(root, encoding='UTF-8', xml_declaration=True)).toprettyxml(indent)

def convert_xts_xml_to_junit_xml(input_xml_string):
    input_xml_root = ElementTree.ElementTree(ElementTree.fromstring(input_xml_string)).getroot()

    output_xml_testsuites = ElementTree.Element("testsuites")
    output_xml_testsuites.set('name', 'AllTests')

    output_xml_testsuites_fail = 0
    output_xml_testsuites_total = 0

    for input_xml_result in input_xml_root.iter('Result'):
        for input_xml_module in input_xml_result.iter('Module'):
            # e.g. CtsDeqpTestCases
            input_module_name = input_xml_module.get('name')

            output_xml_module_testsuite = ElementTree.SubElement(output_xml_testsuites, "testsuite")
            output_xml_module_testsuite.set('name', input_module_name)

            output_xml_module_testsuite_fail = 0
            output_xml_module_testsuite_total = 0

            for input_xml_testcase in input_xml_module.iter('TestCase'):
                # e.g. dEQP-EGL.functional.image.create
                input_testcase_name = input_xml_testcase.get('name')

                output_xml_testcase_testsuite = ElementTree.SubElement(output_xml_module_testsuite, "testsuite")
                output_xml_testcase_testsuite.set('name', input_testcase_name)

                output_xml_testcase_testsuite_fail = 0
                output_xml_testcase_testsuite_total = 0

                for input_xml_test in input_xml_testcase.iter('Test'):
                    # e.g. gles2_android_native_rgba4_texture
                    input_test_name = input_xml_test.get('name')

                    # e.g. dEQP-EGL.functional.image.create#gles2_android_native_rgba4_texture
                    output_test_name = input_testcase_name + '#' + input_test_name

                    output_xml_testcase = ElementTree.SubElement(output_xml_testcase_testsuite, "testcase")
                    output_xml_testcase.set('name', output_test_name)

                    input_test_result = input_xml_test.get('result')
                    if input_test_result == 'failed':
                        output_xml_testcase_failure = ElementTree.SubElement(output_xml_testcase, "failure")
                        output_xml_testcase_failure.set('name', output_test_name)

                        output_xml_testcase_testsuite_fail += 1

                    output_xml_testcase_testsuite_total += 1

                output_xml_testcase_testsuite.set('tests', str(output_xml_testcase_testsuite_total))
                output_xml_testcase_testsuite.set('failures', str(output_xml_testcase_testsuite_fail))

                output_xml_module_testsuite_fail += output_xml_testcase_testsuite_fail
                output_xml_module_testsuite_total += output_xml_testcase_testsuite_total

            output_xml_module_testsuite.set('tests', str(output_xml_module_testsuite_total))
            output_xml_module_testsuite.set('failures', str(output_xml_module_testsuite_fail))

            output_xml_testsuites_fail += output_xml_module_testsuite_fail
            output_xml_testsuites_total += output_xml_testcase_testsuite_total

    output_xml_testsuites.set('tests', str(output_xml_module_testsuite_total))
    output_xml_testsuites.set('failures', str(output_xml_module_testsuite_fail))

    output_xml_string = to_pretty_xml(output_xml_testsuites)

    return output_xml_string

def main():
    parser = argparse.ArgumentParser(description="Converts Android CTS test_result.xml files to Bazel result xml files")
    parser.add_argument("--input_xml_file", help="The path to input Android CTS test_result.xml file.")
    parser.add_argument("--output_xml_file", help="The path for the output xml file that is digestable by bazel.")
    args = parser.parse_args()

    input_xml_string = None
    with open(args.input_xml_file, 'r') as intput_xml_file:
        input_xml_string = intput_xml_file.read()

    output_xml_string = convert_xts_xml_to_junit_xml(input_xml_string)

    with open(args.output_xml_file, 'w') as output_xml_file:
        output_xml_file.write(output_xml_string)

if __name__ == '__main__':
    main()
