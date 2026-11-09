"""Absolute-operand rendering for the x86-32 -> C lifter.

Forked from nfsu2-web's tools/split_reloc.py.  The original rewrites a handful
of operands at hard-coded Underground 2 instruction addresses into split-screen
runtime symbols.  Those addresses do not mean anything in ProStreet -- and its
image shares the same 0x400000 base, so the special cases *can* fire on
unrelated instructions -- so this fork keeps only the plain rendering.

Add game-specific operand rewrites here, keyed by instruction address, when a
guest access needs to be redirected at lift time (nfsu2-web does this for its
split-screen player tables).
"""


def operand(value, ip=0, immediate=False):
    return f"0x{value & 0xffffffff:x}u"
