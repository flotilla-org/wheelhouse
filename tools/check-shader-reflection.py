#!/usr/bin/env python3
"""Check the generated shaders' interfaces against what the backends bind.

tools/generate-shaders.py runs this on Slang's reflection (`slangc
-reflection-json`) of every shader for every target, before it replaces the
committed generated files, so a failure leaves them untouched. It fails,
naming the shader, target and field or parameter, when:

- a uniform block's field has a different offset or size on another target
  (HLSL cbuffer, Metal constant buffer, std140 through SPIR-V) or in another
  shader that shares the block;
- a uniform block has no C struct registered in C_STRUCTS;
- a resource is bound with a different kind or slot than the backends bind it
  (BINDINGS), or OpenGL would bind two blocks or two textures to one point;
- a vertex shader's attributes are not the ones the backends feed it
  (VERTEX_BUFFERS);
- a fragment stage reads a varying its vertex stage doesn't write, at the
  same semantic (HLSL, Metal) or location (SPIR-V, and so GLSL), with the
  same type.

It also writes a C header of StaticAsserts: every uniform field's offset and
size, each block's size and each vertex buffer's stride, as reflected. The
header is committed and included by render_inc.c, so every build holds the C
structs to the shaders' layout without needing the shader tools.

  check-shader-reflection.py --header OUT.h [--stamp LINE] DIR...

Each DIR holds <shader>.<target>[.<stage>].json, target one of hlsl, metal,
spirv. A file may reflect one entry point (HLSL and Metal are compiled per
stage) or both (SPIR-V).
"""
import argparse
import json
import re
import sys
from pathlib import Path

TARGETS = ('hlsl', 'metal', 'spirv')

# The C struct each uniform block is filled from. A block shared by several
# shaders (the effects' EffectUniforms) is checked once per shader and must
# agree. Per-backend structs are asserted only in that backend's build.
C_STRUCTS = {
    'RectUniforms': {None: 'R_CoreRectUniforms'},
    'BlurUniforms': {None: 'R_CoreBlurUniforms'},
    'MeshUniforms': {None: 'R_CoreMeshUniforms'},
    'MeshGroupUniforms': {None: 'R_CoreMeshGroupUniforms'},
    'CompositeUniforms': {None: 'R_CoreCompositeUniforms'},
    'EffectUniforms': {
        'R_BACKEND_D3D11': 'R_D3D11_EffectUniforms',
        'R_BACKEND_METAL': 'R_MTL_EffectUniforms',
        'R_BACKEND_OPENGL': 'R_OGL_EffectUniforms',
    },
}

# Where the backends bind each resource, by parameter name (a trailing _ is a
# prefix). The same for every core shader and effect:
# - D3D11 (render_d3d11.c): {VS,PS}SetConstantBuffers slot 0, the mesh
#   group's slot 1; PSSetShaderResources and PSSetSamplers slot 0.
# - Metal (render_metal.c): set{Vertex,Fragment}Buffer index 0, the mesh
#   group's fragment buffer 1; setFragmentTexture and
#   setFragmentSamplerState index 0. Vertex buffers 29 and 30
#   (R_MTL_CORE_*_BUFFER) are the vertex descriptors', so no uniform may
#   take them.
# OpenGL binds by position after link instead; see check_gl_blocks.
BINDINGS = {
    'u': ('constantBuffer', 0),
    'g': ('constantBuffer', 1),
    't_': ('shaderResource', 0),
    's_': ('samplerState', 0),
}
BOUND_TARGETS = ('hlsl', 'metal')

# OpenGL (render_opengl.c) binds every uniform block to point 0, then the
# block it finds by name to its own point; samplers stay on texture unit 0.
GL_NAMED_BLOCKS = {'MeshGroupUniforms'}

# The vertex buffers each shader reads, in attribute order, and the C size
# of one element. An attribute's position in the concatenated list is its
# Metal [[attribute(n)]] (the MTLVertexDescriptor in render_metal.c) and its
# GL location (glVertexAttribPointer in render_opengl.c); its name is the
# D3D11 input-layout semantic (r_d3d11_g_*_ilay_elements in render_d3d11.c).
# A shader not listed reads no attributes.
VERTEX_BUFFERS = {
    'rect': [('sizeof(R_Rect2DInst)', ['POS', 'TEX', 'COL0', 'COL1', 'COL2', 'COL3', 'CRAD', 'STY'])],
    'mesh': [('R_CORE_MESH_VERTEX_STRIDE', ['POS', 'NOR', 'TEX', 'COL']),
             ('sizeof(R_Mesh3DInst)', ['XFORM0', 'XFORM1', 'XFORM2', 'XFORM3', 'REMAP'])],
}

