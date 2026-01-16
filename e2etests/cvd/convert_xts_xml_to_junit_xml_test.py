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
"""Tests for convert_xts_xml_to_junit_xml.py"""

import unittest

from cvd.convert_xts_xml_to_junit_xml import *

class TestConvertXtsXmlToJunitXml(unittest.TestCase):

    def test_basic_conversion(self):
        input = """<?xml version='1.0' encoding='UTF-8' standalone='no' ?><?xml-stylesheet type="text/xsl" href="compatibility_result.xsl"?>
<Result start="1765488789733" end="1765488875726" start_display="Thu Dec 11 21:33:09 UTC 2025" end_display="Thu Dec 11 21:34:35 UTC 2025" devices="0.0.0.0:6520" host_name="jason" report_version="5.0" suite_build_number="14561884" suite_name="CTS" suite_plan="cts" suite_variant="CTS" suite_version="13_r15" command_line_args="cts --log-level-display=INFO --include-filter=CtsDeqpTestCases --module-arg=CtsDeqpTestCases:include-filter:dEQP-VK.api.smoke*">
  <Summary pass="6" failed="0" modules_done="1" modules_total="1" />
  <Module name="CtsDeqpTestCases" abi="x86_64" runtime="7355" done="true" pass="6" total_tests="6">
    <TestCase name="dEQP-VK.api.smoke">
      <Test result="pass" name="create_sampler" />
      <Test result="pass" name="create_shader" />
      <Test result="pass" name="triangle" />
      <Test result="pass" name="asm_triangle" />
      <Test result="pass" name="asm_triangle_no_opname" />
      <Test result="pass" name="unused_resolve_attachment" />
    </TestCase>
  </Module>
</Result>
        """
        actual = convert_xts_xml_to_junit_xml(input)
        expected = """<?xml version="1.0" ?>
<testsuites name="AllTests" tests="6" failures="0">
    <testsuite name="CtsDeqpTestCases" tests="6" failures="0">
        <testsuite name="dEQP-VK.api.smoke" tests="6" failures="0">
            <testcase name="dEQP-VK.api.smoke#create_sampler"/>
            <testcase name="dEQP-VK.api.smoke#create_shader"/>
            <testcase name="dEQP-VK.api.smoke#triangle"/>
            <testcase name="dEQP-VK.api.smoke#asm_triangle"/>
            <testcase name="dEQP-VK.api.smoke#asm_triangle_no_opname"/>
            <testcase name="dEQP-VK.api.smoke#unused_resolve_attachment"/>
        </testsuite>
    </testsuite>
</testsuites>
"""
        self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()