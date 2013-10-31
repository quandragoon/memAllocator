#!/usr/bin/env python
#
from opentuner import ConfigurationManipulator
from opentuner.search.manipulator import PowerOfTwoParameter

mdriver_manipulator = ConfigurationManipulator()

"""
See opentuner/search/manipulator.py for more parameter types,
like IntegerParameter, EnumParameter, etc.

TODO(project3): Implement the parameters of your allocator. Once
you have at least one other parameters, feel free to remove ALIGNMENT.
"""
mdriver_manipulator.add_parameter(PowerOfTwoParameter('ALIGNMENT', 8, 8))
mdriver_manipulator.add_parameter(PowerOfTwoParameter('MIN_SIZE', 1, 1 << 17))
mdriver_manipulator.add_parameter(PowerOfTwoParameter('MAX_DIFF', 1, 1 << 18))
mdriver_manipulator.add_parameter(PowerOfTwoParameter('MIN_DIFF', 1, 1 << 17))
