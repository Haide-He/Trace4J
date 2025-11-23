#!/home/hdhe/anaconda3/envs/test/bin/python

import os, sys
from _tgen import TraceGenerator
from pylib import *
import multiprocessing as mp
from concurrent.futures import ThreadPoolExecutor, as_completed
#from functools import partial

##global variables
isDataCentric = False
isNuma = False
isGeneric = False
isHeap = False

class Trace:
	def __init__(self, start, end, metrics_dict, callpath):
		self.start = start
		self.end = end
		self.metrics_dict = metrics_dict
		self.callpath = callpath

def get_all_files(directory):
	files = [f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory,f))]
	ret_dict = dict()
	for f in files:
		if f.startswith("agent-trace-") and f.find(".run") >= 0:
			start_index = len("agent-trace-")
			end_index = f.find(".run")
			tid = f[start_index:end_index]
			if tid not in ret_dict:
				ret_dict[tid] = []
			ret_dict[tid].append(os.path.join(directory,f))
	return ret_dict

def parse_input_file(file_path, level_one_node_tag):
	print ("parsing", file_path, level_one_node_tag)
	with open(file_path) as f:
		contents = f.read()
		#print contents
	parser = special_xml.HomoXMLParser(level_one_node_tag, contents)
	return parser.getVirtualRoot()

def remove_all_files(directory):
	files = [f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory,f))]
	for f in files:
		if f.startswith("agent-trace-") and f.find(".run") >= 0:
			os.remove(f)
		elif f.startswith("agent-statistics") and f.find(".run"):
			os.remove(f)

def load_method(method_root):
	method_manager = code_cache.MethodManager()
	for m_xml in method_root.getChildren():
		m = code_cache.Method(m_xml.getAttr("id"),m_xml.getAttr("version"))
		## set fields
		m.file = m_xml.getAttr("file")
		m.start_addr = m_xml.getAttr("start_addr")
		m.code_size = m_xml.getAttr("code_size")
		m.method_name = m_xml.getAttr("name")
		m.class_name = m_xml.getAttr("class")

		## add children; currently addr2line mapping and bci2line mapping
		addr2line_xml = None
		bci2line_xml = None
		for c_xml in m_xml.getChildren():
			if c_xml.name() == "addr2line":
				assert(not addr2line_xml)
				addr2line_xml = c_xml
			elif c_xml.name() == "bci2line":
				assert(not bci2line_xml)
				bci2line_xml = c_xml
		if addr2line_xml:
			for range_xml in addr2line_xml.getChildren():
				assert(range_xml.name() == "range")
				start = range_xml.getAttr("start")
				end = range_xml.getAttr("end")
				lineno = range_xml.getAttr("data")

				m.addAddr2Line(start,end,lineno)

		if bci2line_xml:
			for range_xml in bci2line_xml.getChildren():
				assert(range_xml.name() == "range")
				start = range_xml.getAttr("start")
				end = range_xml.getAttr("end")
				lineno = range_xml.getAttr("data")

				m.addBCI2Line(start,end,lineno)

		method_manager.addMethod(m)
	return method_manager

def load_context(context_root):
	context_manager = context.ContextManager()
	print ("It has ", len(context_root.getChildren()), " contexts")
	for ctxt_xml in context_root.getChildren():

		ctxt = context.Context(ctxt_xml.getAttr("id"))
		# set fields
		ctxt.method_version = ctxt_xml.getAttr("method_version")
		ctxt.binary_addr = ctxt_xml.getAttr("binary_addr")
		ctxt.method_id = ctxt_xml.getAttr("method_id")
		ctxt.bci = ctxt_xml.getAttr("bci")
		ctxt.setParentID(ctxt_xml.getAttr("parent_id"))

		metrics_xml = None
		for c_xml in ctxt_xml.getChildren():
			if c_xml.name() == "metrics":
				assert(not metrics_xml)
				metrics_xml = c_xml
		if metrics_xml:
			for c_xml in metrics_xml.getChildren():
				attr_dict = c_xml.getAttrDict()
				if "value" not in attr_dict:
					break
				
				# if attr_dict["event"] == "BR_INST_RETIRED.NEAR_CALL" and attr_dict["value"] == "1":
				# 	break
				if isinstance(attr_dict["event"], str):
					ctxt.metrics_dict[attr_dict["event"] + ":" + attr_dict["measure"]] = attr_dict["value"]
				else:
					ctxt.metrics_dict[attr_dict["event"]+":"+attr_dict["measure"]] = float(attr_dict["value"])
			ctxt.metrics_type = "BR_INST_RETIRED.NEAR_CALL"

		## add it to context manager
		context_manager.addContext(ctxt)
	roots = context_manager.getRoots()
	print("remaining roots: ", str([r.id for r in roots]))
	assert(len(roots) == 1)
	#context_manager.populateMetrics()
	return context_manager

