#!/usr/bin/env python3
"""Compile the small MXPL UEFI subset into C."""
import ast
import json
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1])
target = pathlib.Path(sys.argv[2])
raw_lines = source.read_text(encoding="utf-8").splitlines()

def fail(line, message):
    raise SystemExit(f"{source}:{line}: {message}")

def quoted(value, line):
    try:
        result = ast.literal_eval(value)
    except (SyntaxError, ValueError):
        fail(line, "expected a quoted string")
    if not isinstance(result, str) or any(ord(c) > 127 for c in result):
        fail(line, "strings currently support ASCII text only")
    return json.dumps(result)

def split_args(value):
    parts, current, depth, quote = [], "", 0, None
    for char in value:
        if char in "\"'" and (not current or current[-1] != "\\"):
            quote = None if quote == char else char if quote is None else quote
        if quote is None:
            if char == "(": depth += 1
            if char == ")": depth -= 1
            if char == "," and depth == 0:
                parts.append(current.strip()); current = ""; continue
        current += char
    parts.append(current.strip())
    return parts

def number(value, line):
    match = re.fullmatch(r"(-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+))(?:px|ms|x|y)?", value.strip())
    if not match: fail(line, f"expected a number: {value}")
    return match[1]


def integer_expr(value, line):
    value = value.strip()
    if value in numeric_vars:
        if numeric_units[value] == "percent": fail(line, "low-level operations need a plain number")
        return "num_" + value
    match = re.fullmatch(r"(-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+))(?:px|ms|x|y)?",value)
    if match:return match[1]
    return numeric_expr(value,line)


def string_expr(value, line):
    value = value.strip()
    if value in variables: return "var_" + value
    return quoted(value, line)


def numeric_expr(value, line):
    try: root = ast.parse(value, mode="eval").body
    except SyntaxError: fail(line, f"invalid numeric expression: {value}")
    operators = {ast.Add:"+", ast.Sub:"-", ast.Mult:"*", ast.Div:"/", ast.FloorDiv:"/",
                 ast.Mod:"%", ast.BitAnd:"&", ast.BitOr:"|", ast.BitXor:"^",
                 ast.LShift:"<<", ast.RShift:">>"}
    def walk(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return str(node.value)
        if isinstance(node, ast.Name) and node.id in numeric_vars:
            if numeric_units[node.id] == "percent":fail(line,"percent cannot be used in a low-level expression")
            return "num_" + node.id
        if isinstance(node, ast.BinOp) and type(node.op) in operators:
            return f"({walk(node.left)} {operators[type(node.op)]} {walk(node.right)})"
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub,ast.Invert)):
            return ("-" if isinstance(node.op,ast.USub) else "~")+"("+walk(node.operand)+")"
        if isinstance(node, ast.Call) and isinstance(node.func,ast.Name) and len(node.args)==1 and not node.keywords:
            fn={"peek8":"mx_peek8","peek16":"mx_peek16","peek32":"mx_peek32","in8":"mx_in8","alloc":"mx_alloc"}.get(node.func.id)
            if fn:return f"{fn}({walk(node.args[0])})"
        fail(line,f"unsupported numeric expression: {value}")
    return walk(root)


def spatial(value, line, axis):
    match = re.fullmatch(r"(-?\d+)(%|px|x|y)?", value.strip())
    if not match: fail(line, f"expected a position or size: {value}")
    if match[2] == "%":
        return f'mx_percent_{axis}({match[1]})'
    return match[1]


def radius_value(value, line, width, height):
    match = re.fullmatch(r"radius\(\s*(\d+)(px|%)?\s*\)", value)
    if not match: fail(line, "invalid radius")
    if match[2] == "%":
        return f'mx_percent_radius({match[1]},{width},{height})'
    return match[1]

def color(value, line):
    match = re.fullmatch(r"rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", value)
    if not match or any(int(n) > 255 for n in match.groups()):
        fail(line, f"invalid RGB color: {value}")
    return "0x%02x%02x%02x" % tuple(map(int, match.groups()))

