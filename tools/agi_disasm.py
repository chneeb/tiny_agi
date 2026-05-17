#!/usr/bin/env python3
"""Minimal AGI Logic disassembler — proper test-mode tracking."""
import sys, struct

TESTS = [
    ("ZERO", 0),
    ("equaln", 2), ("equalv", 2), ("lessn", 2), ("lessv", 2),
    ("greatern", 2), ("greaterv", 2), ("isset", 1), ("issetv", 1),
    ("has", 1), ("obj_in_room", 2), ("posn", 5), ("controller", 1),
    ("have_key", 0), ("said", -1),
    ("compare_strings", 2), ("obj_in_box", 5),
    ("center_posn", 5), ("right_posn", 5),
]

ACTIONS_RAW = """
_return:0 increment:1 decrement:1 assignn:2 assignv:2 addn:2 addv:2 subn:2 subv:2
lindirectv:2 rindirect:2 lindirectn:2 set:1 reset:1 toggle:1 set_v:1 reset_v:1 toggle_v:1
new_room:1 new_room_v:1 load_logics:1 load_logics_v:1 call:1 call_v:1
load_pic:1 draw_pic:1 show_pic:0 discard_pic:1 overlay_pic:1 show_pri_screen:0
load_view:1 load_view_v:1 discard_view:1 animate_obj:1 unanimate_all:0
draw:1 erase:1 position:3 position_v:3 get_posn:3 reposition:3
set_view:2 set_view_v:2 set_loop:2 set_loop_v:2 fix_loop:1 release_loop:1
set_cel:2 set_cel_v:2 last_cel:2 current_cel:2 current_loop:2 current_view:2 number_of_loops:2
set_priority:2 set_priority_v:2 release_priority:1 get_priority:2
stop_update:1 start_update:1 force_update:1
ignore_horizon:1 observe_horizon:1 set_horizon:1
object_on_water:1 object_on_land:1 object_on_anything:1
ignore_objs:1 observe_objs:1 distance:3
stop_cycling:1 start_cycling:1 normal_cycle:1
end_of_loop:2 reverse_cycle:1 reverse_loop:2 cycle_time:2
stop_motion:1 start_motion:1 step_size:2 step_time:2
move_obj:5 move_obj_v:5 follow_ego:3 wander:1 normal_motion:1
set_dir:2 get_dir:2 ignore_blocks:1 observe_blocks:1 block:4 unblock:0
get:1 get_v:1 drop:1 put:2 put_v:2 get_room_v:2
load_sound:1 sound:2 stop_sound:0
print:1 print_v:1 display:3 display_v:3 clear_lines:3 text_screen:0 graphics:0
set_cursor_char:1 set_text_attribute:2 shake_screen:1 configure_screen:3
status_line_on:0 status_line_off:0
set_string:2 get_string:5 word_to_string:2 parse:1 get_num:2
prevent_input:0 accept_input:0 set_key:3
add_to_pic:7 add_to_pic_v:7 status:0 save_game:0 restore_game:0
init_disk:0 restart_game:0 show_obj:1 random:3
program_control:0 player_control:0 obj_status_v:1 quit:1
show_mem:0 pause:0 echo_line:0 cancel_line:0 init_joy:0 toggle_monitor:0
version:0 script_size:1 set_game_id:1 log:1 set_scan_start:0 reset_scan_start:0
reposition_to:3 reposition_to_v:3 trace_on:0 trace_info:3
print_at:4 print_at_v:4 discard_view_v:1
clear_text_rect:5 set_upper_left:2
set_menu:1 set_menu_item:2 submit_menu:0 enable_item:1 disable_item:1 menu_input:0
show_obj_v:1 open_dialogue:0 close_dialogue:0
mul_n:2 mul_v:2 div_n:2 div_v:2 close_window:0 set_simple:1
push_script:0 pop_script:0 hold_key:0
""".split()
ACTIONS = [(t.split(":")[0], int(t.split(":")[1])) for t in ACTIONS_RAW]


def load_logdir(path):
    data = open(path, "rb").read()
    out = []
    for i in range(0, len(data), 3):
        b0, b1, b2 = data[i], data[i+1], data[i+2]
        vol = (b0 & 0xF0) >> 4
        offset = ((b0 & 0x0F) << 16) | (b1 << 8) | b2
        out.append((vol, offset))
    return out

def load_resource(vol_path, offset):
    f = open(vol_path, "rb")
    f.seek(offset)
    header = f.read(5)
    sig = struct.unpack("<H", header[0:2])[0]
    assert sig == 0x3412, f"Bad signature 0x{sig:x} at {offset}"
    size = struct.unpack("<H", header[3:5])[0]
    return f.read(size)

