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

    C_code = gather_C_code()
    command_codes = generate_command_codes(C_code)

    func_dict = {}
    for command_name in command_codes.keys():
        #print(command_name, f"void {command_name}\\(\\)" in C_code)
        command_code = command_codes[command_name]
        q = re.search(f"void\\s+{command_name}\\s*\\(\\)", C_code)
        #print(C_code[q.span()[1]:q.span()[1]+1000])
        func_body = get_next_code_block(C_code[q.span()[1]:]) # code enclosed by closest brace block
        # Fixme: error-prone assumption that args are always the 1st block
        args_struct = get_next_code_block(func_body) 

        struct_signature, cmd_length = "", 0
        arg_names, arg_defaults = [], []

        exec_header = f""
        exec_prepro = f""
        exec_struct = f"" 
        exec_stargs = f""

        for line in re.finditer(r"(u?)int(8|16|32)_t\s+([\w,]*)([; \t\w=\-\/]*)",  args_struct):
            unsigned, bits, arg_name_multi, line_comments = line.groups()
            bit_width_code = {8:'b', 16:'h', 32:'i'}[int(bits)]

            comment_dict = {}
            for comment in re.split("\\s+", line_comments):
                if u := re.match("(\\w*)=(-?\\d*)", comment):
                    comment_dict[u.groups()[0]] = u.groups()[1]

            for arg_name in arg_name_multi.split(","):
                cmd_length += int(bits)//8
                exec_struct += bit_width_code.upper() if unsigned else bit_width_code
                exec_stargs += f"\n\t\t\t{arg_name},"
                arg_names.append(arg_name)

                if (d := comment_dict.get("default")):
                    exec_header += f"{arg_name}={d}, "
                else: 
                    exec_header += f"{arg_name},"
                if m := comment_dict.get("min") is not None:
                    exec_prepro += f"\tassert {arg_name} >= {comment_dict['min']}, "+\
                            f"'Minimum value for {arg_name} is {comment_dict['min']}'\n"
                if m := comment_dict.get("max") is not None:
                    exec_prepro += f"\tassert {arg_name} <= {comment_dict['max']},"+\
                            f"'Maximum value for {arg_name} is {comment_dict['max']}'\n"

        # once 16-bit msglen enabled: cmd_length will go +3, and 1st struct Byte must change to Half-int 
        exec_msghdr = f"', {cmd_length+2}, {command_code}, "
        code = f"def {command_name}(self,{exec_header} _callback=None):\n" +\
                exec_prepro +\
                f"\tif {command_code} not in self.sync_report_cb_queues.keys():\n" +\
                f"\t\tself.sync_report_cb_queues[{command_code}] = queue.Queue()\n" +\
                f"\tself.report_callbacks[{command_code}] = _callback\n" +\
                f"\tself.port.write(struct.pack('<BB{exec_struct}{exec_msghdr}{exec_stargs}))\n" +\
                f"\tif not _callback:\n" +\
                f"\t\treturn self.default_blocking_callback({command_code})"


        func_dict[command_name] = code  # returns Python code
    return func_dict

def generate_report_binary_interface():
    C_code = gather_C_code()
    command_codes = generate_command_codes(C_code)
    report_lengths, report_header_signatures, arg_namess = {}, {}, {}
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
        arg_namess[report_number] = arg_names

    return report_lengths, report_header_signatures, arg_namess

def gather_C_code():
    C_code = open('rp2daq.c').read()
    for included in pathlib.Path('include').glob('*.c'):
        C_code += open(included).read()
    return C_code

if __name__ == "__main__":
    print("This module was run as a command. It will print out the generated command interface.")
    
    command_functions = generate_command_binary_interface()
    for func_name, func_code in command_functions.items():
        print(func_code)

    report_lengths, report_header_signatures, arg_namess = generate_report_binary_interface()

    print(f"{report_lengths=}")
    print(f"{report_header_signatures=}")
    print(f"{arg_namess=}")






"""
    #exec(code); func_dict[command_name] = locals().get(command_name) # returns function objects

    # TODO also autogenerate code that registers an (optionally) provided callback function 
    # TODO by default it should set a "blocking" flag, and reference to a default "unblocking" CB
    # e.g. use named param like "self.internal_adc(..., cb=self._blocking_CB)"

"""