def output_to_file(method_manager, context_manager, dump_data, trace_list):
	intpr = interpreter.Interpreter(method_manager, context_manager)
	ip = dict()
	counter = 0
	for ctxt_list in context_manager.getAllPaths("0", "root-leaf"):#"root-subnode"):
		i = 0
		while i < len(ctxt_list):
			if ctxt_list[i].metrics_dict:
				counter += int(ctxt_list[i].metrics_dict['BR_INST_RETIRED.NEAR_CALL:COUNT'])
				key = "\n".join(intpr.getSrcPosition(c) for c in ctxt_list[:(i+1)])
				start_list = ctxt_list[i].metrics_dict['PERF_COUNT_HW_INSTRUCTIONS:Start'].split()
				end_list = ctxt_list[i].metrics_dict['PERF_COUNT_HW_INSTRUCTIONS:End'].split()
				# print(ctxt_list[i].metrics_dict)
				for j in range(0, len(start_list)):
					trace_list.append(Trace(int(start_list[j]), int(end_list[j]), {'INSTRUCTIONS_COUNT:TOTAL': int(int(ctxt_list[i].metrics_dict['BR_INST_RETIRED.NEAR_CALL:COUNT']) * round(float(ctxt_list[i].metrics_dict['PERF_COUNT_HW_INSTRUCTIONS:MEAN']))),
																					'METHOD_INSTANCE_COUNT': ctxt_list[i].metrics_dict['BR_INST_RETIRED.NEAR_CALL:COUNT'],
																				  	'INSTRUCTIONS_COUNT:MEAN': round(float(ctxt_list[i].metrics_dict['PERF_COUNT_HW_INSTRUCTIONS:MEAN'])),
																					'INSTRUCTIONS_COUNT:DISPERSION': format(round(float(ctxt_list[i].metrics_dict['PERF_COUNT_HW_INSTRUCTIONS:CV']),2),'.2f')
																					}, key))
				dump_data.append([key, ctxt_list[i].metrics_dict])
			i += 1
	print(counter)

def output_to_pftrace(trace_list, name):
	rows = sorted(trace_list, key=lambda x: x.start)
	name_list = []
	for c in rows:
		s = c.callpath[c.callpath.rfind("\n") + 2 : len(c.callpath)]
		name_list.append(s[s.rfind(".", 0, s.rfind(".", 0, s.find("("))) + 1: s.find("(")])
	# print(name_list)
	# removegap(rows)
	tgen = TraceGenerator(name + ".pftrace")
	pid = tgen.create_group("Thread ID")
	tid = pid.create_track(name)
	my_stack = []
	for i in range(0, len(rows)):
		# print(rows[i].start, ";", rows[i].end)
		output_name = name_list[i]
		output_callpath = rows[i].callpath[1:len(rows[i].callpath)]
		if i == 0:
			my_stack.append(i)
			tid.open(rows[i].start, output_name, categories = output_callpath,  kwargs = {"metrics":rows[i].metrics_dict})
			continue
		top = my_stack[len(my_stack) - 1]
		if rows[top].end > rows[i].start:
			tid.close(rows[i].start)
			my_stack.append(i)
			tid.open(rows[i].start, output_name, categories = output_callpath, kwargs = {"metrics":rows[i].metrics_dict})
		else:
			while len(my_stack) > 1 and rows[my_stack[len(my_stack) - 1]].end < rows[i].start:
				temp = my_stack.pop()
				tid.close(rows[temp].end)
				tid.open(rows[temp].end, name_list[my_stack[len(my_stack) - 1]], categories = output_callpath, kwargs = {"metrics":rows[temp].metrics_dict})
			if rows[my_stack[len(my_stack) - 1]].end > rows[i].start:
				tid.close(rows[i].start)
				tid.open(rows[i].start, output_name, categories = output_callpath, kwargs = {"metrics":rows[i].metrics_dict})
				my_stack.append(i)
			else:
				tid.close(rows[my_stack.pop()].end)
				tid.open(rows[i].start, output_name, categories = output_callpath, kwargs = {"metrics":rows[i].metrics_dict})
				my_stack.append(i)
	while len(my_stack) > 1:
		temp = my_stack.pop()
		tid.close(rows[temp].end)
		tid.open(rows[temp].end, name_list[my_stack[len(my_stack) - 1]], categories = rows[temp].callpath, kwargs = {"metrics":rows[temp].metrics_dict})
	tid.close(rows[my_stack.pop()].end)
	# not necessary, but anyway.
	tgen.flush()

