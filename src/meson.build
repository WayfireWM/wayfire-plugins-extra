autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, wlroots, wfconfig, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

background_view = shared_module('background-view', 'background-view.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

benchmark = shared_module('bench', 'bench.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

joinviews = shared_module('join-views', 'join-views.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

if get_option('enable_nk')
    subdir('network-keyboard')
endif
