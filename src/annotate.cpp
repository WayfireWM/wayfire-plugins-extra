/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <map>
#include <math.h>
#include <memory>
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/output.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/per-output-plugin.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/plugins/common/cairo-util.hpp"
#include "wayfire/plugins/common/input-grab.hpp"

enum annotate_draw_method
{
    ANNOTATE_METHOD_DRAW,
    ANNOTATE_METHOD_LINE,
    ANNOTATE_METHOD_RECTANGLE,
    ANNOTATE_METHOD_CIRCLE,
};

struct anno_ws_overlay
{
    cairo_t *cr = nullptr;
    cairo_surface_t *cairo_surface;
    std::unique_ptr<wf::simple_texture_t> texture;
};

namespace wf
{
namespace scene
{
namespace annotate
{
class simple_node_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    node_t *self;
    damage_callback push_to_parent;
    std::shared_ptr<anno_ws_overlay> overlay, shape_overlay;
    int *x, *y, *w, *h;

  public:
    simple_node_render_instance_t(node_t *self, damage_callback push_dmg,
        int *x, int *y, int *w, int *h, std::shared_ptr<anno_ws_overlay> overlay,
        std::shared_ptr<anno_ws_overlay> shape_overlay)
    {
        this->x    = x;
        this->y    = y;
        this->w    = w;
        this->h    = h;
        this->self = self;
        this->overlay = overlay;
        this->shape_overlay  = shape_overlay;
        this->push_to_parent = push_dmg;
        self->connect(&on_node_damaged);
    }

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage)
    {
        // We want to render ourselves only, the node does not have children
        instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region)
    {
        auto ol    = this->overlay;
        wlr_box og = {*x, *y, *w, *h};
        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            if (ol->cr)
            {
                OpenGL::render_texture(wf::texture_t{ol->texture->tex},
                    target, og, glm::vec4(1.0),
                    OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
            }

            if (shape_overlay->cr)
            {
                OpenGL::render_texture(wf::texture_t{shape_overlay->texture->tex},
                    target, og, glm::vec4(1.0),
                    OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
            }
        }

        OpenGL::render_end();
    }
};


class simple_node_t : public node_t
{
    int x, y, w, h;

  public:
    std::shared_ptr<anno_ws_overlay> overlay, shape_overlay;
    simple_node_t(int x, int y, int w, int h) : node_t(false)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        overlay = std::make_shared<anno_ws_overlay>();
        shape_overlay = std::make_shared<anno_ws_overlay>();
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        // push_damage accepts damage in the parent's coordinate system
        // If the node is a transformer, it may transform the damage. However,
        // this simple nodes does not need any transformations, so the push_damage
        // callback is just passed along.
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, &x, &y, &w, &h, overlay, shape_overlay));
    }

    void do_push_damage(wf::region_t updated_region)
    {
        node_damage_signal ev;
        ev.region = updated_region;
        this->emit(&ev);
    }

    wf::geometry_t get_bounding_box() override
    {
        // Specify whatever geometry your node has
        return {x, y, w, h};
    }

    void set_position(int x, int y)
    {
        this->x = x;
        this->y = y;
    }

    void set_size(int w, int h)
    {
        this->w = w;
        this->h = h;
    }
};

std::shared_ptr<simple_node_t> add_simple_node(wf::output_t *output, int x, int y,
    int w, int h)
{
    auto subnode = std::make_shared<simple_node_t>(x, y, w, h);
    wf::scene::add_front(output->node_for_layer(wf::scene::layer::TOP), subnode);
    return subnode;
}