lines = []
c_bodies = {}
source_pos = 0
while source_pos < len(raw_lines):
    index = source_pos + 1
    raw = raw_lines[source_pos]
    source_pos += 1
    if not raw.strip() or raw.lstrip().startswith("#"): continue
    if "\t" in raw[:len(raw)-len(raw.lstrip())]: fail(index, "use spaces for indentation")
    indent = len(raw)-len(raw.lstrip())
    if indent % 4: fail(index, "indentation must use multiples of four spaces")
    statement = raw.strip()
    if statement == "__C__:":
        block = []
        while source_pos < len(raw_lines):
            c_raw = raw_lines[source_pos]
            c_indent = len(c_raw)-len(c_raw.lstrip())
            if c_raw.strip() and c_indent <= indent: break
            if c_raw.strip() and c_indent < indent+4: fail(source_pos+1,"C block needs indentation")
            block.append(c_raw[indent+4:] if c_raw.strip() else "")
            source_pos += 1
        if not block: fail(index,"empty C block")
        c_bodies[index] = block
        lines.append((indent,index,statement))
        continue
    # Accept the older quoted class spelling too.
    for prefix in ("print", "rect"):
        if statement.startswith(prefix + "('class") and statement.endswith("')"):
            statement = prefix + " " + statement[len(prefix)+2:-2]
    for kind in ("rect", "text"):
        plain = re.fullmatch(rf'{kind}\((.*)\)', statement)
        if plain:
            statement = f'{kind} class"__mx_{kind}_{index}"({plain[1]})'
    lines.append((indent, index, statement))
if not lines or lines[0][2] != "[uefi]" or lines[0][0]:
    fail(1, "first statement must be [uefi]")
lines.pop(0)

def parse_block(pos, indent):
    nodes = []
    while pos < len(lines):
        level, line, statement = lines[pos]
        if level < indent: break
        if level > indent: fail(line, "unexpected indentation")
        pos += 1
        if statement.endswith(":") and (statement == "while True:" or statement.startswith(("if ", "elif ", "def ")) or statement == "else:"):
            if pos >= len(lines) or lines[pos][0] != indent+4:
                fail(line, "expected an indented block")
            body, pos = parse_block(pos, indent+4)
            nodes.append((line, statement, body))
        elif statement == "__C__:":
            nodes.append((line, statement, c_bodies[line]))
        else:
            nodes.append((line, statement, None))
    return nodes, pos

tree, end = parse_block(0, 0)
if end != len(lines): fail(lines[end][1], "unexpected indentation")

rects, texts, conditional, functions, variables, numeric_vars, c_blocks = [], [], set(), {}, set(), set(), []
numeric_units = {}

def collect(nodes, nested=False):
    for line, statement, body in nodes:
        match = re.fullmatch(r'rect class"([A-Za-z_]\w*)"\((.*)\)', statement)
        if match:
            args = split_args(match[2])
            compact = len(args) == 5 and args[-1].startswith("radius(")
            if compact:
                x, y, w, h, c = spatial(args[0],line,"x"), "0", spatial(args[1],line,"x"), spatial(args[2],line,"y"), color(args[3],line)
                radius = args[4]
            elif len(args) in (5, 6):
                x,y,w,h = (spatial(args[j],line,"x" if j in (0,2) else "y") for j in range(4))
                c = color(args[4],line)
                radius = args[5] if len(args)==6 else "radius(0)"
            else: fail(line, "rect expects x, y, width, height, rgb, and optional radius")
            rects.append((match[1],x,y,w,h,c,radius_value(radius,line,w,h)))
            if nested: conditional.add(match[1])
            continue
        match = re.fullmatch(r'text class"([A-Za-z_]\w*)"\((.*)\)',statement)
        if match:
            args=split_args(match[2])
            if len(args) not in (3,4): fail(line,"text expects x, y, string, and optional rgb")
            texts.append((match[1],spatial(args[0],line,"x"),spatial(args[1],line,"y"),quoted(args[2],line),color(args[3],line) if len(args)==4 else "0xffffff"))
            if nested: conditional.add(match[1])
            continue
        match=re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(input|poll_input|password_input|wifi_ssid|ethernet_mac)\((.*)\)',statement)
        if match:
            variables.add(match[1])
            if match[2] not in ("wifi_ssid","ethernet_mac"):quoted(match[3],line)
        match=re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)(?:px|x|y|%)?)',statement)
        if match:
            numeric_vars.add(match[1])
            unit = "percent" if match[2].endswith("%") else "pixels"
            if match[1] in numeric_units and numeric_units[match[1]] != unit:
                fail(line, f"cannot mix percent and pixel values in {match[1]}")
            numeric_units[match[1]] = unit
        match=re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(.+)',statement)
        if match and match[1] not in variables and match[1] not in numeric_vars:
            numeric_vars.add(match[1]);numeric_units[match[1]]="pixels"
        if statement.startswith("def "):
            name=re.fullmatch(r'def ([A-Za-z_]\w*)\(\):',statement)
            if not name: fail(line,"invalid function declaration")
            functions[name[1]]=body
        if statement=="__C__:": c_blocks.extend(body)
        elif body is not None: collect(body,True)