def decode_messages(msg_section):
    if not msg_section: return {}
    num = msg_section[0]
    key = b"Avis Durgan"
    data_area_start = 3 + 2 * num
    # Decrypt entire data area with cumulative key position
    decrypted = bytearray(msg_section)
    for pos in range(data_area_start, len(msg_section)):
        decrypted[pos] = msg_section[pos] ^ key[(pos - data_area_start) % len(key)]
    msgs = {}
    for m in range(num):
        off_pos = 3 + 2*m
        if off_pos + 1 >= len(msg_section): break
        off = msg_section[off_pos] | (msg_section[off_pos+1] << 8)
        if off == 0:
            msgs[m+1] = ""; continue
        text_start = off + 1
        buf = bytearray()
        i = text_start
        while i < len(decrypted) and decrypted[i] != 0:
            buf.append(decrypted[i]); i += 1
        msgs[m+1] = buf.decode('latin-1', errors='replace')
    return msgs

def disasm_logic(data, logic_no):
    msg_offset = data[0] | (data[1] << 8)
    code_start = 2
    code_end = code_start + msg_offset
    msg_section = data[msg_offset + 2:]
    msgs = decode_messages(msg_section)

    print(f"=== Logic {logic_no}: code [0x{code_start:x},0x{code_end:x}), msgs={len(msgs)} ===\n")

    pc = code_start
    in_test = False
    indent = 0
    while pc < code_end:
        addr = pc - code_start
        op = data[pc]; pc += 1
        sp = ' ' * indent
        if not in_test:
            if op == 0xFF:
                in_test = True
                print(f"  {addr:04x}: {sp}if (")
                indent += 2
            elif op == 0xFE:
                jo = data[pc] | (data[pc+1] << 8); pc += 2
                if jo & 0x8000: jo -= 0x10000
                target = (pc - code_start) + jo
                print(f"  {addr:04x}: {sp}}} else_goto 0x{target:04x}")
                indent = max(0, indent - 2)
            elif op == 0x00:
                print(f"  {addr:04x}: {sp}return")
                indent = 0
            else:
                if op < len(ACTIONS):
                    name, nargs = ACTIONS[op]
                    args = []
                    for _ in range(nargs):
                        if pc >= code_end: break
                        args.append(data[pc]); pc += 1
                    annot = ""
                    if name == "print" and args and args[0] in msgs:
                        annot = f"  // \"{msgs[args[0]][:90]}\""
                    if name == "display" and len(args) >= 3 and args[2] in msgs:
                        annot = f"  // \"{msgs[args[2]][:90]}\""
                    if name == "new_room" and args:
                        annot = f"  // -> room {args[0]}"
                    if name == "set" and args:
                        annot = f"  // f{args[0]} = TRUE"
                    if name == "reset" and args:
                        annot = f"  // f{args[0]} = FALSE"
                    arg_str = ', '.join(str(a) for a in args)
                    print(f"  {addr:04x}: {sp}{name}({arg_str}){annot}")
                else:
                    print(f"  {addr:04x}: {sp}?? op=0x{op:02x}")
        else:
            if op == 0xFF:
                in_test = False
                jo = data[pc] | (data[pc+1] << 8); pc += 2
                target_addr = (pc - code_start) + jo
                indent = max(0, indent - 2)
                sp2 = ' ' * indent
                print(f"  {addr:04x}: {sp2}) {{   // else_goto 0x{target_addr:04x}")
                indent += 2
            elif op == 0xFC:
                print(f"  {addr:04x}: {sp}OR")
            elif op == 0xFD:
                print(f"  {addr:04x}: {sp}NOT")
            else:
                if op < len(TESTS):
                    name, nargs = TESTS[op]
                    if name == "said":
                        n = data[pc]; pc += 1
                        args = []
                        for _ in range(n):
                            if pc+1 >= code_end: break
                            w = data[pc] | (data[pc+1] << 8); pc += 2
                            args.append(w)
                        print(f"  {addr:04x}: {sp}{name}({', '.join(str(a) for a in args)})")
                    else:
                        args = []
                        for _ in range(nargs):
                            if pc >= code_end: break
                            args.append(data[pc]); pc += 1
                        annot = ""
                        if name == "isset" and args:
                            annot = f"  // f{args[0]}"
                        if name == "controller" and args:
                            annot = f"  // ctrl{args[0]}"
                        if name == "equaln" and len(args) == 2:
                            annot = f"  // v{args[0]}=={args[1]}"
                        if name == "lessn" and len(args) == 2:
                            annot = f"  // v{args[0]}<{args[1]}"
                        print(f"  {addr:04x}: {sp}{name}({', '.join(str(a) for a in args)}){annot}")
                else:
                    print(f"  {addr:04x}: {sp}TEST? op=0x{op:02x}")
    print()
    print("--- Messages ---")
    for k, v in sorted(msgs.items()):
        print(f"  msg{k}: \"{v}\"")

if __name__ == "__main__":
    game_dir = sys.argv[1] if len(sys.argv) > 1 else "/home/chneeb/Downloads/quest/sq1"
    logic_no = int(sys.argv[2]) if len(sys.argv) > 2 else 9
    logdir = load_logdir(f"{game_dir}/LOGDIR")
    vol, off = logdir[logic_no]
    data = load_resource(f"{game_dir}/VOL.{vol}", off)
    print(f"Logic {logic_no}: vol={vol} offset=0x{off:x}, size={len(data)}")
    disasm_logic(data, logic_no)
