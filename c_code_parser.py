#!/usr/bin/python3  
#-*- coding: utf-8 -*-

"""
"""

import pathlib
import re


def remove_c_comments(f):
    f = re.sub("\/\*.+?\*\/", "", f, flags=re.M+re.S) # rm block comments
    f = re.sub("\/\/.*", "", f) # rm line comments
    return f


def get_next_code_block(s, lbrace="{", rbrace="}"):
    """
    Returns the content of a first brace-delimited block, properly handling nested 
    sub-blocks. Does NOT ignore parentheses in strings, comments etc.
    >>> get_next_code_block("aaaaaaa{bbb{ccc{dd}cc}b{ccccc}b}a{bb}a")
    bbb{ccc{dd}cc}b{ccccc}b
    """
    s = s.split(lbrace,1)[1] # strip everything before the opening parenthesis
    found_block, nest_level = "", 1
    for new_chunk in s.split(rbrace): 
        nest_level += new_chunk.count(lbrace) - 1 
        found_block += new_chunk 
        if nest_level == 0: 
            return found_block
        found_block += rbrace

def get_prev_code_block(s, lbrace="{", rbrace="}"):
    return get_next_code_block(s[::-1], lbrace=rbrace, rbrace=lbrace)[::-1]

def generate_command_codes(C_code):
    command_table_match = re.search(r"message_descriptor message_table", C_code, 
            flags=re.M+re.S)
    command_table_code = get_next_code_block(C_code[command_table_match.span()[1]:])
    command_table_code = remove_c_comments(command_table_code)
    return {c.group():n//2 for n,c in enumerate(re.finditer(r"\w+", command_table_code, 
        flags=re.M+re.S)) if not c.group().endswith("_report")}

