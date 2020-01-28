autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, wlroots, wfconfig, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

joinviews = shared_module('join-views', 'join-views.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))


subdir('network-keyboard')