INPUT_KINDS = ('varyingInput', 'metalAttribute')
SCALAR_BYTES = {'float32': 4, 'int32': 4, 'uint32': 4, 'float16': 2, 'bool': 4}


class Errors(list):
    def add(self, shader, target, what, expected, actual):
        message = '%s.slang %s: %s: expected %s, got %s' % (shader, target, what, expected, actual)
        if message not in self:
            self.append(message)


def bindings_of(var):
    if 'binding' in var:
        return [var['binding']]
    return list(var.get('bindings', []))


def semantic_of(var):
    """(NAME, index) with any trailing digits of the name folded into index."""
    name = var.get('semanticName')
    if not name:
        return None
    return split_semantic(name.upper(), var.get('semanticIndex', 0))


def split_semantic(name, index=0):
    m = re.match(r'^(.*?)(\d+)$', name)
    if m and not index:
        return (m.group(1), int(m.group(2)))
    return (name, index)


def semantic_str(sem):
    return '%s%d' % sem


def type_sig(t):
    """('float32', components) for a scalar, vector or matrix type."""
    kind = t.get('kind')
    if kind == 'scalar':
        return (t.get('scalarType'), 1)
    if kind == 'vector':
        return (type_sig(t['elementType'])[0], t['elementCount'])
    if kind == 'matrix':
        return (type_sig(t['elementType'])[0], t['rowCount'] * t['columnCount'])
    return (kind, 0)


def sig_str(sig):
    return '%s%s' % (sig[0], 'x%d' % sig[1] if sig[1] != 1 else '')


def block_struct(t):
    """The struct type layout inside a ConstantBuffer<T>'s type layout."""
    inner = t.get('elementVarLayout', {}).get('type') or t.get('elementType') or {}
    return inner if inner.get('kind') == 'struct' else None


def parse_globals(params):
    """Uniform blocks {param: (struct, {field: (offset, size)})}, resource
    bindings {param: [(kind, index)]} and texture names of a reflection's
    global parameters."""
    blocks, resources, textures = {}, {}, set()
    for var in params:
        name, t = var['name'], var.get('type', {})
        slots = [(b['kind'], b.get('index', 0)) for b in bindings_of(var) if b['kind'] != 'uniform']
        resources[name] = slots
        if t.get('kind') == 'resource':
            textures.add(name)
        if t.get('kind') in ('constantBuffer', 'parameterBlock'):
            st = block_struct(t)
            fields = {}
            for f in (st or {}).get('fields', []):
                for b in bindings_of(f):
                    if b['kind'] == 'uniform':
                        fields[f['name']] = (b['offset'], b['size'])
            blocks[name] = ((st or {}).get('name', '?'), fields)
    return blocks, resources, textures


def flatten_varyings(var, kinds, base, path, out):
    """Append (path, semantic, location, sig) for every leaf varying of var.
    Locations in Slang's reflection are relative to the enclosing struct."""
    loc = base
    for b in bindings_of(var):
        if b['kind'] in kinds:
            loc = (base or 0) + b.get('index', 0)
    sem = semantic_of(var)
    t = var.get('type', {})
    if t.get('kind') == 'struct':
        for f in t.get('fields', []):
            flatten_varyings(f, kinds, loc, path + [f['name']], out)
        return
    if sem and sem[0].startswith('SV_'):
        return
    out.append(('.'.join(path), sem, loc, type_sig(t)))


def parse_entry_point(ep):
    inputs, outputs = [], []
    for p in ep.get('parameters', []):
        flatten_varyings(p, INPUT_KINDS, None, [p.get('name', '?')], inputs)
    if 'result' in ep:
        flatten_varyings(ep['result'], ('varyingOutput',), None, ['result'], outputs)
    return inputs, outputs


def load(dirs):
    """{(shader, target): {'blocks', 'resources', 'entry_points'}}"""
    shaders = {}
    for d in dirs:
        for path in sorted(Path(d).glob('*.json')):
            parts = path.name.split('.')
            shader, target = parts[0], parts[1]
            if target not in TARGETS:
                continue
            data = json.loads(path.read_text())
            entry = shaders.setdefault((shader, target), {'blocks': [], 'resources': [], 'textures': set(), 'entry_points': {}})
            # Each file's globals are kept: HLSL and Metal compile each stage
            # on its own, and either stage's layout could drift.
            blocks, resources, textures = parse_globals(data.get('parameters', []))
            entry['blocks'] += sorted(blocks.items())
            entry['resources'] += sorted(resources.items())
            entry['textures'] |= textures
            for ep in data.get('entryPoints', []):
                entry['entry_points'][ep['name']] = parse_entry_point(ep)
    return shaders


