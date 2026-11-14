/*
 * cover-switch: a window switcher in the style of Compiz's Shift Switcher
 * (Cover mode).
 *
 * The windows of the current workspace stand in a row like album covers:
 * the selected one in the middle facing the viewer, the others to the left
 * and right, turned towards the middle. Every window has a fading mirror
 * image on the floor, the desktop behind is dimmed, and the title of the
 * selected window is shown in a label at the bottom. On opening, the
 * windows fly from their places on the desktop into the row; on closing,
 * back again.
 *
 * Keys: next (default Super+Tab) / prev (Super+Shift+Tab) open the switcher
 * and move the selection; releasing the modifier picks the selected window.
 * While open: Left/Right and the mouse wheel move, Enter or a click picks,
 * Esc cancels (the previously focused window stays focused).
 *
 * Rendering: every window is rendered into its own buffer and drawn as a
 * textured quad in 3D (perspective like the cube), farthest first (painter's
 * order, no depth buffer needed); the desktop's background, bottom and top
 * layers are drawn first and darkened.
 */
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <linux/input-event-codes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

static const char *cover_vertex =
    R"(#version 100
attribute highp vec3 position;
attribute highp vec2 uvPosition;
varying highp vec2 uvpos;
uniform mat4 VP;
uniform mat4 model;

void main() {
    gl_Position = VP * model * vec4(position, 1.0);
    uvpos = uvPosition;
})";

/* premultiplied output; refl > 0: mirror image, fading out with the
 * distance from the window's bottom edge (uv.y = 1 at the floor) */
static const char *cover_fragment =
    R"(#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;
uniform highp float alpha;
uniform highp float bright;
uniform highp float refl;
uniform highp float fade;

void main() {
    highp vec4 c = texture2D(smp, uvpos);
    highp float a = alpha;
    if (refl > 0.0) {
        a *= refl * clamp(1.0 - (1.0 - uvpos.y) / fade, 0.0, 1.0);
    }
    gl_FragColor = vec4(c.rgb * bright, c.a) * a;
})";

class cover_switch_t : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t, public wf::pointer_interaction_t
{
    wf::option_wrapper_t<wf::keybinding_t> next_key{"cover-switch/next"};
    wf::option_wrapper_t<wf::keybinding_t> prev_key{"cover-switch/prev"};
    wf::option_wrapper_t<wf::animation_description_t> speed{"cover-switch/speed"};
    wf::option_wrapper_t<double> scale_opt{"cover-switch/scale"};
    wf::option_wrapper_t<double> angle_opt{"cover-switch/angle"};
    wf::option_wrapper_t<double> spacing_opt{"cover-switch/spacing"};
    wf::option_wrapper_t<double> dim_opt{"cover-switch/background_dim"};
    wf::option_wrapper_t<bool> reflection_opt{"cover-switch/reflection"};
    wf::option_wrapper_t<double> reflection_intensity{"cover-switch/reflection_intensity"};
    wf::option_wrapper_t<double> reflection_fade{"cover-switch/reflection_fade"};
    wf::option_wrapper_t<bool> title_opt{"cover-switch/show_title"};
    wf::option_wrapper_t<int> font_size{"cover-switch/font_size"};
    wf::option_wrapper_t<wf::color_t> text_color{"cover-switch/text_color"};
    wf::option_wrapper_t<wf::color_t> label_color{"cover-switch/label_color"};

    /* opening (0 -> 1) and closing (1 -> 0); the selection position */
    wf::animation::simple_animation_t appear{speed};
    wf::animation::simple_animation_t pos{speed};

    struct entry_t
    {
        wayfire_toplevel_view view;
        std::vector<wf::scene::render_instance_uptr> instances;
        wf::auxilliary_buffer_t buffer;
        wf::geometry_t box = {0, 0, 0, 0}; /* output-local, rendered part */
        int stack = 0; /* place in the real stacking order, 0 = top */
    };

    std::vector<std::unique_ptr<entry_t>> entries; /* stacking order */
    int selected = 0;
    bool active  = false;   /* accepting input; false while closing */
    bool running = false; /* hooks and render node installed */
    uint32_t activating_modifiers = 0;

