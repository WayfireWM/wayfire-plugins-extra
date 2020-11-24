annotate = shared_module('annotate', 'annotate.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, wlroots, wfconfig, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

background_view = shared_module('background-view', 'background-view.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

benchmark = shared_module('bench', 'bench.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

crosshair = shared_module('crosshair', 'crosshair.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

follow_focus = shared_module('follow-focus', 'follow-focus.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

force_fullscreen = shared_module('force-fullscreen', 'force-fullscreen.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
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

showrepaint = shared_module('showrepaint', 'showrepaint.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

water = shared_module('water', 'water.cpp',
    dependencies: [wayfire, wlroots, wfconfig],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

workspace_names = shared_module('workspace-names', 'workspace-names.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

if get_option('enable_nk')
    subdir('network-keyboard')
endif

