project(
	'wayfire-plugins-extra',
	'c',
	'cpp',
	version: '0.11.2',
	license: 'MIT',
	meson_version: '>=0.59.0',
	default_options: [
		'cpp_std=c++17',
		'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

wayfire = dependency('wayfire', version: '>=0.11.0')
giomm = dependency('giomm-2.4', required: false)
wayland_protos = dependency('wayland-protocols', version: '>=1.12')
wayland_server = dependency('wayland-server')
evdev = dependency('libevdev')

if get_option('wayfire_shadows') == true or get_option('focus_request') == true or \
    get_option('pixdecor') == true or get_option('filters') == true
    find_program('git', required: true)
endif

if get_option('wayfire_shadows') == true
    wayfire_shadows = subproject('wayfire-shadows')
endif

if get_option('focus_request') == true
    focus_request = subproject('focus-request')
endif

if get_option('pixdecor') == true
    pixdecor = subproject('pixdecor')
endif

if get_option('filters') == true
    filters = subproject('filters')
endif

if get_option('wstroke') == true
    wstroke = subproject('wstroke')
endif

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic'], language:'cpp')

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')
subdir('proto')
subdir('src')
subdir('metadata')
subdir('locale')

summary = [
	'',
	'----------------',
	'wayfire-plugins-extra @0@'.format(meson.project_version()),
	'----------------',
	''
]
message('\n'.join(summary))