def removegap(rows):
	offset = rows[0].start
	pointer = 0
	rows[0].start -= offset
	rows[0].end -= offset
	for i in range(1, len(rows)):
		if(rows[i].start - offset >= rows[pointer].end):
			offset = rows[i].start - rows[pointer].end
			rows[i].start -= offset
			rows[i].end -= offset
			pointer = i
		else:
			rows[i].start -= offset
			rows[i].end -= offset

def parallel1(tid, tid_file_dict):
	root = xml.XMLObj("root")
	if tid.find("method") != -1 : # == "method" or tid == "method2" or tid == "method3" or tid == "method4":
		level_one_node_tag = "method"
	else:
		level_one_node_tag = "context"

	for f in tid_file_dict[tid]:
		new_root = parse_input_file(f, level_one_node_tag)
		root.addChildren(new_root.getChildren())
	if len(root.getChildren()) > 0:
		#xml_root_dict[tid] = root
		return tid, root

def parallel2(tid, xml_root_dict, method_manager):
	if tid == "method": # or tid == "method2" or tid == "method3" or tid == "method4":
		return
	print("Reconstructing contexts from TID " + tid)
	xml_root = xml_root_dict[tid]
	print("Dumping contexts from TID "+tid)
	dump_data = []
	trace_list = []
	output_to_file(method_manager, load_context(xml_root), dump_data, trace_list)
	output_to_pftrace(trace_list, tid)

	file = open("agent-data-" + tid + ".out", "w")
	rows = sorted(dump_data, key=lambda x: int(x[1]['BR_INST_RETIRED.NEAR_CALL:COUNT']), reverse = True)
	for row in rows:
		file.write(row[0] + "\n")
		for col in row[1]:
			file.write(col + " = " + str(row[1][col]) + "\n")
		file.write("\n\n")

	file.close()



if __name__ == "__main__":
	### read all agent trace files
	tid_file_dict = get_all_files(".")
	# print(tid_file_dict)
	### each file may have two kinds of information
	# 1. context; 2. code
	# the code information should be shared global while the context information is on a per-thread basis.
	pool = mp.Pool(mp.cpu_count())
	print('cpu_count' , mp.cpu_count())
	tmp = [pool.apply_async(parallel1, args=(tid, tid_file_dict)) for tid in tid_file_dict]
	# print(tmp)
	output = [r.get() for r in tmp]
	pool.close()
	output = list(filter(None, output))
	xml_root_dict = dict(output)
	# print(xml_root_dict)
	for i in range(2, 97):
		if 'method' + str(i) in xml_root_dict :
			xml_root_dict['method'].addChildren(xml_root_dict['method' + str(i)].getChildren())
			del xml_root_dict['method' + str(i)]
	# xml_root_dict['method1'].addChildren(xml_root_dict['method2'].getChildren())
	# xml_root_dict['method1'].addChildren(xml_root_dict['method3'].getChildren())
	# xml_root_dict['method1'].addChildren(xml_root_dict['method4'].getChildren())
	# del xml_root_dict['method2']
	# del xml_root_dict['method3']
	# del xml_root_dict['method4']
	#print (xml_root_dict)
	
	### reconstruct method
	print("start to load methods")
	method_root = xml_root_dict["method"]
	method_manager = load_method(method_root)
	print("Finished loading methods")

	print("Start to output")
	with ThreadPoolExecutor(max_workers=20) as executor:
		futures = [executor.submit(parallel2, tid, xml_root_dict, method_manager) for tid in xml_root_dict]

		for future in as_completed(futures):
			print(future.result())

	# with Pool(processes=48) as pool:
	# 	results = [pool.apply_async(parallel2, args=(tid, xml_root_dict, method_manager)) for tid in xml_root_dict]
	# 	for r in results:
	# 		r.wait()


	print("Final dumping")

	#remove_all_files(".")

