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
    sub-blocks. 
    Todo: Braces in comments, nor preprocessor tricks are NOT handled.
    >>> get_next_code_block("aaaaaaa{bbb{ccc{dd}cc}b{ccccc}b}a{bb}a")
    bbb{ccc{dd}cc}b{ccccc}b
    """
    s = s.split(lbrace,1)[1]
    #print(s)

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

def generate_command_binary_interface():
    """ Parses the RP2DAQ firmware in C language, searching for the table of commands, and 
    then the binary structures each command is supposed to accept.
    Returns a dict of functions which transmit such binary messages, exactly matching the 
    message specification in the C code. For convenience, these functions have properly named 
    parameters, possibly with default values, and with checks for their minimum/maximum 
    allowed values. """
    # Fixme: error-prone assumption that args are always the 1st block

    C_code = gather_C_code()
    command_codes = generate_command_codes(C_code)

    func_dict = {}
    markdown_docs = ""
    for command_name in command_codes.keys():
        command_code = command_codes[command_name]
        q = re.search(f"void\\s+{command_name}\\s*\\(\\)", C_code)
        func_body = get_next_code_block(C_code[q.span()[1]:]) # code enclosed by closest brace block
        args_struct = get_next_code_block(func_body) 

        struct_signature, cmd_length = "", 0
        arg_names, arg_defaults = [], []

        exec_header = f""
        exec_prepro = f""
        exec_struct = f"" 
        exec_stargs = f""

        try:
            raw_docstring = get_next_code_block(func_body, lbrace="/*", rbrace="*/").strip()
            raw_docstring = re.sub(r"\n(\s*\*)? ?", "\n", raw_docstring)  # rm leading asterisks, keep indent
        except IndexError: 
            raw_docstring = ""
        exec_docstring = f"{raw_docstring}\n\n"

        #print(exec_docstring)


        param_docstring = ""
        for line in re.finditer(r"(u?)int(8|16|32)_t\s+([\w,]*)(.*)",  args_struct):
            unsigned, bits, arg_name_multi, line_comments = line.groups()
            bit_width_code = {8:'b', 16:'h', 32:'i'}[int(bits)]

            arg_attribs = {}
            arg_comment = ""
            for commentoid in re.split("\\s+", line_comments):
                if u := re.match("(\\w*)=(-?\\d*)", commentoid):
                    arg_attribs[u.groups()[0]] = u.groups()[1]
                elif not commentoid in ("", "//", ";"):
                    arg_comment += " " + commentoid
                comment = arg_comment.strip()

            for arg_name in arg_name_multi.split(","):
                cmd_length += int(bits)//8
                exec_struct += bit_width_code.upper() if unsigned else bit_width_code
                exec_stargs += f"\n\t\t\t{arg_name},"
                arg_names.append(arg_name)

                if (d := arg_attribs.get("default")):
                    exec_header += f"{arg_name}={d}, "
                else: 
                    exec_header += f"{arg_name}, "
                if m := arg_attribs.get("min") is not None:
                    exec_prepro += f"\tassert {arg_name} >= {arg_attribs['min']}, "+\
                            f"'Minimum value for {arg_name} is {arg_attribs['min']}'\n"
                if m := arg_attribs.get("max") is not None:
                    exec_prepro += f"\tassert {arg_name} <= {arg_attribs['max']},"+\
                            f"'Maximum value for {arg_name} is {arg_attribs['max']}'\n"

                param_docstring += f"  * {arg_name} {':' if comment else ''} {comment} \n" #  TODO print range  (min=0 max= 2³²-1)

        param_docstring += f"  * _callback : optional report handling function; if set, this command becomes asynchronous (does not wait for report) \n\n"

        #exec_docstring += "Returns:\n\n" # TODO analyze report structure (and comments therein)

        # Append extracted docstring to the overall API reference
        markdown_docs += f"\n\n## {command_name}\n\n{raw_docstring}\n\n"
        markdown_docs += f"__Call signature:__\n\n`{command_name}({exec_header} _callback=None)`\n\n"
        markdown_docs += f"__Parameters__:\n\n{param_docstring}\n"
        #markdown_docs += f"{raw_docstring}\n\n#### Arguments:"

        # TODO once 16-bit msglen enabled: cmd_length will go +3, and 1st struct Byte must change to Half-int 
        exec_msghdr = f"', {cmd_length+2}, {command_code}, "
        code = f"def {command_name}(self,{exec_header} _callback=None):\n" +\
                f'\t"""{raw_docstring}Parameters:\n{param_docstring}"""\n' +\
                exec_prepro +\
                f"\tif {command_code} not in self.sync_report_cb_queues.keys():\n" +\
                f"\t\tself.sync_report_cb_queues[{command_code}] = queue.Queue()\n" +\
                f"\tself.report_callbacks[{command_code}] = _callback\n" +\
                f"\tself.port.write(struct.pack('<BB{exec_struct}{exec_msghdr}{exec_stargs}))\n" +\
                f"\tif not _callback:\n" +\
                f"\t\treturn self.default_blocking_callback({command_code})"



        func_dict[command_name] = code  # returns Python code
    return func_dict, markdown_docs

def generate_report_binary_interface():
    C_code = gather_C_code()
    command_codes = generate_command_codes(C_code)
    report_lengths, report_header_signatures, arg_names_for_reports = {}, {}, {}
    for report_name, report_number in command_codes.items():
        q = re.search(f"}}\\s*{report_name}_report", C_code)
        #print(report_number, report_name,q)
        report_struct_code = get_prev_code_block(C_code[:q.span()[0]+1]) # code enclosed by closest brace block
        #print(f"{report_name=} {report_struct_code=}") 

        report_header_signature, report_length = "<", 0
        arg_names, arg_defaults = [], []

        for line in re.finditer(r"(u?)int(8|16|32)_t\s+([\w,]*)([; \t\w=\/]*)",  report_struct_code):
            unsigned, bits, arg_name_multi, line_comments = line.groups()
            bit_width_code = {8:'b', 16:'h', 32:'i'}[int(bits)]

            for arg_name in arg_name_multi.split(","):
                report_length += int(bits)//8
                report_header_signature += bit_width_code.upper() if unsigned else bit_width_code
                arg_names.append(arg_name)
        report_lengths[report_number] = report_length
        assert report_length > 0, "every report has to contain at least 1 byte, troubles ahead"
        report_header_signatures[report_number] = report_header_signature
        arg_names_for_reports[report_number] = arg_names

    return report_lengths, report_header_signatures, arg_names_for_reports

def gather_C_code():
    C_code = open('rp2daq.c').read()
    for included in pathlib.Path('include').glob('*.c'):
        C_code += open(included).read()
    return C_code

if __name__ == "__main__":
    pyref_file = "./docs/PYTHON_REFERENCE.md"
    print("This module was run as a command. It will parse C code and re-generate "+pyref_file)
    
    command_functions, markdown_docs = generate_command_binary_interface()

    with open(pyref_file, "w") as of:
        of.write("# RP2DAQ: Python API reference\n\nThis file was auto-generated by c_code_parser.py, " + 
            "using comments found in all ```include/*.c``` source files.\n\n Contents:\n\n")
        for cmdname in command_functions.keys():
            of.write(f"   1. [{cmdname}](#{cmdname})\n") # .replace('_','-')
        of.write(markdown_docs)
    #for func_name, func_code in command_functions.items():
        #print(func_code)

    report_lengths, report_header_signatures, arg_names_for_reports = generate_report_binary_interface()
    #print(f"{report_lengths=}")
    #print(f"{report_header_signatures=}")
    #print(f"{arg_names_for_reports=}")