collect(tree)
if variables & numeric_vars: fail(1,"variable cannot be both text and number")
names=[item[0] for item in rects+texts]
if len(names)!=len(set(names)): fail(1,"duplicate object name")
if len(rects)>48 or len(texts)>48: fail(1,"maximum 48 rectangles and 48 text objects")

def coordinate(value,line,axis):
    value=value.strip()
    if value in numeric_vars:
        return f'mx_percent_{axis}(num_{value})' if numeric_units[value] == "percent" else 'num_'+value
    return spatial(value,line,axis)

def emit(nodes, depth=0):
    result=[]; pad="    "*depth; pos=0
    while pos<len(nodes):
        line,statement,body=nodes[pos];pos+=1
        if statement.startswith(("elif ","else:")):fail(line,"elif/else without matching if")
        if statement.startswith("if "):
            group=[(line,statement,body)]
            while pos<len(nodes) and nodes[pos][1].startswith(("elif ","else:")):
                group.append(nodes[pos]);pos+=1
            for j,(branch_line,branch,branch_body) in enumerate(group):
                if branch=="else:":
                    if j != len(group)-1:fail(branch_line,"else must be last")
                    result.append(pad+"else {")
                else:
                    click_match=re.fullmatch(r'(?:if|elif)\s+click\((.*)\):',branch)
                    if click_match:
                        name=quoted(click_match[1],branch_line)
                        if json.loads(name) not in names:fail(branch_line,"unknown clickable object")
                        result.append(pad+("if" if j==0 else "else if")+f'(mx_clicked({name}))'+' {')
                        result.extend(emit(branch_body,depth+1));result.append(pad+"}")
                        continue
                    key_match=re.fullmatch(r'(?:if|elif)\s+key\((.*)\):',branch)
                    if key_match:
                        key_name=quoted(key_match[1],branch_line)
                        if key_name not in ('"Escape"','"Enter"'):fail(branch_line,"supported keys: Escape, Enter")
                        result.append(pad+("if" if j==0 else "else if")+f'(mx_key({key_name}))'+' {')
                        result.extend(emit(branch_body,depth+1));result.append(pad+"}")
                        continue
                    value_match=re.fullmatch(r'(?:if|elif)\s+([A-Za-z_]\w*):',branch)
                    if value_match:
                        name=value_match[1]
                        if name in variables:condition=f'var_{name}[0]'
                        elif name in numeric_vars:condition=f'num_{name}'
                        else:fail(branch_line,f"unknown variable {name}")
                        result.append(pad+("if" if j==0 else "else if")+f'({condition})'+' {')
                        result.extend(emit(branch_body,depth+1));result.append(pad+"}")
                        continue
                    numeric_match=re.fullmatch(r'(?:if|elif)\s+([A-Za-z_]\w*)\s*(==|!=|<=|>=|<|>)\s*(.+):',branch)
                    if numeric_match and numeric_match[1] in numeric_vars:
                        rhs=numeric_expr(numeric_match[3],branch_line)
                        result.append(pad+("if" if j==0 else "else if")+f'(num_{numeric_match[1]} {numeric_match[2]} {rhs})'+' {')
                        result.extend(emit(branch_body,depth+1));result.append(pad+"}")
                        continue
                    match=re.fullmatch(r'(?:if|elif)\s+"?([A-Za-z_]\w*)"?\s*={1,2}\s*"?([A-Za-z_]\w*)"?:',branch)
                    if not match:fail(branch_line,"expected if Command == value:")
                    if match[1] not in variables:fail(branch_line,f"unknown variable {match[1]}")
                    result.append(pad+("if" if j==0 else "else if")+f'(mx_streq(var_{match[1]},"{match[2]}"))'+' {')
                result.extend(emit(branch_body,depth+1));result.append(pad+"}")
            continue
        if statement=="while True:":
            result.append(pad+"for(;;){mx_poll();");result.extend(emit(body,depth+1));result.append(pad+"mx_step();}");continue
        if statement.startswith("def ") or statement=="__C__:":continue
        match=re.fullmatch(r'(?:rect|text) class"([A-Za-z_]\w*)"\(.*\)',statement)
        if match:
            if match[1] in conditional:result.append(pad+f'mx_visible("{match[1]}",1);')
            continue
        match=re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(input|poll_input|password_input|wifi_ssid|ethernet_mac)\((.*)\)',statement)
        if match:
            fn={"input":"mx_input","poll_input":"mx_poll_input","password_input":"mx_password_input","wifi_ssid":"mx_wifi_ssid","ethernet_mac":"mx_ethernet_mac"}[match[2]]
            arg=integer_expr(match[3],line) if match[2] in ("wifi_ssid","ethernet_mac") else quoted(match[3],line)
            result.append(pad+f'var_{match[1]}={fn}({arg});');continue
        match=re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(.*)',statement)
        if match and match[1] in numeric_vars:
            rhs=match[2].strip()
            call=re.fullmatch(r'(peek8|peek16|peek32|in8|alloc|runbin|wifi_scan|wifi_count|wifi_quality|wifi_connect|ethernet_scan|ethernet_count|ethernet_link|ethernet_open|ethernet_close|ethernet_send|ethernet_receive)\((.*)\)',rhs)
            if call:
                if call[1] in ("wifi_scan","wifi_count","ethernet_scan","ethernet_count"):
                    if call[2].strip():fail(line,f"{call[1]} takes no arguments")
                    arg=""
                elif call[1] in ("ethernet_send","ethernet_receive"):
                    args=split_args(call[2])
                    if len(args)!=3:fail(line,f"{call[1]} expects adapter, buffer, length")
                    arg=','.join(integer_expr(a,line) for a in args)
                elif call[1]=="wifi_connect":
                    args=split_args(call[2])
                    if len(args)!=2:fail(line,"wifi_connect expects SSID and password")
                    arg=','.join(string_expr(a,line) for a in args)
                else:arg=string_expr(call[2],line) if call[1]=="runbin" else integer_expr(call[2],line)
                fn={"peek8":"mx_peek8","peek16":"mx_peek16","peek32":"mx_peek32","in8":"mx_in8","alloc":"mx_alloc","runbin":"mx_runbin","wifi_scan":"mx_wifi_scan","wifi_count":"mx_wifi_count","wifi_quality":"mx_wifi_quality","wifi_connect":"mx_wifi_connect","ethernet_scan":"mx_ethernet_scan","ethernet_count":"mx_ethernet_count","ethernet_link":"mx_ethernet_link","ethernet_open":"mx_ethernet_open","ethernet_close":"mx_ethernet_close","ethernet_send":"mx_ethernet_send","ethernet_receive":"mx_ethernet_receive"}[call[1]]
                result.append(pad+f'num_{match[1]}={fn}({arg});'+('mx_forget_password();' if call[1]=='wifi_connect' else ''));continue
            parsed=re.fullmatch(r'(-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+))(?:px|x|y|%)?',rhs)
            if parsed:
                if (rhs.endswith("%")) != (numeric_units[match[1]] == "percent"):
                    fail(line,f"cannot mix percent and pixel values in {match[1]}")
                result.append(pad+f'num_{match[1]}={parsed[1]};');continue
            if numeric_units[match[1]]=="percent":fail(line,"percent variable needs a percent value")
            result.append(pad+f'num_{match[1]}={numeric_expr(rhs,line)};');continue
        match=re.fullmatch(r'print class"([A-Za-z_]\w*)"\((.*)\)',statement)
        if match:
            args=split_args(match[2])
            if len(args)==2:
                old=re.fullmatch(r'(-?\d+(?:px|x|y|%)?)\s*(".*"|\'.*\')',args[1])
                if not old:fail(line,"print class expects x, y, and text")
                args=[args[0],old[1],old[2]]
            if len(args)!=3:fail(line,"print class expects x, y, and text")
            result.append(pad+f'mx_label("{match[1]}",{coordinate(args[0],line,"x")},{coordinate(args[1],line,"y")},{quoted(args[2],line)});');continue
        match=re.fullmatch(r'print\((.*)\)',statement)
        if match:
            arg=match[1].strip()
            result.append(pad+(f'mx_print_number(num_{arg});' if arg in numeric_vars else f'mx_print({string_expr(arg,line)});'));continue
        match=re.fullmatch(r'(poke8|poke16|poke32|out8)\((.*)\)',statement)
        if match:
            args=split_args(match[2])
            if len(args)!=2:fail(line,f"{match[1]} expects address/port and value")
            fn={"poke8":"mx_poke8","poke16":"mx_poke16","poke32":"mx_poke32","out8":"mx_out8"}[match[1]]
            result.append(pad+f'{fn}({integer_expr(args[0],line)},{integer_expr(args[1],line)});');continue
        match=re.fullmatch(r'free\((.*)\)',statement)
        if match:result.append(pad+f'mx_free({integer_expr(match[1],line)});');continue
        match=re.fullmatch(r'(browser|runbin)\((.*)\)',statement)
        if match:
            fn="mx_browser" if match[1]=="browser" else "mx_runbin"
            result.append(pad+f'{fn}({string_expr(match[2],line)});');continue
        if statement=="wifi_manager()":result.append(pad+'mx_wifi_manager();');continue
        if statement=="wifi_scan()":result.append(pad+'mx_wifi_scan();');continue
        if statement=="ethernet_manager()":result.append(pad+'mx_ethernet_manager();');continue
        if statement=="ethernet_scan()":result.append(pad+'mx_ethernet_scan();');continue
        match=re.fullmatch(r'wifi_connect\((.*)\)',statement)
        if match:
            args=split_args(match[1])
            if len(args)!=2:fail(line,"wifi_connect expects SSID and password")
            result.append(pad+f'mx_wifi_connect({string_expr(args[0],line)},{string_expr(args[1],line)});mx_forget_password();');continue
        match=re.fullmatch(r'sleep\((.*)\)',statement)
        if match:result.append(pad+f'mx_sleep({integer_expr(match[1],line)});');continue
        match=re.fullmatch(r'(show|hide)\((.*)\)',statement)
        if match:result.append(pad+f'mx_visible({quoted(match[2],line)},{1 if match[1]=="show" else 0});');continue
        match=re.fullmatch(r'clear\((.*)\)',statement)
        if match:result.append(pad+f'mx_background({color(match[1],line)});mx_present();');continue
        match=re.fullmatch(r'animate\((.*)\)',statement)
        if match:
            args=split_args(match[1])
            if len(args) not in (2,3):fail(line,"animate expects name, distance, optional duration")
            result.append(pad+f'mx_animate({quoted(args[0],line)},{coordinate(args[1],line,"x")},{number(args[2],line) if len(args)==3 else "1000"});');continue
        match=re.fullmatch(r'move\((.*)\)',statement)
        if match:
            args=split_args(match[1])
            if len(args)!=3:fail(line,"move expects name, dx, dy")
            result.append(pad+f'mx_move({quoted(args[0],line)},{coordinate(args[1],line,"x")},{coordinate(args[2],line,"y")});');continue
        if statement=="shutdown()":result.append(pad+"mx_shutdown();");continue
        if statement=="mouse()":result.append(pad+"mx_mouse(1600);");continue
        match=re.fullmatch(r'mouse\(\s*dpi\s*=\s*([A-Za-z_]\w*|\d+)\s*\)',statement)
        if match:
            value=match[1]
            if value in numeric_vars:
                if numeric_units[value]=="percent":fail(line,"dpi must be a plain number")
                value='num_'+value
            elif not value.isdigit():fail(line,f"unknown dpi variable {value}")
            elif not 100<=int(value)<=12800:fail(line,"dpi must be between 100 and 12800")
            result.append(pad+f'mx_mouse({value});');continue
        match=re.fullmatch(r'([A-Za-z_]\w*)\(\)',statement)
        if match and match[1] in functions:result.append(pad+f'fn_{match[1]}();');continue
        fail(line,f"unsupported MX statement: {statement}")
    return result

