"""Unit test of check_signatures.expand_descriptor_macros: the descriptor macro forms of the adapter headers
(actor_virtuals, stage_flush, weapon_methods) and literal rows. Synthetic text only."""
import importlib.util
import re
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("check_signatures", Path(sys.argv[1]))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ROW = re.compile(r"\{ES2_HOOK_ID\((\w+)\),\s*(\w+)")
failures = 0


def check(label, text, expected):
    global failures
    got = ROW.findall(module.expand_descriptor_macros(text))
    if got != expected:
        failures += 1
        print(f"FAIL {label}: {got} != {expected}")
    else:
        print(f"PASS {label}")


check("prefix and suffix, name first with more arguments",
      "#define AV_DESC(name,len) \\\n    {ES2_HOOK_ID(name),av_##name##_sig,len,0}\n"
      "static es2_hook_descriptor a[]={\n    AV_DESC(alpha,5),\n    AV_DESC(beta,6),\n};\n",
      [("alpha", "av_alpha_sig"), ("beta", "av_beta_sig")])
check("no prefix, the name the only argument",
      "#define SF_DESC(name) \\\n    {ES2_HOOK_ID(name),name##_sig,sizeof(name##_sig),5}\n"
      "static es2_hook_descriptor b[]={\n    SF_DESC(flush_a),\n    SF_DESC(flush_b),\n};\n",
      [("flush_a", "flush_a_sig"), ("flush_b", "flush_b_sig")])
check("signature from a second argument, body over two lines",
      "#define WM_DESC(name,sig,relocate,lens,args) \\\n"
      "    {ES2_HOOK_ID(name),wm_##sig##_sig,sizeof(wm_##sig##_sig),relocate,wm_##lens##_lens, \\\n"
      "     args,0}\n"
      "static es2_hook_descriptor c[]={\n    WM_DESC(equip_ready,zero,5,xor,1),\n    WM_DESC(gun_ready,gun_ready,6,load,1),\n};\n",
      [("equip_ready", "wm_zero_sig"), ("gun_ready", "wm_gun_ready_sig")])
check("literal rows stay",
      "static es2_hook_descriptor d[]={\n    {ES2_HOOK_ID(rng_next),rng_next_sig,10,6},\n};\n",
      [("rng_next", "rng_next_sig")])
check("the #define line is not read as a hook",
      "#define X_DESC(name) \\\n    {ES2_HOOK_ID(name),name##_sig}\nstatic es2_hook_descriptor e[]={X_DESC(only)};\n",
      [("only", "only_sig")])
print("check_signatures macro expansion: %d failure(s)" % failures)
sys.exit(1 if failures else 0)
