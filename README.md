# wayfire-plugins-extra
Additional plugins for Wayfire

This is a place for plugins that have external dependencies, debugging purposes or otherwise will not be included with wayfire core plugins.

# Building

Dependencies:
`wayfire`, `wlroots`, `wf-config`, `cairo`, `giomm`

```
git clone https://github.com/WayfireWM/wayfire-plugins-extra && cd wayfire-plugins-extra
meson build --prefix=/usr --buildtype=release
ninja -C build && sudo ninja -C build install
```

Then add the plugins you want to enable in [`~/.config/wayfire.ini`](https://github.com/WayfireWM/wayfire/blob/master/wayfire.ini)
