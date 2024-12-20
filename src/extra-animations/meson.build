boost = dependency('boost')

extra_animations = shared_module('extra-animations', 'plugin.cpp',
    dependencies: [wayfire, boost],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
