project(
    'spel',
    ['c', 'cpp'],
    version: '0.0.1',
    default_options: [
        'c_std=c11',
        'cpp_std=c++20',
        'default_library=static',
        'b_lto=true',
        'b_staticpic=true',
    ],
    meson_version: '>= 1.10.0',
)

sources = [
    'src/core/entry.c',
    'src/core/memory.c',
    'src/core/log.c',
    'src/core/window.c',
    'src/core/event.c',
    'src/utils/math.c',
    'src/utils/time.c',
    'src/gfx/gfx.c',
    'src/utils/path.c',
    'src/gfx/gfx_pipeline.c',
    'src/utils/terminal.c',
    'src/core/panic.c',
    'src/utils/build_info.c',
    'src/core/env_info.c',
    'src/utils/display.c',
    'src/gfx/gfx_shader_reflect.c',
    'src/input/input.c',

    'src/audio/audio_device.c',
    'src/audio/audio_source.c',
    'src/audio/audio_mixer.c',

    'src/gfx/gfx_canvas.c',
    'src/gfx/canvas/canvas_path.c',
    'src/gfx/canvas/canvas_internal_fonts.c',
    'src/gfx/canvas/canvas_font.c',

    'src/gfx/backends/gl/gfx_context_gl.c',
    'src/gfx/backends/gl/gfx_cmdlist_gl.c',
    'src/gfx/backends/gl/gfx_buffer_gl.c',
    'src/gfx/backends/gl/gfx_shader_gl.c',
    'src/gfx/backends/gl/gfx_pipeline_gl.c',
    'src/gfx/backends/gl/gfx_texture_gl.c',
    'src/gfx/backends/gl/gfx_vtable_gl.c',
    'src/gfx/backends/gl/gfx_framebuffer_gl.c',

    'src/utils/internal/xxh_x86dispatch.c',
    'src/utils/internal/xxhash.c',
    'src/utils/internal/execinfo.c',
    'src/utils/internal/stacktraverse.c',
    'src/utils/internal/spirv_reflect.c',
]

shaders = [
    'shaders/spel_internal_2d.glsl',
    'shaders/spel_internal_fullscreen.glsl',
    'shaders/spel_internal_imgui.glsl',
    'shaders/spel_internal_canvas.glsl',
    'shaders/spel_internal_text.glsl'
]

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

compile_args = []
link_args = []

rel_compile_args = []
rel_link_args = []

if cc.get_id() != 'msvc'
    rel_compile_args = [
        '-ffunction-sections',
        '-fdata-sections',
        '-fno-unwind-tables',
        '-fno-asynchronous-unwind-tables',
    ]
    rel_link_args = [
        '-Wl,--gc-sections',
        '-Wl,--strip-all',
        '-Wl,--exclude-libs,ALL',
        '-Wl,--no-as-needed',
        '-Wl,-Map=output.map'
    ]
else
    rel_compile_args += [
        '/Gy',
        '/Gw',
    ]

    rel_link_args += [
        '/OPT:REF',
        '/OPT:ICF',
    ]
endif

compile_args += ['-D_POSIX_C_SOURCE=200809L', '-D_XOPEN_SOURCE=700']

if get_option('weak')
    add_project_arguments(['-DSP_WEAK_LINK'], language: 'c')
endif

if get_option('debug')
    add_project_arguments(['-DDEBUG'], language: 'c')

    if cc.get_id() != 'msvc'
        compile_args += [
            '-fno-omit-frame-pointer',
            '-fasynchronous-unwind-tables',
            '-fno-optimize-sibling-calls',
            '-fno-lto',
            '-Wno-frame-address',
        ]
        link_args += ['-Wl,--export-dynamic']
    else
        compile_args += [
            '/Od',
            '/Oy-',
            '/Zi',
        ]
        link_args += [
            '/DEBUG',
        ]
    endif
else
    compile_args += rel_compile_args
    link_args += rel_link_args
endif

if cc.get_id() != 'msvc'
    add_global_arguments(
        [
            '-fvisibility=hidden',
        ],
        language: ['c', 'cpp'],
    )

    add_global_arguments(['-fvisibility-inlines-hidden'], language: 'cpp')
endif

if get_option('sanitizers')
    if cc.get_id() != 'msvc'
        compile_args += [
            '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer',
        ]

        link_args += [
            '-fsanitize=address,undefined',
        ]
    else
        warning('sanitizers not supported under msvc')
    endif
endif

add_global_arguments(compile_args, language: ['c', 'cpp'])
add_global_link_arguments(link_args, language: ['c', 'cpp'])

