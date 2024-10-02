

import sys
import xml.etree.ElementTree as ETree

sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import re

def results_tree(results_xml):
    with open(results_xml) as f:
        xml = f.read()
        tree = ETree.fromstring(re.sub(r"(<\?xml[^>]+\?>)", r"\1<root>", xml) + "</root>")
    return tree