    OpenGL::program_t program;
    wf::cairo_text_t label;
    std::string label_text;

    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface = {
        .name = "cover-switch",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

    class render_node_t : public wf::scene::node_t
    {
        class instance_t : public wf::scene::render_instance_t
        {
            std::shared_ptr<render_node_t> self;

          public:
            instance_t(render_node_t *self)
            {
                this->self = std::dynamic_pointer_cast<render_node_t>(self->shared_from_this());
            }

            void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::regionf_t& damage) override
            {
                self->plugin->render_windows();
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target.translated(-wf::origin(self->get_bounding_box())),
                    .damage   = damage & self->get_bounding_box(),
                });
                damage ^= self->get_bounding_box(); /* nothing below is visible */
            }

            void render(const wf::scene::render_instruction_t& data) override
            {
                self->plugin->render(data);
            }
        };

      public:
        render_node_t(cover_switch_t *plugin) : node_t(false), plugin(plugin)
        {}

        void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
            wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
        {
            if (shown_on == plugin->output)
            {
                instances.push_back(std::make_unique<instance_t>(this));
            }
        }

        wf::geometry_t get_bounding_box() override
        {
            return plugin->output->get_layout_geometry();
        }

        std::string stringify() const override
        {
            return "cover-switch";
        }

        cover_switch_t *plugin;
    };

    std::shared_ptr<render_node_t> render_node;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("cover-switch: requires the GLES2 renderer");
            return;
        }

        output->add_key(next_key, &next_binding);
        output->add_key(prev_key, &prev_binding);
        output->connect(&view_disappeared);
        input_grab = std::make_unique<wf::input_grab_t>("cover-switch", output, this, this, nullptr);
        grab_interface.cancel = [=] () { stop(); };
    }

    void fini() override
    {
        if (running)
        {
            stop();
        }

        output->rem_binding(&next_binding);
        output->rem_binding(&prev_binding);
        wf::gles::run_in_context_if_gles([&] { program.free_resources(); });
    }

    wf::key_callback next_binding = [=] (auto) { return switch_request(1); };
    wf::key_callback prev_binding = [=] (auto) { return switch_request(-1); };

    /* ---------------------------------------------------------- control */

    std::vector<wayfire_toplevel_view> workspace_views()
    {
        return output->wset()->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_CURRENT_WORKSPACE |
            wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING);
    }

    bool switch_request(int dir)
    {
        if (active)
        {
            move_selection(dir);
            return true;
        }

        if (running) /* still closing: start again from here */
        {
            stop();
        }

        auto views = workspace_views();
        if (views.empty() || !output->activate_plugin(&grab_interface))
        {
            return false;
        }

        for (auto& v : views)
        {
            auto e = std::make_unique<entry_t>();
            e->view = v;
            v->get_root_node()->gen_render_instances(e->instances,
                [=] (const wf::regionf_t&) { damage(); }, output);
            e->stack = entries.size();
            entries.push_back(std::move(e));
        }

        wf::gles::run_in_context([&]
        {
            if (program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
            {
                program.set_simple(OpenGL::compile_program(cover_vertex, cover_fragment));
            }
        });

        render_node = std::make_shared<render_node_t>(this);
        wf::scene::add_front(wf::get_core().scene(), render_node);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        input_grab->grab_input(wf::scene::layer::OVERLAY);
        activating_modifiers = wf::get_core().seat->get_keyboard_modifiers();
        running = active = true;

        /* start on the window after the focused one, like Alt+Tab */
        const int n = entries.size();
        selected = (n > 1) ? ((dir > 0) ? 1 : n - 1) : 0;
        pos.animate(0, selected);
        appear.animate(0, 1);
        update_label();
        damage();
        return true;
    }

    void move_selection(int dir)
    {
        const int n = entries.size();
        if (n < 2)
        {
            return;
        }

        int next = selected + dir;
        if ((next < 0) || (next >= n))
        {
            /* wrap around: slide in from outside the row */
            next = (next + n) % n;
            pos.animate(next - dir, next);
        } else
        {
            pos.animate(pos, next);
        }

        selected = next;
        update_label();
        damage();
    }

    /* pick = true: focus the selected window; false: keep the old focus */
    void finish(bool pick)
    {
        if (!active)
        {
            return;
        }

        active = false;
        input_grab->ungrab_input();
        if (pick && (selected < (int)entries.size()))
        {
            wf::get_core().default_wm->focus_raise_view(entries[selected]->view);
        }

        /* the windows fly back into the new stacking order */
        auto views = workspace_views();
        for (auto& e : entries)
        {
            auto it = std::find(views.begin(), views.end(), e->view);
            e->stack = (it != views.end()) ? int(it - views.begin()) : 0;
        }

        appear.animate(appear, 0);
        damage();
    }

    void stop()
    {
        if (!running)
        {
            return;
        }

        if (active)
        {
            input_grab->ungrab_input();
        }

        running = active = false;
        output->render->rem_effect(&pre_hook);
        wf::scene::remove_child(render_node);
        render_node = nullptr;
        entries.clear();
        output->deactivate_plugin(&grab_interface);
        /* the last frame showed the switcher: repaint the desktop */
        output->render->damage_whole();
    }

    void damage()
    {
        if (render_node)
        {
            wf::scene::damage_node(render_node, render_node->get_bounding_box());
        }
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        damage();
        if (!active && !appear.running())
        {
            stop();
        }
    };

    wf::signal::connection_t<wf::view_disappeared_signal> view_disappeared =
        [=] (wf::view_disappeared_signal *ev)
    {
        auto it = std::find_if(entries.begin(), entries.end(),
            [&] (auto& e) { return e->view.get() == ev->view.get(); });
        if (it == entries.end())
        {
            return;
        }

        entries.erase(it);
        if (entries.empty())
        {
            stop();
            return;
        }

        selected = std::min(selected, (int)entries.size() - 1);
        pos.animate(pos, selected);
        update_label();
    };

    /* ------------------------------------------------------------ input */

    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override
    {
        auto mod = wf::get_core().seat->modifier_from_keycode(ev.keycode);
        if ((ev.state == WLR_KEY_RELEASED) && (mod & activating_modifiers))
        {
            finish(true);
            return;
        }

        if (ev.state != WLR_KEY_PRESSED)
        {
            return;
        }

        switch (ev.keycode)
        {
          case KEY_LEFT:
            move_selection(-1);
            break;

          case KEY_RIGHT:
            move_selection(1);
            break;

          case KEY_ENTER:
          case KEY_KPENTER:
          case KEY_SPACE:
            finish(true);
            break;

          case KEY_ESC:
            selected = 0;
            finish(false);
            break;
        }
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& ev) override
    {
        if (ev.delta != 0)
        {
            move_selection(ev.delta > 0 ? 1 : -1);
        }
    }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        if (ev.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            finish(true);
        }
    }

    /* ----------------------------------------------------------- layout */

    struct pose_t
    {
        float x = 0, z = 0, angle = 0; /* centre x, depth, turn about y */
        float w = 0, h = 0, bottom = 0; /* size, y of the bottom edge */
        float bright = 1;
    };

    /* World units: y = -1 .. 1 is the screen height at z = 0 (y downwards,
     * like the screen), x = -aspect .. aspect the width. */
    float aspect()
    {
        auto og = output->get_relative_geometry();
        return og.height > 0 ? float(og.width) / og.height : 1.0f;
    }

    pose_t desktop_pose(const entry_t& e)
    {
        auto og = output->get_relative_geometry();
        pose_t p;
        float k = 2.0f / std::max(1.0, (double)og.height);
        p.w = e.box.width * k;
        p.h = e.box.height * k;
        p.x = (e.box.x + e.box.width / 2.0f) * k - aspect();
        p.bottom = (e.box.y + e.box.height) * k - 1.0f;
        return p;
    }

    pose_t cover_pose(const entry_t& e, float d)
    {
        pose_t p;
        const float a = aspect();
        p.h = 2.0f * std::clamp((double)scale_opt, 0.1, 0.9);
        float ratio = (e.box.height > 0) ? float(e.box.width) / e.box.height : 1.0f;
        p.w = p.h * ratio;
        if (p.w > 1.2f * a) /* very wide windows */
        {
            p.w = 1.2f * a;
            p.h = p.w / ratio;
        }

        p.bottom = 0.5f * 2.0f * std::clamp((double)scale_opt, 0.1, 0.9) - 0.05f;
        const float side  = 0.55f * a;
        const float space = std::clamp((double)spacing_opt, 0.05, 1.0);
        const float turn  = glm::radians(std::clamp((double)angle_opt, 0.0, 85.0));
        const float ad    = std::abs(d);
        const float sg    = (d < 0) ? -1.0f : 1.0f;
        if (ad < 1)
        {
            p.x     = d * side;
            p.angle = -d * turn;
            p.z     = -ad * 0.6f;
        } else
        {
            p.x     = sg * (side + (ad - 1) * space);
            p.angle = -sg * turn;
            p.z     = -0.6f - (ad - 1) * 0.05f;
        }

        p.bright = 1.0f - 0.25f * std::min(ad, 1.0f);
        return p;
    }

    static pose_t mix(const pose_t& a, const pose_t& b, float t)
    {
        auto m = [&] (float x, float y) { return x + (y - x) * t; };
        pose_t p;
        p.x     = m(a.x, b.x);
        p.z     = m(a.z, b.z);
        p.angle = m(a.angle, b.angle);
        p.w     = m(a.w, b.w);
        p.h     = m(a.h, b.h);
        p.bottom = m(a.bottom, b.bottom);
        p.bright = m(a.bright, b.bright);
        return p;
    }

    /* ------------------------------------------------------- rendering */

    void update_label()
    {
        if (!title_opt || entries.empty())
        {
            return;
        }

        std::string text = entries[selected]->view->get_title();
        if (text.empty())
        {
            text = entries[selected]->view->get_app_id();
        }

        if (text != label_text)
        {
            wf::cairo_text_t::params par(std::clamp(int(font_size), 8, 72), label_color, text_color,
                output->handle->scale, {0, 0}, true, true);
            label.render_text(text, par);
            label_text = text;
        }
    }

    /* every window into its own buffer (called before the frame) */
    void render_windows()
    {
        const float scale = output->handle->scale;
        for (auto& e : entries)
        {
            auto bbox = e->view->get_root_node()->get_bounding_box();
            if ((bbox.width <= 0) || (bbox.height <= 0))
            {
                e->box = {0, 0, 0, 0};
                continue;
            }

            e->box = bbox;
            e->buffer.allocate(wf::dimensions(bbox), scale);
            wf::render_target_t target{e->buffer};
            target.geometry = bbox;
            target.scale    = scale;

            wf::render_pass_params_t params;
            params.instances = &e->instances;
            params.damage    = bbox;
            params.reference_output = output;
            params.target = target;
            params.background_color = {0, 0, 0, 0};
            params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
            wf::render_pass_t::run(params);
        }
    }

    void render_views_of(const std::vector<wayfire_view>& views, const wf::render_target_t& target)
    {
        for (auto& view : views)
        {
            std::vector<wf::scene::render_instance_uptr> instances;
            view->get_transformed_node()->gen_render_instances(instances, [] (auto) {});
            wf::render_pass_params_t params;
            params.instances = &instances;
            params.damage    = view->get_transformed_node()->get_bounding_box();
            params.reference_output = output;
            params.target = target;
            wf::render_pass_t::run(params);
        }
    }

    void render(const wf::scene::render_instruction_t& data)
    {
        const float t = std::clamp((double)appear, 0.0, 1.0);

        /* the desktop without the windows, darkened */
        static wf::option_wrapper_t<wf::color_t> background_color{"core/background_color"};
        data.pass->clear(data.target.geometry, background_color);
        render_views_of(wf::collect_views_from_output(output,
            {wf::scene::layer::BACKGROUND, wf::scene::layer::BOTTOM, wf::scene::layer::TOP}),
            data.target);
        const float dim = std::clamp((double)dim_opt, 0.0, 1.0) * t;
        /* the switcher repaints the whole output every frame: clip to all of
         * it (the instruction's damage did not line up with the target) */
        data.pass->add_rect({0, 0, 0, dim}, data.target, data.target.geometry,
            wf::regionf_t{data.target.geometry});

        auto og = output->get_relative_geometry();
        const float a = aspect();
        const float D = 1.0f / std::tan(glm::radians(22.5f));
        glm::mat4 vp  = wf::gles::render_target_gl_to_framebuffer(data.target) *
            glm::scale(glm::mat4(1.0), {1, -1, 1}) *
            glm::perspective(glm::radians(45.0f), a, 0.1f, 100.0f) *
            glm::translate(glm::mat4(1.0), {0, 0, -D});

        struct item_t
        {
            entry_t *e;
            pose_t p;
            float order;
        };

        std::vector<item_t> items;
        for (int i = 0; i < (int)entries.size(); i++)
        {
            auto& e = *entries[i];
            if (e.box.width <= 0)
            {
                continue;
            }

            float d = i - (float)(double)pos;
            /* in the row: farthest from the middle first; near the
             * desktop (first/last half of opening/closing): bottom window
             * first, as they are stacked */
            float order = (t >= 0.5f) ? std::abs(d) : float(e.stack);
            items.push_back({&e, mix(desktop_pose(e), cover_pose(e, d), t), order});
        }

        /* painter's order: largest first */
        std::sort(items.begin(), items.end(), [] (auto& x, auto& y) { return x.order > y.order; });

        const float refl = reflection_opt ? std::clamp((double)reflection_intensity, 0.0, 1.0) * t : 0;
        const float fade = std::clamp((double)reflection_fade, 0.05, 1.0);
        data.pass->custom_gles_subpass([&]
        {
            program.use(wf::TEXTURE_TYPE_RGBA);
            program.uniformMatrix4f("VP", vp);
            program.uniform1f("fade", fade);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
            static const GLuint index[] = {0, 1, 2, 0, 2, 3};

            auto draw = [&] (const item_t& it, bool mirror)
            {
                const auto& p = it.p;
                float x0 = -p.w / 2, x1 = p.w / 2;
                float y0 = p.bottom - p.h, y1 = p.bottom;
                GLfloat pos[12], uv[8];
                if (!mirror)
                {
                    GLfloat q[] = {x0, y1, 0, x1, y1, 0, x1, y0, 0, x0, y0, 0};
                    GLfloat u[] = {0, 1, 1, 1, 1, 0, 0, 0};
                    std::copy(q, q + 12, pos);
                    std::copy(u, u + 8, uv);
                } else
                {
                    float y2    = y1 + p.h * fade;
                    GLfloat q[] = {x0, y2, 0, x1, y2, 0, x1, y1, 0, x0, y1, 0};
                    GLfloat u[] = {0, 1 - fade, 1, 1 - fade, 1, 1, 0, 1};
                    std::copy(q, q + 12, pos);
                    std::copy(u, u + 8, uv);
                }

                glm::mat4 model = glm::translate(glm::mat4(1.0), {p.x, 0, p.z}) *
                    glm::rotate(glm::mat4(1.0), p.angle, {0, 1, 0});
                program.attrib_pointer("position", 3, 0, pos);
                program.attrib_pointer("uvPosition", 2, 0, uv);
                program.uniformMatrix4f("model", model);
                program.uniform1f("alpha", 1.0f);
                program.uniform1f("bright", p.bright);
                program.uniform1f("refl", mirror ? refl : 0.0f);
                GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(it.e->buffer).tex_id));
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, index));
            };

            if (refl > 0)
            {
                for (auto& it : items)
                {
                    draw(it, true);
                }
            }

            for (auto& it : items)
            {
                draw(it, false);
            }

            /* the title of the selected window, at the bottom of the screen */
            if (title_opt && label.get_texture() && (t > 0))
            {
                wf::gles_texture_t gt(label.get_texture());
                auto size = label.get_size();
                const float fb_h = og.height * output->handle->scale;
                float lw    = size.width / fb_h * 2, lh = size.height / fb_h * 2;
                float yb    = 0.88f;
                GLfloat q[] = {-lw / 2, yb, 0, lw / 2, yb, 0, lw / 2, yb - lh, 0, -lw / 2, yb - lh, 0};
                GLfloat u[] = {0, 1, 1, 1, 1, 0, 0, 0};
                program.attrib_pointer("position", 3, 0, q);
                program.attrib_pointer("uvPosition", 2, 0, u);
                program.uniformMatrix4f("model", glm::mat4(1.0));
                program.uniform1f("alpha", t);
                program.uniform1f("bright", 1.0f);
                program.uniform1f("refl", 0.0f);
                GL_CALL(glBindTexture(gt.target, gt.tex_id));
                /* a fresh texture has no mipmaps: linear filtering, else black */
                GL_CALL(glTexParameteri(gt.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
                GL_CALL(glTexParameteri(gt.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, index));
            }

            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program.deactivate();
        });
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<cover_switch_t>);
