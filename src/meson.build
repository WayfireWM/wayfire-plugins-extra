autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, wlroots, wfconfig, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

joinviews = shared_module('join-views', 'join-views.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

keycolor = shared_module('keycolor', 'keycolor.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

magnifier = shared_module('magnifier', 'mag.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

backgroundview = shared_module('background-view', 'background-view.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))


subdir('network-keyboard')

