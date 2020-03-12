autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, wlroots, wfconfig, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

joinviews = shared_module('join-views', 'join-views.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

keycolor = shared_module('keycolor', 'keycolor.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

magnifier = shared_module('mag', 'mag.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

backgroundview = shared_module('background-view', 'background-view.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

fullscreen = shared_module('fullscreen', 'fullscreen.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

bench = shared_module('bench', 'bench.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

showrepaint = shared_module('showrepaint', 'showrepaint.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))


subdir('network-keyboard')

