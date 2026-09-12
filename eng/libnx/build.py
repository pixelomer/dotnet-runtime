#!/usr/bin/env python3
"""Build the Horizon runtime and dependencies from this source checkout."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
from support import digest, download, extract_tar, git_source, read_mirrors, run

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def options():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--flavor', choices=['coreclr', 'nativeaot', 'mono'], required=True)
    p.add_argument('--llvm', action='store_true', help='Build the real Mono LLVM cross compiler and runtime helpers')
    p.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    p.add_argument('--dependencies-only', action='store_true')
    p.add_argument('--source-mirrors', type=Path, help='Optional JSON mapping of canonical Git URLs to source mirrors')
    p.add_argument('--devkitpro', type=Path, default=Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')))
    a = p.parse_args()
    if a.jobs < 1: p.error('--jobs must be positive')
    if a.llvm and a.flavor != 'mono': p.error('--llvm applies only to Mono')
    return a


def prepare(a):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise RuntimeError('This cross-build recipe requires Linux x86-64')
    original = a.devkitpro.resolve()
    for relative in ['devkitA64/bin/aarch64-none-elf-gcc', 'tools/bin/elf2nro', 'libnx/include/switch.h']:
        if not (original / relative).is_file(): raise RuntimeError('Missing devkitPro prerequisite: ' + relative)
    for tool in ['git', 'cmake', 'make', 'patch', 'clang', 'gcc', 'g++', 'ninja']:
        if not shutil.which(tool): raise RuntimeError('Missing host prerequisite: ' + tool)
    if any(c.isspace() for c in str(ROOT) + str(original)):
        raise RuntimeError('Upstream native build scripts require paths without whitespace')
    out = ROOT / 'artifacts/horizon'
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((HERE / 'dependencies.json').read_text())
    libnx = git_source(lock['libnx'], out / 'sources/libnx', read_mirrors(a.source_mirrors))
    env = os.environ.copy()
    env.update(DEVKITPRO=str(original), DEVKITA64=str(original / 'devkitA64'))
    env['PATH'] = str(original / 'devkitA64/bin') + ':' + str(original / 'tools/bin') + ':' + env['PATH']
    run(['make', '-C', libnx, '-j', a.jobs], env=env)
    # A private SDK overlay keeps package-manager installations unchanged.
    overlay = out / 'devkitpro'
    overlay.mkdir(exist_ok=True)
    for path in original.iterdir():
        if path.name == 'libnx': continue
        destination = overlay / path.name
        if not destination.exists(): destination.symlink_to(path, target_is_directory=path.is_dir())
    nx = overlay / 'libnx'
    nx.mkdir(exist_ok=True)
    for name in ['include', 'lib']:
        shutil.copytree(libnx / 'nx' / name, nx / name, dirs_exist_ok=True)
    shutil.copytree(libnx / 'nx/external/bsd/include', nx / 'include', dirs_exist_ok=True)
    for name in ['switch.specs', 'switch.ld', 'switch_rules', 'default_icon.jpg']:
        shutil.copy2(libnx / 'nx' / name, nx / name)
    env.update(DEVKITPRO=str(overlay), DEVKITA64=str(original / 'devkitA64'), ROOTFS_DIR=str(overlay))
    icu = out / 'icu'
    stamp = icu / 'source-manifest.json'
    identity = {'source': lock['icu'], 'patch': digest(HERE / 'icu-libnx.patch'),
                'compiler': subprocess.check_output([str(original / 'devkitA64/bin/aarch64-none-elf-gcc'), '-dumpfullversion'], text=True).strip(),
                'libnx': lock['libnx']['revision']}
    if not stamp.exists() or json.loads(stamp.read_text()) != identity:
        archive = download(lock['icu'], out / 'downloads/icu4c-77_1-src.tgz')
        source_root = out / 'sources/icu'
        if source_root.exists(): shutil.rmtree(source_root)
        source_root.mkdir(parents=True)
        extract_tar(archive, source_root)
        run(['patch', '-p1', '-i', HERE / 'icu-libnx.patch'], cwd=source_root)
        source = source_root / 'icu/source'
        host = out / 'icu-host-build'
        target = out / 'icu-target-build'
        for build in [host, target]:
            if build.exists(): shutil.rmtree(build)
            build.mkdir()
        host_env = env.copy()
        for name in ['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'ROOTFS_DIR']:
            host_env.pop(name, None)
        run(['bash', source / 'configure', '--disable-tests', '--disable-samples'], cwd=host, env=host_env)
        run(['make', '-j', a.jobs], cwd=host, env=host_env)
        arch = '-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE'
        target_env = env | {'CC': str(original / 'devkitA64/bin/aarch64-none-elf-gcc'),
                            'CXX': str(original / 'devkitA64/bin/aarch64-none-elf-g++'),
                            'CFLAGS': arch + ' -g -O2 -ffunction-sections',
                            'CXXFLAGS': arch + ' -g -O2 -ffunction-sections -fno-exceptions',
                            'LDFLAGS': '-specs=' + str(nx / 'switch.specs') + ' -g ' + arch}
        run(['bash', source / 'configure', '--disable-renaming', '--disable-shared', '--enable-static',
             '--disable-dyload', '--with-data-packaging=archive', '--with-cross-build=' + str(host),
             '--host=aarch64-pc-linux-gnu', '--disable-icuio', '--disable-extras', '--disable-tests',
             '--disable-samples', '--disable-tools', '--prefix=' + str(icu)], cwd=target, env=target_env)
        run(['make', '-j', a.jobs], cwd=target, env=target_env)
        run(['make', 'install'], cwd=target, env=target_env)
        stamp.write_text(json.dumps(identity, indent=2) + '\n')
    if not (icu / 'share/icu/77.1/icudt77l.dat').is_file():
        raise RuntimeError('ICU installation is missing globalization data')
    env.update(ICU_NX_INSTALL_DIR=str(icu), CMAKE_BUILD_PARALLEL_LEVEL=str(a.jobs),
               DOTNET_PROCESSOR_COUNT=str(a.jobs))
    (out / 'environment.json').write_text(json.dumps({k: env[k] for k in
        ['DEVKITPRO', 'DEVKITA64', 'ROOTFS_DIR', 'ICU_NX_INSTALL_DIR']}, indent=2) + '\n')
    return out, env


def build(a, out, env):
    version = json.loads((ROOT / 'global.json').read_text())['sdk']['version']
    major = int(version.split('.')[0])
    common = ['-c', 'Release', '--cross', '-a', 'arm64', '--os', 'libnx', '/p:PublicSign=true']
    cmake = '-DFEATURE_EVENT_TRACE=OFF -DFEATURE_PERFTRACING=OFF -DLIBNX_ROOT=' + env['DEVKITPRO'] + '/libnx'
    if a.flavor == 'coreclr':
        if major < 10: raise RuntimeError('CoreCLR requires the Horizon .NET10 branch')
        run(['bash', ROOT / 'src/coreclr/build-runtime.sh', 'arm64', 'release', 'cross', '-os', 'libnx',
             '-configureonly', '-subdir', 'coreclr-probe', '-cmakeargs', cmake], cwd=ROOT, env=env)
        native = ROOT / 'artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe'
        run(['cmake', '--build', native, '--target', 'coreclr_static', '--parallel', a.jobs], env=env)
        run([ROOT / 'build.sh', '-s', 'clr.corelib+libs.sfx', *common, '/p:RuntimeFlavor=CoreCLR'], cwd=ROOT, env=env)
    elif a.flavor == 'nativeaot':
        run([ROOT / 'build.sh', '-s', 'clr.nativeaotruntime+libs.native', *common,
             '/p:NativeAotSupported=true', '/p:EnableTrimAnalyzer=false', '-cmakeargs', cmake], cwd=ROOT, env=env)
        run([ROOT / 'build.sh', '-s', 'clr.nativeaotlibs', *common,
             '/p:NativeAotSupported=true', '/p:EnableTrimAnalyzer=false'], cwd=ROOT, env=env)
        if major == 9:
            sockets = out / 'sockets-reference-build'
            if sockets.exists(): shutil.rmtree(sockets)
            run([sys.executable, ROOT / 'src/coreclr/nativeaot/Runtime/libnx/build-sockets.py', '--output', sockets], cwd=ROOT, env=env)
    else:
        if a.llvm:
            host_env = env | {'ROOTFS_DIR': ''}
            run([ROOT / 'dotnet.sh', 'msbuild', 'src/mono/llvm/llvm-init.proj', '-restore', '-t:Build',
                 '/p:Configuration=Release', '/p:TargetOS=linux', '/p:TargetArchitecture=x64',
                 '/p:BuildArchitecture=x64', '/p:AotHostArchitecture=x64', '/p:AotHostOS=linux'], cwd=ROOT, env=host_env)
            libclang = ROOT / 'artifacts/obj/mono/linux.x64.Release/llvm/x64/lib/libclang.so'
            if not libclang.is_file(): raise RuntimeError('Pinned LLVM libclang is missing')
            run([ROOT / 'build.sh', '-s', 'mono.runtime+mono.corelib+libs.native+libs.sfx', *common,
                 '/p:MonoEnableLLVMRuntime=true'], cwd=ROOT, env=env)
            run([ROOT / 'build.sh', '-s', 'mono.aotcross', '-c', 'Release',
                 '/p:MonoGenerateOffsetsOSGroups=libnx', '/p:MonoLibClang=' + str(libclang),
                 '/p:MonoEnableLLVMRuntime=true'], cwd=ROOT, env=env)
        else:
            run([ROOT / 'build.sh', '-s', 'mono.runtime+mono.corelib+libs.native+libs.sfx', *common], cwd=ROOT, env=env)
            run([ROOT / 'build.sh', '-s', 'mono.aotcross', '-c', 'Release', '/p:MonoGenerateOffsetsOSGroups=libnx'], cwd=ROOT, env=env)
        run([ROOT / 'build.sh', '-s', 'mono.aotcross', '-c', 'Release', '/p:AotHostArchitecture=x64',
             '/p:AotHostOS=linux', '/p:MonoCrossAOTTargetOS=libnx', '/p:SkipMonoCrossJitConfigure=true',
             '/p:BuildMonoAOTCrossCompilerOnly=true', '/p:BuildMonoAOTCrossCompiler=true',
             '/p:MonoAOTEnableLLVM=' + str(a.llvm).lower()], cwd=ROOT, env=env | {'ROOTFS_DIR': ''})
        compiler = ROOT / 'artifacts/bin/mono/linux.x64.Release/cross/linux-x64/libnx-arm64/mono-aot-cross'
        information = subprocess.check_output([str(compiler), '--version'], text=True)
        if a.llvm and not __import__('re').search(r'LLVM:\s+yes', information):
            raise RuntimeError('Compiler does not have an active LLVM backend')
        print(information)
        run([ROOT / 'dotnet.sh', 'build', ROOT / 'src/tools/illink/src/linker/Mono.Linker.csproj',
             '-c', 'Release', '/p:PublicSign=true'], cwd=ROOT, env=env | {'ROOTFS_DIR': ''})
    record = {'flavor': a.flavor, 'llvm': a.llvm, 'host_sdk': version,
              'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'environment_file': 'environment.json'}
    (out / (a.flavor + '-build.json')).write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    args = options()
    output, environment = prepare(args)
    if not args.dependencies_only:
        build(args, output, environment)
    print('Build environment: ' + str(output / 'environment.json'))
