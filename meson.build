project('retro-glquake', 'cpp',
  default_options: ['warning_level=1', 'buildtype=debug'])

sdl3 = dependency('sdl3')

dependencies = [sdl3]
cpp_args = []
link_args = []

if host_machine.system() == 'darwin'
  cpp_args += ['-DGL_SILENCE_DEPRECATION']
  link_args += ['-framework', 'OpenGL', '-Wl,-undefined,dynamic_lookup']
else
  dependencies += [
    dependency('gl'),
    dependency('glu'),
  ]
endif

executable('quake', [
    'src/quake.cpp',
  ],
  dependencies: dependencies,
  cpp_args: cpp_args,
  link_args: link_args)