def expected_binding(param):
    if param in BINDINGS:
        return BINDINGS[param]
    for key, value in BINDINGS.items():
        if key.endswith('_') and param.startswith(key):
            return value
    return None


def check_bindings(shader, target, info, errors):
    for param, slots in sorted(set((p, tuple(s)) for p, s in info['resources'])):
        slots = list(slots)
        expected = expected_binding(param)
        if expected is None:
            errors.add(shader, target, 'parameter %s' % param, 'a binding listed in BINDINGS', 'none listed')
        elif slots != [expected]:
            errors.add(shader, target, 'parameter %s binding' % param, '%s %d' % expected,
                       ', '.join('%s %d' % s for s in slots) or 'nothing')


def check_gl_blocks(shader, info, errors):
    shared = sorted({s for _, (s, _) in info['blocks'] if s not in GL_NAMED_BLOCKS})
    if len(shared) > 1:
        errors.add(shader, 'spirv', 'uniform blocks on GL binding point 0', 'at most one', ', '.join(shared))
    textures = sorted(info['textures'])
    if len(textures) > 1:
        errors.add(shader, 'spirv', 'textures on GL unit 0', 'at most one', ', '.join(textures))


def check_vertex_inputs(shader, target, info, errors):
    if 'vs_main' not in info['entry_points']:
        return
    inputs = info['entry_points']['vs_main'][0]
    expected = [split_semantic(s) for _, sems in VERTEX_BUFFERS.get(shader, []) for s in sems]
    if target == 'hlsl':
        # D3D11 matches the input layout by semantic.
        got = sorted(semantic_str(sem) for _, sem, _, _ in inputs)
        want = sorted(semantic_str(sem) for sem in expected)
        if got != want:
            errors.add(shader, target, 'vs_main input semantics', ' '.join(want) or 'none', ' '.join(got) or 'none')
        return
    # Metal and GL match by attribute index / location.
    by_loc = {}
    for path, sem, loc, _ in inputs:
        if loc in by_loc:
            errors.add(shader, target, 'vs_main inputs %s and %s' % (by_loc[loc][0], path), 'distinct attributes',
                       'both at attribute %s' % loc)
        by_loc[loc] = (path, sem)
    for loc, sem in enumerate(expected):
        if loc not in by_loc:
            errors.add(shader, target, 'vs_main attribute %d (%s)' % (loc, semantic_str(sem)), 'an input', 'none')
        elif by_loc[loc][1] not in (None, sem):
            errors.add(shader, target, 'vs_main attribute %d (%s)' % (loc, by_loc[loc][0]),
                       semantic_str(sem), semantic_str(by_loc[loc][1]))
    for loc, (path, sem) in sorted(by_loc.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)):
        if loc is None or loc >= len(expected):
            errors.add(shader, target, 'vs_main input %s' % path, 'attributes 0..%d' % (len(expected) - 1)
                       if expected else 'no attributes', 'attribute %s' % loc)


def check_varyings(shader, target, info, errors):
    eps = info['entry_points']
    if 'vs_main' not in eps or 'fs_main' not in eps:
        errors.add(shader, target, 'entry points', 'vs_main and fs_main', ', '.join(sorted(eps)) or 'none')
        return
    by_sem = target != 'spirv'
    key = (lambda v: v[1]) if by_sem else (lambda v: v[2])
    show = (lambda k: semantic_str(k) if k else '?') if by_sem else (lambda k: 'location %s' % k)
    written = {key(v): v for v in eps['vs_main'][1]}
    for v in eps['fs_main'][0]:
        k = key(v)
        if k not in written:
            errors.add(shader, target, 'fs_main input %s (%s)' % (v[0], show(k)), 'a vs_main output there', 'none')
        elif written[k][3] != v[3]:
            errors.add(shader, target, 'fs_main input %s (%s)' % (v[0], show(k)),
                       '%s, as vs_main writes it' % sig_str(written[k][3]), sig_str(v[3]))


def collect_layouts(shaders, errors):
    """{struct: {'fields': {field: (offset, size)}, 'size': n, 'seen': [...]}},
    requiring every target and shader to agree."""
    layouts = {}
    for (shader, target), info in sorted(shaders.items()):
        for param, (struct, fields) in info['blocks']:
            if struct not in C_STRUCTS:
                errors.add(shader, target, 'uniform block %s (%s)' % (param, struct), 'a C struct in C_STRUCTS', 'none')
                continue
            seen = '%s %s' % (shader, target)
            layout = layouts.setdefault(struct, {'fields': {}, 'size': 0, 'first': {}})
            for field, (offset, size) in fields.items():
                if field.startswith('_padding'):
                    continue
                if field in layout['fields'] and layout['fields'][field] != (offset, size):
                    want = layout['fields'][field]
                    errors.add(shader, target, '%s.%s offset, size' % (param, field),
                               '%d, %d (as in %s)' % (want[0], want[1], layout['first'][field]), '%d, %d' % (offset, size))
                    continue
                layout['fields'].setdefault(field, (offset, size))
                layout['first'].setdefault(field, seen)
                layout['size'] = max(layout['size'], offset + size)
    return layouts


