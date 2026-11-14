/*
 * tilt-move - while a window is dragged with the move plugin, it turns in
 * 3D perspective, following the motion: the faster it moves, the further
 * it leans (the trailing edge goes back into the screen), and it springs
 * back flat when the motion stops or the window is dropped. In the style
 * of Compiz's "Freewins"/3D tilt seen in Compiz Fusion videos.
 *
 * MIT licence, as wayfire-plugins-extra.
 *
 * How: the move plugin's shared drag state (move_drag::core_drag_t) emits a
 * signal on every pointer motion of a drag. The pointer velocity (smoothed)
 * gives target angles about the y axis (horizontal motion) and the x axis
 * (vertical motion); a 60 Hz timer lets the angles follow the targets and
 * return to zero. The rotation is a view_3d_transformer_t on the window,
 * below the move-drag and wobbly transformers, so the window is still drawn
 * by the drag overlay and wobbly bends the tilted image.
 */
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
const std::string tilt_transformer_name = "tilt-move";
constexpr int TICK_MS = 16;

double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
}

class wayfire_tilt_move_t : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag;

    wf::option_wrapper_t<double> strength{"tilt-move/strength"};
    wf::option_wrapper_t<double> max_angle{"tilt-move/max_angle"};
    wf::option_wrapper_t<double> response{"tilt-move/response"};
    wf::option_wrapper_t<bool> invert{"tilt-move/invert"};

    /* the window which is tilted (still being dragged, or settling) */
    wayfire_toplevel_view view = nullptr;
    bool dragging = false;

    wf::pointf_t last_pos;
    double last_motion = 0; // time of the last motion event, ms
    double last_tick = 0;
    double vx = 0, vy = 0; // smoothed pointer velocity, px/s
    double ang_y = 0, ang_x = 0; // current angles, degrees

    wf::wl_timer<true> tick;

    std::shared_ptr<wf::scene::view_3d_transformer_t> get_tr()
    {
        if (!view)
        {
            return nullptr;
        }

        return view->get_transformed_node()->get_transformer<wf::scene::view_3d_transformer_t>(
            tilt_transformer_name);
    }

    void attach(wayfire_toplevel_view v, wf::pointf_t pos)
    {
        if (view && (view != v))
        {
            detach();
        }

        if (!view)
        {
            view = v;
            view->connect(&on_unmap);
            ang_x = ang_y = 0;
        }

        if (!get_tr())
        {
            auto tr = std::make_shared<wf::scene::view_3d_transformer_t>(view);
            view->get_transformed_node()->add_transformer(tr,
                wf::TRANSFORMER_HIGHLEVEL - 2, tilt_transformer_name);
        }

        dragging    = true;
        last_pos    = pos;
        last_motion = last_tick = now_ms();
        vx = vy = 0;
        if (!tick.is_connected())
        {
            tick.set_timeout(TICK_MS, [=] () { return step(); });
        }
    }

    void detach()
    {
        if (view)
        {
            if (get_tr())
            {
                view->get_transformed_node()->rem_transformer(tilt_transformer_name);
            }

            on_unmap.disconnect();
        }

        view     = nullptr;
        dragging = false;
        vx = vy = ang_x = ang_y = 0;
    }

    void apply()
    {
        auto tr = get_tr();
        if (!tr)
        {
            return;
        }

        const double sign = invert ? -1.0 : 1.0;
        view->get_transformed_node()->begin_transform_update();
        /* positive vx (moving right): rotation about +y by -angle sends the
         * left (trailing) edge back; positive vy (moving down): rotation
         * about +x by -angle sends the top (trailing) edge back (measured
         * in the headless test, docs Step 40). */
        tr->rotation =
            glm::rotate(glm::mat4(1.0), (float)glm::radians(-sign * ang_y), glm::vec3(0, 1, 0)) *
            glm::rotate(glm::mat4(1.0), (float)glm::radians(-sign * ang_x), glm::vec3(1, 0, 0));
        view->get_transformed_node()->end_transform_update();
    }

    /* timer callback: returns whether to keep ticking */
    bool step()
    {
        if (!view)
        {
            return false;
        }

        const double t  = now_ms();
        const double dt = std::clamp(t - last_tick, 1.0, 100.0);
        last_tick = t;

        /* no motion for a while: the pointer stands still, velocity fades */
        if (!dragging || (t - last_motion > 40))
        {
            const double k = std::exp(-dt / 70.0);
            vx *= k;
            vy *= k;
        }

        const double lim = max_angle;
        const double target_y = dragging ? std::clamp(strength * vx / 1000.0, -lim, lim) : 0.0;
        const double target_x = dragging ? std::clamp(strength * vy / 1000.0, -lim, lim) : 0.0;
        /* first-order follow: response = 1/s */
        const double f = 1.0 - std::exp(-(double)response * dt / 1000.0);
        ang_y += (target_y - ang_y) * f;
        ang_x += (target_x - ang_x) * f;

        if (!dragging && (std::abs(ang_x) < 0.05) && (std::abs(ang_y) < 0.05))
        {
            detach();
            return false;
        }

        apply();
        return true;
    }

    wf::signal::connection_t<wf::move_drag::drag_motion_signal> on_motion =
        [=] (wf::move_drag::drag_motion_signal *ev)
    {
        auto v = drag->view;
        if (!v)
        {
            return;
        }

        if ((v != view) || !dragging)
        {
            attach(v, ev->current_position);
            return;
        }

        const double t  = now_ms();
        const double dt = t - last_motion;
        if (dt < 1)
        {
            /* several events in the same millisecond: accumulate */
            return;
        }

        const double ix = (ev->current_position.x - last_pos.x) * 1000.0 / dt;
        const double iy = (ev->current_position.y - last_pos.y) * 1000.0 / dt;
        /* exponential smoothing, time constant 50 ms */
        const double a = 1.0 - std::exp(-dt / 50.0);
        vx += (ix - vx) * a;
        vy += (iy - vy) * a;
        last_pos    = ev->current_position;
        last_motion = t;
    };

    wf::signal::connection_t<wf::move_drag::drag_done_signal> on_done =
        [=] (wf::move_drag::drag_done_signal*)
    {
        /* let it spring back flat (the timer removes the transformer) */
        dragging = false;
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_unmap =
        [=] (wf::view_unmapped_signal*)
    {
        tick.disconnect();
        detach();
    };

  public:
    void init() override
    {
        drag->connect(&on_motion);
        drag->connect(&on_done);
    }

    void fini() override
    {
        tick.disconnect();
        detach();
        on_motion.disconnect();
        on_done.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_tilt_move_t);
