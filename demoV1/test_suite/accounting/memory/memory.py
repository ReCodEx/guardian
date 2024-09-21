#!/usr/bin/python3


import os
import sys
import xml.etree.ElementTree as ET
from .. import basic_utils

sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import matplotlib.pyplot as plt
from scipy import stats
import numpy as np

def status():
    return 0

def cg_memory_peak(results_xml):
    return 0

def rusage_rss(results_xml):
    return 0

def measure_allocation(exec, alloc, results_xml, config_xml = None):
    
    return (cg_memory_peak(results_xml), rusage_rss(results_xml))

cntnr_path = "../src/build/rcdx_cntnr_demo"
tree = ET.parse('country_data.xml')
root = tree.getroot()