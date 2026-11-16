#!/usr/bin/env python3
"""Exercise tools/check-shader-reflection.py on hand-built reflection.

The fixtures follow the shape of `slangc -reflection-json` for rect.slang,
mesh.slang and the crt effect: one file per stage for HLSL and Metal, one for
both stages for SPIR-V. They are built here rather than generated, so these
tests need no shader tools; they check the checker, not the shaders.
"""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_shader_reflection', ROOT / 'tools/check-shader-reflection.py')
csr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(csr)


def f32(n):
    scalar = {'kind': 'scalar', 'scalarType': 'float32'}
    return scalar if n == 1 else {'kind': 'vector', 'elementCount': n, 'elementType': scalar}


def array(n, count):
    return {'kind': 'array', 'elementCount': count, 'elementType': f32(n), 'uniformStride': 16}


def block(param, struct, fields, slot):
    return {
        'name': param,
        'binding': slot,
        'type': {
            'kind': 'constantBuffer',
            'elementType': {
                'kind': 'struct', 'name': struct,
                'fields': [{'name': n, 'type': t, 'binding': {'kind': 'uniform', 'offset': o, 'size': s}}
                           for n, t, o, s in fields],
            },
        },
    }


def resource(param, slot, kind):
    return {'name': param, 'binding': slot, 'type': {'kind': kind}}


def varying(name, n, semantic, index, kind):
    return {'name': name, 'type': f32(n), 'semanticName': semantic, 'binding': {'kind': kind, 'index': index}}


def texcoord(name, n, semantic_index, index, kind):
    var = varying(name, n, 'TEXCOORD', index, kind)
    if semantic_index:
        var['semanticIndex'] = semantic_index
    return var


def system_value(name, n, semantic):
    return {'name': name, 'type': f32(n), 'semanticName': semantic}


def struct_param(name, struct, fields, kind, index, stage):
    return {'name': name, 'stage': stage, 'binding': {'kind': kind, 'index': index, 'count': len(fields)},
            'type': {'kind': 'struct', 'name': struct, 'fields': fields}}


def slot(target, kind, index):
    if target == 'spirv':
        return {'kind': 'descriptorTableSlot', 'index': index}
    return {'kind': kind, 'index': index}


RECT_FIELDS = [
    ('viewport_size', f32(2), 0, 8), ('opacity', f32(1), 8, 4), ('sample_is_surface', f32(1), 12, 4),
    ('texture_sample_channel_map', array(4, 4), 16, 64), ('texture_size', f32(2), 80, 8),
    ('xform_scale', f32(2), 88, 8), ('xform', array(4, 3), 96, 48), ('surface_flip_y', f32(1), 144, 4),
]
RECT_V2P = [('texcoord', 2, 'TEXCOORD', 0), ('sdf_sample_pos', 2, 'TEXCOORD', 1), ('tint', 4, 'TEXCOORD', 2),
            ('corner_radius', 1, 'TEXCOORD', 3), ('rect_half_size', 2, 'TEXCOORD', 4),
            ('border_thickness', 1, 'TEXCOORD', 5), ('softness', 1, 'TEXCOORD', 6), ('omit_texture', 1, 'TEXCOORD', 7)]
RECT_INST = ['POS', 'TEX', 'COL0', 'COL1', 'COL2', 'COL3', 'CRAD', 'STY']


def v2p(fields, kind):
    return [system_value('position', 4, 'SV_Position')] + \
           [texcoord(n, c, si, i, kind) for i, (n, c, _, si) in enumerate(fields)]


def entry_points(vs_inputs, v2p_fields):
    """vs_inputs: [(param, [(semantic, components)])], one per vertex buffer."""
    vs_params = [system_value('vid', 1, 'SV_VertexID')]
    base = 0
    for name, attrs in vs_inputs:
        fields = [varying('%s_%d' % (name, i), n, s, i, 'varyingInput') for i, (s, n) in enumerate(attrs)]
        vs_params.append(struct_param(name, name.capitalize(), fields, 'varyingInput', base, 'vertex'))
        base += len(attrs)
    vs = {'name': 'vs_main', 'stage': 'vertex', 'parameters': vs_params,
          'result': {'stage': 'vertex', 'binding': {'kind': 'varyingOutput', 'index': 0, 'count': len(v2p_fields)},
                     'type': {'kind': 'struct', 'name': 'V2P', 'fields': v2p(v2p_fields, 'varyingOutput')}}}
    fs = {'name': 'fs_main', 'stage': 'fragment',
          'parameters': [struct_param('in', 'V2P', v2p(v2p_fields, 'varyingInput'), 'varyingInput', 0, 'fragment')],
          'result': {'stage': 'fragment', 'semanticName': 'SV_Target', 'binding': {'kind': 'varyingOutput', 'index': 0},
                     'type': f32(4)}}
    return vs, fs