def analyze_c_firmware():
    """ Parses the RP2DAQ firmware in C language, searching for the table of commands, and 
    then the binary structures each command is supposed to accept.
    Returns a dict of functions which transmit such binary messages, exactly matching the 
    message specification in the C code. For convenience, these functions have properly named 
    parameters, possibly with default values, and with checks for their minimum/maximum 
    allowed values. """
    # Fixme: error-prone assumption that args are always the 1st parentheses block in every 
    # command/function body

    proj_path = pathlib.Path(__file__).resolve().parent
    C_code = gather_C_code(proj_path)
    command_codes = generate_command_codes(C_code)

    # Results that will be dynamically populated
    func_dict = {}  # Rp2daq class' methods for calling commands
    report_names, report_lengths, report_header_signatures, arg_names_for_reports = {}, {}, {}, {}
    markdown_docs = ""  # Docs generated from C code and comments therein

    for command_name, command_code in command_codes.items():
        ## Search for the command handlers in C code
        q = re.search(f"void\\s+{command_name}\\s*\\(\\)", C_code)
        func_body = get_next_code_block(C_code[q.span()[1]:]) # code enclosed by closest brace block
        args_struct = get_next_code_block(func_body) 

        struct_signature, cmd_length = "", 0
        arg_names, arg_defaults = [], []
        exec_header, exec_prepro, exec_struct,  exec_stargs = ["" for _ in range(4)]

        try:
            raw_docstring = get_next_code_block(func_body, lbrace="/*", rbrace="*/").strip()
            raw_docstring = re.sub(r"\n(\s*\*)? ?", "\n", raw_docstring)  # rm leading asterisks, keep indent
        except IndexError: 
            raw_docstring = ""


        param_docstring = ""
        args_struct = re.sub(r'\n\s*\/\/', '', args_struct) # allow multi-line comments for params
        for line in re.finditer(r"(u?)int(8|16|32|64)_t\s+([\w,]*)(.*)", args_struct):
            unsigned, bits, arg_name_multi, line_comments = line.groups()
            bit_width_code = {8:'b', 16:'h', 32:'i', 64:'q'}[int(bits)]

            arg_attribs = {}
            arg_comment = ""
            for commentoid in re.split("\\s+", line_comments): # the rest of the line may be ...
                u = re.match("(\\w*)=(-?\\d*)", commentoid)
                if u:   # ... either annotation of values (min,max,default) ...
                    arg_attribs[u.groups()[0]] = u.groups()[1]
                elif not commentoid in ("", "//", ";"): # ... or really just a comment
                    arg_comment += " " + commentoid
                comment = arg_comment.strip()

            for arg_name in arg_name_multi.split(","):
                cmd_length += int(bits)//8
                exec_struct += bit_width_code.upper() if unsigned else bit_width_code
                exec_stargs += f"\n\t\t\t{arg_name},"
                arg_names.append(arg_name)

                param_maxmindef = ""
                m = arg_attribs.get("min")
                if m is not None:
                    exec_prepro += f"\tassert {arg_name} >= {arg_attribs['min']}, "+\
                            f"'Minimum value for {arg_name} is {arg_attribs['min']}'\n"
                    param_maxmindef += f"min={m}, "

                m = arg_attribs.get("max")
                if m is not None:
                    exec_prepro += f"\tassert {arg_name} <= {arg_attribs['max']},"+\
                            f"'Maximum value for {arg_name} is {arg_attribs['max']}'\n"
                    param_maxmindef += f"max={m}, "

                d = arg_attribs.get("default")
                if d: 
                    exec_header += f"{arg_name}={d}, "
                    param_maxmindef += f"default={d}, "
                else: exec_header += f"{arg_name}, "


                param_docstring += f"  * **{arg_name}** "
                if param_maxmindef or comment: param_docstring += f" :"
                if comment: param_docstring += f" {comment} "
                if param_maxmindef: param_docstring += f" _({param_maxmindef.strip(', ')})_ "
                param_docstring += f"\n"

        param_docstring += f"  * **_callback** : Optionally, a function to handle future report(s). "
        param_docstring += f"If set, makes this command asynchronous so it does not wait for the command being finished. \n\n"


        # TODO once 16-bit msglen enabled: cmd_length will go +3, and 1st struct Byte must change to Half-int 
        exec_msghdr = f"', {cmd_length+2}, {command_code}, "
        code = f"def {command_name}(self,{exec_header} _callback=None):\n" +\
                f'\t"""{raw_docstring}\n\nParameters:\n{param_docstring}"""\n' +\
                exec_prepro +\
                f"\tif not self.run_event.is_set(): raise RuntimeError('Sending commands when device disconnected')\n" +\
                f"\tif {command_code} not in self.sync_report_cb_queues.keys():\n" +\
                f"\t\tself.sync_report_cb_queues[{command_code}] = queue.Queue()\n" +\
                f"\tself.report_callbacks[{command_code}] = _callback\n" +\
                f"\tself.command_queue.put(struct.pack('<BB{exec_struct}{exec_msghdr}{exec_stargs}))\n" +\
                f"\tif not _callback:\n" +\
                f"\t\treturn self.default_blocking_callback({command_code})"

                #f"\tself.report_pipe_out.send_bytes(struct.pack('<BB{exec_struct}{exec_msghdr}{exec_stargs}))\n" +\
        func_dict[command_name] = code  # returns Python code


        ## Search for the report structures in C code
        report_docstring = ""
        q = re.search(f"}}\\s*{command_name}_report", C_code)
        report_struct_code = get_prev_code_block(C_code[:q.span()[0]+1]) # code enclosed by closest brace block
        #report_struct_code = remove_c_comments(report_struct_code)

        report_header_signature, report_length = "<", 0
        arg_names, arg_defaults = [], []

        report_struct_code = re.sub(r'\n\s*\/\/', '', report_struct_code) # allow multi-line comments for report comments
        for line in re.finditer(r"(u?)int(8|16|32|64)_t\s+([\w,]*)(.*)",  report_struct_code):
            unsigned, bits, arg_name_multi, line_comments = line.groups()
            bit_width_code = {8:'b', 16:'h', 32:'i', 64:'q'}[int(bits)]

            arg_comment = ""
            for commentoid in re.split("\\s+", line_comments): 
                if commentoid.strip() not in ("", "//", ";"): # filter for real comments only
                    arg_comment += " " + commentoid

            if arg_name_multi == 'report_code': 
                arg_comment = f'{command_code} {arg_comment}'

            for arg_name in arg_name_multi.split(","):
                arg_name = arg_name.lstrip('_') # TODO should simply remove all underscores in C argnames, then rm this line
                report_length += int(bits)//8
                report_header_signature += bit_width_code.upper() if unsigned else bit_width_code
                arg_names.append(arg_name)

                if arg_name not in ('data_count', 'data_bitwidth'):
                    report_docstring += f"  * **{arg_name}** {':' if arg_comment else ''} {arg_comment.strip()} \n"
                elif arg_name == 'data_count':
                    report_docstring += f"  * **data** : as a list of integers. \n"

        report_names[command_code] = command_name
        report_lengths[command_code] = report_length
        assert report_length > 0, "every report has to contain at least 1 byte, troubles ahead"
        report_header_signatures[command_code] = report_header_signature
        arg_names_for_reports[command_code] = arg_names

        # Append extracted docstring to the overall API reference
        markdown_docs += f"\n\n## {command_name}\n\n"
        markdown_docs += f"```Python\n{command_name}({exec_header} _callback=None)\n```\n\n"
        markdown_docs += f"{raw_docstring}\n\n"
        markdown_docs += f"***Command parameters:***\n\n{param_docstring}\n"
        markdown_docs += f"***Report returns:***\n\n{report_docstring}\n"

    return report_names, report_lengths, report_header_signatures, arg_names_for_reports, func_dict, markdown_docs


def gather_C_code(proj_path):
    C_code = open(proj_path/'rp2daq.c').read()
    for included in pathlib.Path(proj_path/'include').glob('*.c'):
        C_code += open(included).read()
    return C_code


def get_C_code_version():
    rp2daq_h_file = open(pathlib.Path(__file__).resolve().parent/'rp2daq.h')
    rp2daq_h_line = [l for l in rp2daq_h_file.readlines() if '#define FIRMWARE_VERSION' in l][0]
    return int(rp2daq_h_line.split('rp2daq_')[1][:6])


if __name__ == "__main__":
    proj_path = pathlib.Path(__file__).resolve().parent
    reference_file = "./docs/PYTHON_REFERENCE.md"
    print(f"This module was run as a command. It will parse C code and re-generate {reference_file}")
    
    report_names, report_lengths, report_header_signatures, arg_names_for_reports, command_functions, markdown_docs =\
            analyze_c_firmware()

    with open(proj_path / reference_file, "w") as of:
        of.write("# RP2DAQ: Python API reference\n\nThis file was auto-generated by c_code_parser.py, " + 
            "using firmware code and comments found in all ```include/*.c``` source files.\n\n" + 
            "If not specified otherwise, all data types are integers. \n\n" + 
            f"Firmware version: {get_C_code_version()}. \n\nContents:\n\n")
        for cmdname in command_functions.keys():
            of.write(f"   1. [{cmdname}](#{cmdname})\n") # .replace('_','-')
        of.write(markdown_docs)
    print("Done")