conf = configuration_data()
conf.set('SPEL_COMPILER_ID', cc.get_id())
conf.set('SPEL_COMPILER_VERSION', cc.version())
conf.set(
    'SPEL_TARGET_TRIPLE',
    host_machine.system() + '-' + host_machine.cpu_family(),
)

conf.set('SPEL_BUILD_TYPE', get_option('buildtype'))
conf.set('SPEL_COMPILER_ARGS', ' '.join(compile_args))
conf.set('SPEL_LINKER_ARGS', ' '.join(link_args))
conf.set('SPEL_MESON_BACKEND', meson.backend())
conf.set('SPEL_LINKER_ID', cc.get_linker_id())

vcs = custom_target(
    input: 'build-aux/build_info_vcs.in.h',
    output: 'build_info_vcs.generated.h',
    command: ['tools/git-commit.sh', '@INPUT@', '@OUTPUT@'],
)

conf_file = configure_file(
    input: 'build-aux/build_info.in.h',
    output: 'build_info.generated.h',
    configuration: conf,
)

sources += [vcs, conf_file]

m_dep = cc.find_library('m', required: false)

cmake = import('cmake')
cmake_args = cmake.subproject_options()

cmake_args.add_cmake_defines(
    {
        'ENABLE_GLSLANG_BINARIES': false,
        'CMAKE_BUILD_TYPE': 'MinSizeRel',
        'MSDF_ATLAS_USE_SKIA': false,
        'MSDF_ATLAS_USE_VCPKG': false,
        'MSDF_ATLAS_BUILD_STANDALONE': false,
        'MSDF_ATLAS_NO_ARTERY_FONT': true,
    },
)

# tools
sh2spv_deps = []

if dependency('glslang', required: false).found()
    sh2spv_deps += dependency('glslang')
    rel_compile_args += '-DSPEL_SYSTEM_GLSLANG'
else
    glslang_args = cmake_args
    glslang_args.set_override_option('default_library', 'shared')
    sh2spv_deps += cmake.subproject('glslang', options: cmake_args).dependency('glslang')
endif

sh2spv = executable(
    'spel-sh2spv',
    sources: ['tools/sh2spv.cpp'],
    dependencies: sh2spv_deps,
    cpp_args: [rel_compile_args],
    install: true
)

spv2h = executable(
    'spel-spv2h',
    sources: ['tools/spv2h.cpp'],
    dependencies: dependency('nlohmann_json'),
    cpp_args: [rel_compile_args],
    install: true
)

executable(
    'spel-fontgen',
    sources: [
        'tools/fontgen/fontgen.cpp',
        'tools/fontgen/fontgen_grid.cpp',
        'tools/fontgen/fontgen_bmfont.cpp',
    ],
    install: true,
    dependencies: cmake.subproject('msdf_atlas_gen', options: cmake_args).dependency('msdf-atlas-gen'),
    cpp_args: [rel_compile_args],
)

executable(
    'spel-ttf2png',
    sources: ['tools/ttf2png.cpp'],
    cpp_args: [rel_compile_args],
    install: true
)

compile_shaders = custom_target(
    'compile_shaders',
    input: shaders,
    output: 'shaders.json',
    command: [sh2spv, '@INPUT@', '-Oz'],
    console: false,
)

shader_sources = custom_target(
    input: compile_shaders,
    command: [spv2h, '@INPUT@', '@OUTPUT0@', '@OUTPUT1@'],
    output: ['gfx_internal_shaders.h', 'gfx_internal_shaders.c'],
)

executable(
    'spel-str2fnv',
    sources: ['tools/str2fnv.c'],
    cpp_args: [rel_compile_args],
    install: true
)

# Spël
dependencies = [dependency('sdl3'), m_dep]

spel_lib = library(
    'spel',
    sources: [sources, shader_sources],
    include_directories: include_directories('spel'),
    dependencies: dependencies,
    install: true
)

spel_dep = declare_dependency(
    link_with: spel_lib,
    dependencies: dependencies,
    include_directories: include_directories('spel'),
)

# sandbox
sandbox_src = ['sandbox/main.c']
executable(
    'sandbox',
    sources: sandbox_src,
    dependencies: spel_dep,
    link_args: link_args,
)

executable(
    'spel-fontview',
    sources: ['tools/fontview.cpp'],
    dependencies: spel_dep,
    link_args: link_args,
    install: true
)

# installation
install_subdir(
    'spel',
    install_dir: get_option('includedir')
)
pkg = import('pkgconfig')
pkg.generate(
    spel_lib,
    name:        'spel',
    description: 'A 2D game framework',
    version:     '0.1.0',
    filebase:    'spel',
)