def shader_files(name, params, vs, fs):
    files = {}
    for target in ('hlsl', 'metal'):
        files['%s.%s.vs.json' % (name, target)] = {'parameters': params(target), 'entryPoints': [vs]}
        files['%s.%s.fs.json' % (name, target)] = {'parameters': params(target), 'entryPoints': [fs]}
    files['%s.spirv.json' % name] = {'parameters': params('spirv'), 'entryPoints': [vs, fs]}
    return {k: copy.deepcopy(v) for k, v in files.items()}


def fixture():
    files = {}
    vs, fs = entry_points([('inst', [(s, 4) for s in RECT_INST])], RECT_V2P)
    files.update(shader_files('rect', lambda t: [
        block('u', 'RectUniforms', RECT_FIELDS, slot(t, 'constantBuffer', 0)),
        resource('t_color', slot(t, 'shaderResource', 0 if t != 'spirv' else 1), 'resource'),
        resource('s_color', slot(t, 'samplerState', 0 if t != 'spirv' else 2), 'samplerState'),
    ], vs, fs))
    vs, fs = entry_points([('vtx', [('POS', 3), ('NOR', 3), ('TEX', 2), ('COL', 3)]),
                           ('inst', [(s, 4) for s in ('XFORM0', 'XFORM1', 'XFORM2', 'XFORM3', 'REMAP')])],
                          [('texcoord', 2, 'TEXCOORD', 0), ('color', 4, 'TEXCOORD', 1)])
    files.update(shader_files('mesh', lambda t: [
        block('u', 'MeshUniforms', [('view_projection', array(4, 4), 0, 64)], slot(t, 'constantBuffer', 0)),
        block('g', 'MeshGroupUniforms', [('has_albedo', f32(1), 0, 4), ('albedo_sample_is_surface', f32(1), 4, 4),
                                         ('_padding0', f32(2), 8, 8)], slot(t, 'constantBuffer', 1)),
        resource('t_albedo', slot(t, 'shaderResource', 0 if t != 'spirv' else 2), 'resource'),
        resource('s_albedo', slot(t, 'samplerState', 0 if t != 'spirv' else 3), 'samplerState'),
    ], vs, fs))
    vs, fs = entry_points([], [('uv', 2, 'TEXCOORD', 0)])
    files.update(shader_files('crt', lambda t: [
        block('u', 'EffectUniforms', [('source_size_px', f32(2), 0, 8), ('output_size_px', f32(2), 8, 8),
                                      ('params0', f32(4), 16, 16), ('params1', f32(4), 32, 16)], slot(t, 'constantBuffer', 0)),
        resource('t_source', slot(t, 'shaderResource', 0 if t != 'spirv' else 1), 'resource'),
        resource('s_source', slot(t, 'samplerState', 0 if t != 'spirv' else 2), 'samplerState'),
    ], vs, fs))
    return files


def find(data, pred):
    """The first dict under data for which pred holds."""
    if isinstance(data, dict):
        if pred(data):
            return data
        children = data.values()
    elif isinstance(data, list):
        children = data
    else:
        return None
    for child in children:
        hit = find(child, pred)
        if hit is not None:
            return hit
    return None


