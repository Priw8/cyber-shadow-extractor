project('cyber-shadow-extractor', 'cpp',
  version : '0.2',
  default_options : ['warning_level=3',
                     'cpp_std=c++14'])

boost = dependency('boost', modules: ['program_options', 'filesystem'])
zlib  = dependency('zlib')

exe = executable('cyber-shadow-extractor', 'cyber_shadow_extractor.cpp', 'stb.cpp',
  install : true, dependencies: [ boost, zlib ])

test('basic', exe)