class wayfire_annotate_screen : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
    uint32_t button;
    wlr_box last_bbox;
    bool hook_set = false;
    annotate_draw_method draw_method;
    wf::pointf_t grab_point, last_cursor;
    std::vector<std::vector<std::shared_ptr<simple_node_t>>> overlays;
    wf::option_wrapper_t<std::string> method{"annotate/method"};
    wf::option_wrapper_t<double> line_width{"annotate/line_width"};
    wf::option_wrapper_t<bool> shapes_from_center{"annotate/from_center"};
    wf::option_wrapper_t<wf::color_t> stroke_color{"annotate/stroke_color"};
    wf::option_wrapper_t<wf::buttonbinding_t> draw_binding{"annotate/draw"};
    wf::option_wrapper_t<wf::activatorbinding_t> clear_binding{
        "annotate/clear_workspace"};
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface{
        .name = "annotate",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        overlays.resize(wsize.width);
        for (int x = 0; x < wsize.width; x++)
        {
            overlays[x].resize(wsize.height);
        }

        auto og = output->get_relative_geometry();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                overlays[x][y] = add_simple_node(output, x * og.width, y * og.height,
                    og.width, og.height);
            }
        }

        output->connect(&output_config_changed);
        output->connect(&viewport_changed);
        method.set_callback(method_changed);
        output->add_button(draw_binding, &draw_begin);
        output->add_activator(clear_binding, &clear_workspace);
        input_grab = std::make_unique<wf::input_grab_t>(this->grab_interface.name, output, nullptr, this,
            nullptr);
        method_changed();
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if ((event.button == button) && (event.state == WLR_BUTTON_RELEASED))
        {
            draw_end();
        }
    }

    wf::config::option_base_t::updated_callback_t method_changed = [=] ()
    {
        if (std::string(method) == "draw")
        {
            draw_method = ANNOTATE_METHOD_DRAW;
        } else if (std::string(method) == "line")
        {
            draw_method = ANNOTATE_METHOD_LINE;
        } else if (std::string(method) == "rectangle")
        {
            draw_method = ANNOTATE_METHOD_RECTANGLE;
        } else if (std::string(method) == "circle")
        {
            draw_method = ANNOTATE_METHOD_CIRCLE;
        } else
        {
            draw_method = ANNOTATE_METHOD_DRAW;
        }
    };

    std::shared_ptr<simple_node_t> get_node_overlay()
    {
        auto ws = output->workspace->get_current_workspace();
        return overlays[ws.x][ws.y];
    }

    std::shared_ptr<anno_ws_overlay> get_current_overlay()
    {
        auto ws = output->workspace->get_current_workspace();

        return overlays[ws.x][ws.y]->overlay;
    }

    std::shared_ptr<anno_ws_overlay> get_shape_overlay()
    {
        auto ws = output->workspace->get_current_workspace();

        return overlays[ws.x][ws.y]->shape_overlay;
    }

    wf::signal::connection_t<wf::workspace_changed_signal> viewport_changed{[this] (wf::
                                                                                    workspace_changed_signal*
                                                                                    ev)
        {
            auto wsize = output->workspace->get_workspace_grid_size();
            auto og    = output->get_relative_geometry();
            auto nvp   = ev->new_viewport;

            for (int x = 0; x < wsize.width; x++)
            {
                for (int y = 0; y < wsize.height; y++)
                {
                    overlays[x][y]->set_position((x - nvp.x) * og.width,
                        (y - nvp.y) * og.height);
                }
            }

            output->render->damage_whole();
        }
    };

    wf::button_callback draw_begin = [=] (wf::buttonbinding_t btn)
    {
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);
        output->render->damage_whole();
        grab_point = last_cursor = wf::get_core().get_cursor_position();
        button     = btn.get_button();

        grab();

        return false;
    };

    void draw_end()
    {
        auto ol = get_current_overlay();
        auto shape_overlay = get_shape_overlay();

        output->render->rem_effect(&frame_pre_paint);
        overlay_destroy(shape_overlay);
        ungrab();

        switch (draw_method)
        {
          case ANNOTATE_METHOD_LINE:
            cairo_draw_line(ol, wf::get_core().get_cursor_position());
            break;

          case ANNOTATE_METHOD_RECTANGLE:
            cairo_draw_rectangle(ol, last_cursor);
            break;

          case ANNOTATE_METHOD_CIRCLE:
            cairo_draw_circle(ol, last_cursor);
            break;

          default:
            break;
        }
    }

    void overlay_clear(std::shared_ptr<anno_ws_overlay> ol)
    {
        if (!ol->cr)
        {
            return;
        }

        cairo_clear(ol->cr);
    }

    void overlay_destroy(std::shared_ptr<anno_ws_overlay> ol)
    {
        if (!ol->cr)
        {
            return;
        }

        ol->texture.reset();
        cairo_surface_destroy(ol->cairo_surface);
        cairo_destroy(ol->cr);
        ol->cr = nullptr;
    }

    void clear()
    {
        auto ol = get_current_overlay();

        overlay_destroy(ol);

        output->render->damage_whole();
    }

    wf::signal::connection_t<wf::output_configuration_changed_signal> output_config_changed{[this] (wf::
                                                                                                    output_configuration_changed_signal
                                                                                                    *ev)
        {
            if (!ev->changed_fields)
            {
                return;
            }

            if (ev->changed_fields & wf::OUTPUT_SOURCE_CHANGE)
            {
                return;
            }

            clear();
        }
    };

    wf::activator_callback clear_workspace = [=] (auto)
    {
        clear();

        return true;
    };

    void cairo_init(std::shared_ptr<anno_ws_overlay> ol)
    {
        auto og = output->get_relative_geometry();

        if (ol->cr)
        {
            return;
        }

        ol->cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, og.width,
            og.height);
        get_node_overlay()->set_size(og.width, og.height);
        ol->cr = cairo_create(ol->cairo_surface);

        ol->texture = std::make_unique<wf::simple_texture_t>();
    }

    void cairo_clear(cairo_t *cr)
    {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }

    void cairo_surface_upload_to_texture_with_damage(
        cairo_surface_t *surface, wf::simple_texture_t& buffer, wlr_box damage_box)
    {
        buffer.width  = cairo_image_surface_get_width(surface);
        buffer.height = cairo_image_surface_get_height(surface);

        auto src = cairo_image_surface_get_data(surface);

        OpenGL::render_begin();
        if (buffer.tex == (GLuint) - 1)
        {
            GL_CALL(glGenTextures(1, &buffer.tex));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                GL_LINEAR));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                GL_LINEAR));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED));
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                buffer.width, buffer.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src));
            OpenGL::render_end();
            return;
        }

        auto og = output->get_relative_geometry();
        GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, buffer.width));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS,
            wf::clamp(damage_box.y, 0, og.height - damage_box.height)));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS,
            wf::clamp(damage_box.x, 0, og.width - damage_box.width)));

        GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0,
            wf::clamp(damage_box.x, 0, og.width - damage_box.width),
            wf::clamp(damage_box.y, 0, og.height - damage_box.height),
            damage_box.width, damage_box.height,
            GL_RGBA, GL_UNSIGNED_BYTE, src));

        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS, 0));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0));
        OpenGL::render_end();
    }

    void cairo_draw(std::shared_ptr<anno_ws_overlay> ol, wf::pointf_t from,
        wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        cairo_init(ol);
        cairo_t *cr = ol->cr;

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_move_to(cr, from.x, from.y);
        cairo_line_to(cr, to.x, to.y);
        cairo_stroke(cr);

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = std::min(from.x, to.x) - padding;
        bbox.y     = std::min(from.y, to.y) - padding;
        bbox.width = abs(from.x - to.x) + padding * 2;
        bbox.height = abs(from.y - to.y) + padding * 2;
        get_node_overlay()->do_push_damage(wf::region_t(bbox));
        cairo_surface_upload_to_texture_with_damage(ol->cairo_surface, *ol->texture,
            bbox);
    }

    bool should_damage_last()
    {
        auto shape_overlay = get_shape_overlay();
        return shape_overlay->texture && shape_overlay->texture->tex != (uint32_t)-1;
    }

    void cairo_draw_line(std::shared_ptr<anno_ws_overlay> ol, wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();
        auto shape_overlay = get_shape_overlay();
        auto from = grab_point;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol->cr;

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_move_to(cr, from.x, from.y);
        cairo_line_to(cr, to.x, to.y);
        cairo_stroke(cr);

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = std::min(from.x, to.x) - padding;
        bbox.y     = std::min(from.y, to.y) - padding;
        bbox.width = abs(from.x - to.x) + padding * 2;
        bbox.height = abs(from.y - to.y) + padding * 2;
        output->render->damage(bbox);
        wf::region_t damage_region{bbox};
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
            damage_region |= last_bbox;
        }

        damage_region &= output->get_relative_geometry();
        auto damage_extents = damage_region.get_extents();
        wlr_box damage_box  =
        {damage_extents.x1, damage_extents.y1, damage_extents.x2 - damage_extents.x1,
            damage_extents.y2 - damage_extents.y1};
        cairo_surface_upload_to_texture_with_damage(ol->cairo_surface, *ol->texture,
            damage_box);

        get_node_overlay()->do_push_damage(wf::region_t(last_bbox));
        get_node_overlay()->do_push_damage(wf::region_t(bbox));
        last_bbox = bbox;
    }

    void cairo_draw_rectangle(std::shared_ptr<anno_ws_overlay> ol, wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();
        auto shape_overlay = get_shape_overlay();
        auto from = grab_point;
        double x, y, w, h;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol->cr;

        w = fabs(from.x - to.x);
        h = fabs(from.y - to.y);

        if (shapes_from_center)
        {
            x  = from.x - w;
            y  = from.y - h;
            w *= 2;
            h *= 2;
        } else
        {
            x = std::min(from.x, to.x);
            y = std::min(from.y, to.y);
        }

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = x - padding;
        bbox.y     = y - padding;
        bbox.width = w + padding * 2;
        bbox.height = h + padding * 2;
        output->render->damage(bbox);
        wf::region_t damage_region{bbox};
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
            damage_region |= last_bbox;
        }

        damage_region &= output->get_relative_geometry();
        auto damage_extents = damage_region.get_extents();
        wlr_box damage_box  =
        {damage_extents.x1, damage_extents.y1, damage_extents.x2 - damage_extents.x1,
            damage_extents.y2 - damage_extents.y1};
        cairo_surface_upload_to_texture_with_damage(ol->cairo_surface, *ol->texture,
            damage_box);

        get_node_overlay()->do_push_damage(wf::region_t(last_bbox));
        get_node_overlay()->do_push_damage(wf::region_t(bbox));
        last_bbox = bbox;
    }

    void cairo_draw_circle(std::shared_ptr<anno_ws_overlay> ol, wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();
        auto shape_overlay = get_shape_overlay();
        auto from = grab_point;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol->cr;

        auto radius =
            glm::distance(glm::vec2(from.x, from.y), glm::vec2(to.x, to.y));

        if (!shapes_from_center)
        {
            radius /= 2;
            from.x += (to.x - from.x) / 2;
            from.y += (to.y - from.y) / 2;
        }

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_arc(cr, from.x, from.y, radius, 0, 2 * M_PI);
        cairo_stroke(cr);

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = (from.x - radius) - padding;
        bbox.y     = (from.y - radius) - padding;
        bbox.width = (radius * 2) + padding * 2;
        bbox.height = (radius * 2) + padding * 2;
        output->render->damage(bbox);
        wf::region_t damage_region{bbox};
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
            damage_region |= last_bbox;
        }

        damage_region &= output->get_relative_geometry();
        auto damage_extents = damage_region.get_extents();
        wlr_box damage_box  =
        {damage_extents.x1, damage_extents.y1, damage_extents.x2 - damage_extents.x1,
            damage_extents.y2 - damage_extents.y1};
        cairo_surface_upload_to_texture_with_damage(ol->cairo_surface, *ol->texture,
            damage_box);

        get_node_overlay()->do_push_damage(wf::region_t(last_bbox));
        get_node_overlay()->do_push_damage(wf::region_t(bbox));
        last_bbox = bbox;
    }

    wf::effect_hook_t frame_pre_paint = [=] ()
    {
        auto current_cursor = wf::get_core().get_cursor_position();
        auto shape_overlay  = get_shape_overlay();
        auto ol = get_current_overlay();

        switch (draw_method)
        {
          case ANNOTATE_METHOD_DRAW:
            cairo_draw(ol, last_cursor, current_cursor);
            break;

          case ANNOTATE_METHOD_LINE:
            cairo_draw_line(shape_overlay, current_cursor);
            break;

          case ANNOTATE_METHOD_RECTANGLE:
            cairo_draw_rectangle(shape_overlay, current_cursor);
            break;

          case ANNOTATE_METHOD_CIRCLE:
            cairo_draw_circle(shape_overlay, current_cursor);
            break;

          default:
            return;
        }

        last_cursor = current_cursor;
    };

    void grab()
    {
        if (!output->activate_plugin(&grab_interface))
        {
            return;
        }

        input_grab->grab_input(wf::scene::layer::OVERLAY, true);
    }

    void ungrab()
    {
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
    }

    void fini() override
    {
        ungrab();
        output->rem_binding(&draw_begin);
        output->rem_binding(&clear_workspace);
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& ol = overlays[x][y]->overlay;
                overlay_destroy(ol);
                ol.reset();
                auto& shape_overlay = overlays[x][y]->shape_overlay;
                overlay_destroy(shape_overlay);
                shape_overlay.reset();
                wf::scene::remove_child(overlays[x][y]);
                overlays[x][y].reset();
            }
        }

        output->render->damage_whole();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_annotate_screen>);
}
}
}
