#!/usr/bin/env python3
"""Run benign CM helper calculations in an isolated surfaceless EGL context.

No Wayland connection, compositor configuration, KMS changes, or network access.
This characterizes the pinned upstream defect, not a test of a patched renderer.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

PIN = 'e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb'
ENTRY = r'''
layout(local_size_x=1)in;
layout(std430,binding=0)buffer Result {float result[];};
vec4 process(float signal,int srcTF,int dstTF,vec2 srcRange,vec2 dstRange,float brightness){
 return doColorManagement(vec4(vec3(signal),1.0),1.0,srcTF,dstTF,mat3(1.0),srcRange,dstRange,0.0,mat3(1.0),10000.0,10000.0,203.0,203.0,1,1.0,brightness);
}
void main(){
 result[0]=process(0.0203,CM_TRANSFER_FUNCTION_EXT_LINEAR,CM_TRANSFER_FUNCTION_LINEAR,vec2(0,80),vec2(0,10000),1.0).r;
 result[1]=process(0.0203,CM_TRANSFER_FUNCTION_EXT_LINEAR,CM_TRANSFER_FUNCTION_LINEAR,vec2(0,10000),vec2(0,10000),1.0).r;
 result[2]=process(1.0,CM_TRANSFER_FUNCTION_EXT_LINEAR,CM_TRANSFER_FUNCTION_LINEAR,vec2(0,80),vec2(0,10000),1.0).r;
 vec4 baseline=process(1.0,CM_TRANSFER_FUNCTION_GAMMA22,CM_TRANSFER_FUNCTION_ST2084_PQ,vec2(0,80),vec2(0,10000),1.0);
 vec4 brighter=process(1.0,CM_TRANSFER_FUNCTION_GAMMA22,CM_TRANSFER_FUNCTION_ST2084_PQ,vec2(0,80),vec2(0,10000),1.5);
 result[3]=baseline.r;
 result[4]=brighter.r;
 result[5]=tfInvPQ(brighter.rgb).r*10000.0;
}
'''


def pq(nits):
    p = (nits / 10000) ** (2610 / 16384)
    return ((3424 / 4096 + 2413 / 128 * p) / (1 + 2392 / 128 * p)) ** (2523 / 32)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', required=True, type=Path, help='unmodified extracted Hyprland source at the documented pin')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    root = args.source.resolve()
    glsl = root / 'src/render/shaders/glsl'
    source_header = (root / 'src/helpers/cm/ColorManagement.hpp').read_text()
    cm = (glsl / 'cm_helpers.glsl').read_text()
    if 'return SDR_MAX_LUMINANCE; // assume Windows scRGB' not in source_header:
        ap.error('source differs from the fixed-80-nit baseline; no result claimed for this revision')
    if cm.index('pixColor = saturate(pixColor, dstxyz, sdrSaturation)') < cm.index('pixColor = fromLinearNit(pixColor, dstTF, dstTFRange)'):
        ap.error('SDR adjustment order differs from the documented baseline')
    digests = {}

    def expand(path):
        data = path.read_bytes()
        digests[str(path.relative_to(root))] = hashlib.sha256(data).hexdigest()
        lines = []
        for line in data.decode().splitlines():
            if line.lstrip().startswith('#include '):
                lines.append(expand(glsl / line.split('"')[1]))
            elif '#extension' not in line:
                lines.append(line)
        return '\n'.join(lines)

    shader = '#version 310 es\nprecision highp float;\nprecision highp int;\n#define ALLOW_INCLUDES\n' + expand(glsl / 'cm_helpers.glsl') + ENTRY
    digests['src/helpers/cm/ColorManagement.hpp'] = hashlib.sha256(source_header.encode()).hexdigest()
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'glesv2'], text=True))
    with tempfile.TemporaryDirectory(prefix='hypr-hdr-numerics-') as td:
        td = Path(td)
        (td / 'check.comp').write_text(shader)
        subprocess.run(['cc', str(Path(__file__).with_name('runner.c')), '-o', str(td / 'runner'), *flags], check=True)
        env = {**os.environ, 'MESA_SHADER_CACHE_DISABLE': 'true'}
        result = subprocess.run([str(td / 'runner'), str(td / 'check.comp')], env=env, capture_output=True, text=True, check=True)
    values = json.loads(result.stdout)
    if len(values) != 6 or not all(math.isfinite(v) for v in values):
        raise AssertionError(f'invalid shader result: {values}')
    expected = [1.624, 203, 80, pq(80), pq(80) * 1.5]
    for actual, want in zip(values, expected):
        if not math.isclose(actual, want, rel_tol=1e-4, abs_tol=1e-4):
            raise AssertionError(f'{actual=} differs from {want=}')
    if not 800 < values[5] < 820:
        raise AssertionError('post-PQ brightness counterexample was not observed')
    evidence = {
        'documented_upstream_pin': PIN,
        'source_sha256': digests,
        'renderer': result.stderr.strip(),
        'method': 'actual upstream GLSL helper code executed in a standalone surfaceless GLES compute shader; no compositor/client/KMS integration',
        'ext_linear': {'signal': 0.0203, 'declared_max_nits': 10000, 'actual_source_range_max': 80, 'observed_nits': values[0], 'expected_nits': 203},
        'controls': {'custom_range_10000_nits': values[1], 'windows_scrgb_signal_1_range_80_nits': values[2]},
        'pq_brightness': {'baseline_nits': 80, 'multiplier': 1.5, 'baseline_pq': values[3], 'observed_pq': values[4], 'observed_decoded_nits': values[5], 'linear_brightness_expected_nits': 120},
        'physical_hdr_acceptance': 'NOT TESTED',
        'fork_corrections': 'source inspected separately; this harness does not execute the fork',
    }
    output = json.dumps(evidence, indent=2, ensure_ascii=False) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')


if __name__ == '__main__':
    main()