class CheckShaderReflection(unittest.TestCase):
    def run_check(self, files):
        with tempfile.TemporaryDirectory(prefix='wheelhouse-reflection-test-') as d:
            for name, data in files.items():
                (Path(d) / name).write_text(json.dumps(data))
            errors, layouts = csr.check(csr.load([d]))
            return list(errors), csr.header(layouts, csr.vertex_strides(csr.load([d])), 'test')

    def test_matching_reflection_passes_and_writes_asserts(self):
        errors, header = self.run_check(fixture())
        self.assertEqual(errors, [])
        self.assertIn('StaticAssert(OffsetOf(R_CoreRectUniforms, opacity) == 8, RectUniforms_opacity_offset_8_);', header)
        self.assertIn('StaticAssert(sizeof(R_CoreRectUniforms) >= 148, RectUniforms_covers_148_bytes_);', header)
        self.assertIn('StaticAssert(sizeof(R_Rect2DInst) == 128,', header)
        self.assertIn('StaticAssert(R_CORE_MESH_VERTEX_STRIDE == 44,', header)
        self.assertIn('#if R_BACKEND == R_BACKEND_METAL\n//- EffectUniforms -> R_MTL_EffectUniforms', header)
        self.assertNotIn('_padding0', header)

    def test_offset_disagreement_names_shader_target_field(self):
        files = fixture()
        field = find(files['rect.metal.vs.json'], lambda d: d.get('name') == 'opacity')
        field['binding']['offset'] = 12
        errors, _ = self.run_check(files)
        self.assertIn('rect.slang metal: u.opacity offset, size: expected 8, 4 (as in rect hlsl), got 12, 4', errors)

    def test_binding_slot_mismatch_names_parameter(self):
        files = fixture()
        files['mesh.hlsl.fs.json']['parameters'][1]['binding']['index'] = 2
        errors, _ = self.run_check(files)
        self.assertEqual(errors, ['mesh.slang hlsl: parameter g binding: expected constantBuffer 1, got constantBuffer 2'])

    def test_unknown_block_and_parameter_fail(self):
        files = fixture()
        for name in ('crt.hlsl.vs.json', 'crt.hlsl.fs.json'):
            files[name]['parameters'].append(resource('extra', {'kind': 'shaderResource', 'index': 1}, 'resource'))
        errors, _ = self.run_check(files)
        self.assertIn('crt.slang hlsl: parameter extra: expected a binding listed in BINDINGS, got none listed', errors)

    def test_gl_two_shared_blocks_fail(self):
        files = fixture()
        files['rect.spirv.json']['parameters'].append(block('w', 'EffectUniforms', [], slot('spirv', '', 3)))
        errors, _ = self.run_check(files)
        self.assertIn('rect.slang spirv: uniform blocks on GL binding point 0: expected at most one, got EffectUniforms, RectUniforms', errors)

    def test_gl_two_textures_fail(self):
        files = fixture()
        files['crt.spirv.json']['parameters'].append(resource('t_extra', slot('spirv', '', 3), 'resource'))
        errors, _ = self.run_check(files)
        self.assertIn('crt.slang spirv: textures on GL unit 0: expected at most one, got t_extra, t_source', errors)

    def test_varying_missing_or_retyped_fails(self):
        files = fixture()
        fs = files['rect.spirv.json']['entryPoints'][1]
        softness = find(fs, lambda d: d.get('name') == 'softness')
        softness['binding']['index'] = 9
        tint = find(files['rect.hlsl.fs.json'], lambda d: d.get('name') == 'tint')
        tint['type'] = f32(3)
        errors, _ = self.run_check(files)
        self.assertIn('rect.slang spirv: fs_main input in.softness (location 9): expected a vs_main output there, got none', errors)
        self.assertIn('rect.slang hlsl: fs_main input in.tint (TEXCOORD2): expected float32x4, as vs_main writes it, got float32x3', errors)

    def test_vertex_attributes_out_of_order_fail(self):
        files = fixture()
        inst = find(files['mesh.metal.vs.json'], lambda d: d.get('name') == 'inst')
        inst['binding']['index'] = 3
        errors, _ = self.run_check(files)
        self.assertIn('mesh.slang metal: vs_main inputs vtx.vtx_3 and inst.inst_0: expected distinct attributes, got both at attribute 3', errors)

    def test_hlsl_semantics_must_match_input_layout(self):
        files = fixture()
        sty = find(files['rect.hlsl.vs.json'], lambda d: d.get('semanticName') == 'STY')
        sty['semanticName'] = 'STYLE'
        errors, _ = self.run_check(files)
        self.assertEqual(len(errors), 1)
        self.assertTrue(errors[0].startswith('rect.slang hlsl: vs_main input semantics: expected'), errors[0])

    def test_header_compiles_against_render_core(self):
        cc = shutil.which('cc')
        if cc is None:
            self.skipTest('no C compiler')
        _, header = self.run_check(fixture())
        with tempfile.TemporaryDirectory(prefix='wheelhouse-reflection-test-') as d:
            (Path(d) / 'shader_layouts.h').write_text(header)
            tu = Path(d) / 'tu.c'

            def compile_with(extra):
                tu.write_text('#include "base/base_inc.h"\n'
                              'typedef struct WM_Window { U64 u64[1]; } WM_Window;\n'
                              '#define R_BACKEND_STUB 0\n#define R_BACKEND_D3D11 1\n#define R_BACKEND_OPENGL 2\n'
                              '#define R_BACKEND_METAL 3\n#define R_BACKEND R_BACKEND_STUB\n'
                              '#include "render/render_core.h"\n' + extra + '#include "shader_layouts.h"\n')
                return subprocess.run([cc, '-fsyntax-only', '-I', str(ROOT / 'native/src'), '-I', d, str(tu)],
                                      capture_output=True, text=True)
            ok = compile_with('')
            self.assertEqual(ok.returncode, 0, ok.stderr)
            # A C struct that drifts from the shader no longer compiles.
            header = header.replace('OffsetOf(R_CoreRectUniforms, opacity) == 8', 'OffsetOf(R_CoreRectUniforms, opacity) == 12')
            (Path(d) / 'shader_layouts.h').write_text(header)
            broken = compile_with('')
            self.assertNotEqual(broken.returncode, 0)
            self.assertIn('RectUniforms_opacity_offset_8', broken.stderr)


if __name__ == '__main__':
    unittest.main()