def vertex_strides(shaders):
    """{c_size_expr: (shader, bytes)} from the reflected attribute types."""
    strides = {}
    for (shader, target), info in sorted(shaders.items()):
        if target != 'hlsl' or 'vs_main' not in info['entry_points']:
            continue
        sizes = {semantic_str(sem): SCALAR_BYTES.get(sig[0], 0) * sig[1]
                 for _, sem, _, sig in info['entry_points']['vs_main'][0] if sem}
        for c_size, sems in VERTEX_BUFFERS.get(shader, []):
            sems = [semantic_str(split_semantic(s)) for s in sems]
            if all(s in sizes for s in sems):
                strides[c_size] = (shader, sum(sizes[s] for s in sems))
    return strides


def ident(*parts):
    # StaticAssert appends the line number, so end on a separator.
    return re.sub(r'[^A-Za-z0-9_]', '_', '_'.join(str(p) for p in parts)) + '_'


def header(layouts, strides, stamp):
    out = ['// generated by tools/generate-shaders.py from Slang\'s reflection - do not edit\n']
    if stamp:
        out.append('// %s\n' % stamp)
    out.append('//\n'
               '// Every uniform field at the offset and size all three targets give it\n'
               '// (HLSL cbuffer, Metal constant buffer, std140 through SPIR-V), and each\n'
               '// vertex buffer\'s stride, so the C side is held to the shaders\' layout.\n'
               '// Included by render_inc.c after the backend.\n\n'
               '#define r_shader_field_size(T, m) sizeof(((T *)0)->m)\n')
    for struct in sorted(layouts):
        layout = layouts[struct]
        for backend, c_struct in sorted(C_STRUCTS[struct].items(), key=lambda kv: kv[0] or ''):
            out.append('\n')
            if backend:
                out.append('#if R_BACKEND == %s\n' % backend)
            out.append('//- %s -> %s\n' % (struct, c_struct))
            for field, (offset, size) in sorted(layout['fields'].items(), key=lambda kv: kv[1]):
                out.append('StaticAssert(OffsetOf(%s, %s) == %d, %s);\n' % (c_struct, field, offset, ident(struct, field, 'offset', offset)))
                out.append('StaticAssert(r_shader_field_size(%s, %s) == %d, %s);\n' % (c_struct, field, size, ident(struct, field, 'size', size)))
            out.append('StaticAssert(sizeof(%s) >= %d, %s);\n' % (c_struct, layout['size'], ident(struct, 'covers', layout['size'], 'bytes')))
            if backend:
                out.append('#endif\n')
    if strides:
        out.append('\n//- vertex buffers\n')
        for c_size, (shader, size) in sorted(strides.items()):
            out.append('StaticAssert(%s == %d, %s);\n' % (c_size, size, ident(shader, 'vertex_buffer', c_size, size)))
    out.append('\n#undef r_shader_field_size\n')
    return ''.join(out)


def check(shaders):
    errors = Errors()
    names = sorted({s for s, _ in shaders})
    for shader in names:
        for target in TARGETS:
            info = shaders.get((shader, target))
            if info is None:
                errors.add(shader, target, 'reflection', 'a reflection file', 'none')
                continue
            if target in BOUND_TARGETS:
                check_bindings(shader, target, info, errors)
            else:
                check_gl_blocks(shader, info, errors)
            check_vertex_inputs(shader, target, info, errors)
            check_varyings(shader, target, info, errors)
    layouts = collect_layouts(shaders, errors)
    return errors, layouts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--header', required=True, help='C header of StaticAsserts to write')
    ap.add_argument('--stamp', default='', help='tool versions line for the header')
    ap.add_argument('dirs', nargs='+')
    args = ap.parse_args(argv)
    shaders = load(args.dirs)
    if not shaders:
        print('shader reflection: no reflection files in %s' % ' '.join(args.dirs), file=sys.stderr)
        return 1
    errors, layouts = check(shaders)
    if errors:
        for e in errors:
            print('shader reflection: ' + e, file=sys.stderr)
        print('shader reflection: %d mismatch(es); generated files left unchanged' % len(errors), file=sys.stderr)
        return 1
    Path(args.header).write_text(header(layouts, vertex_strides(shaders), args.stamp))
    print('shader reflection: %d shaders x %d targets agree with the backends' % (len({s for s, _ in shaders}), len(TARGETS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