code=['#include "mx_uefi.h"','#include <stdint.h>']+c_blocks
for name in sorted(variables):code.append(f'static const char *var_{name}="";')
for name in sorted(numeric_vars):code.append(f'static int64_t num_{name}=0;')
for name in functions:code.append(f'static void fn_{name}(void);')
for name,body in functions.items():
    code.append(f'static void fn_{name}(void){{');code.extend(emit(body,1));code.append('}')
code.append('void mx_main(void){')
for line,statement,body in tree:
    match=re.fullmatch(r'clear\((.*)\)',statement)
    if match:code.append(f'mx_background({color(match[1],line)});')
for name,x,y,w,h,c,r in rects:code.append(f'mx_add("{name}",{x},{y},{w},{h},{c},{r});')
for name,x,y,value,c in texts:code.append(f'mx_text("{name}",{x},{y},{value},{c});')
for name in sorted(conditional):code.append(f'mx_visible("{name}",0);')
code.append('mx_present();')
code.extend(emit([node for node in tree if not re.fullmatch(r'clear\(.*\)',node[1])],1))
if not any(s=="while True:" for _,s,_ in tree):code.append('for(;;){mx_step();}')
code.append('}')
target.write_text('\n'.join(code)+'\n',encoding='utf-8')
print(f'Generated {target} objects: {len(names)} functions: {len(functions)}')
