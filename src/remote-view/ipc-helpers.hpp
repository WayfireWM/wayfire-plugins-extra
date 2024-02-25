#pragma once

#include "wayfire/geometry.hpp"
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>
#include <nlohmann/json.hpp>

namespace wf
{
namespace ipc
{
inline wayfire_view find_view_by_id(uint32_t id)
{
    for (auto view : wf::get_core().get_all_views())
    {
        if (view->get_id() == id)
        {
            return view;
        }
    }

    return nullptr;
}

inline wf::output_t *find_output_by_id(int32_t id)
{
    for (auto wo : wf::get_core().output_layout->get_outputs())
    {
        if ((int)wo->get_id() == id)
        {
            return wo;
        }
    }

    return nullptr;
}

inline wf::workspace_set_t *find_workspace_set_by_index(int32_t index)
{
    for (auto wset : wf::workspace_set_t::get_all())
    {
        if ((int)wset->get_index() == index)
        {
            return wset.get();
        }
    }

    return nullptr;
}

inline nlohmann::json geometry_to_json(wf::geometry_t g)
{
    nlohmann::json j;
    j["x"]     = g.x;
    j["y"]     = g.y;
    j["width"] = g.width;
    j["height"] = g.height;
    return j;
}

inline std::optional<wf::geometry_t> geometry_from_json(const nlohmann::json& j)
{
#define CHECK(field, type) (j.contains(field) && j[field].is_number_ ## type())
    if (!CHECK("x", integer) || !CHECK("y", integer) ||
        !CHECK("width", unsigned) || !CHECK("height", unsigned))
    {
        return {};
    }

#undef CHECK

    return wf::geometry_t{
        .x     = j["x"],
        .y     = j["y"],
        .width = j["width"],
        .height = j["height"],
    };
}

inline nlohmann::json point_to_json(wf::point_t p)
{
    nlohmann::json j;
    j["x"] = p.x;
    j["y"] = p.y;
    return j;
}

inline std::optional<wf::point_t> point_from_json(const nlohmann::json& j)
{
#define CHECK(field, type) (j.contains(field) && j[field].is_number_ ## type())
    if (!CHECK("x", integer) || !CHECK("y", integer))
    {
        return {};
    }

#undef CHECK

    return wf::point_t{
        .x = j["x"],
        .y = j["y"],
    };
}

inline nlohmann::json dimensions_to_json(wf::dimensions_t d)
{
    nlohmann::json j;
    j["width"]  = d.width;
    j["height"] = d.height;
    return j;
}

inline std::optional<wf::dimensions_t> dimensions_from_json(const nlohmann::json& j)
{
#define CHECK(field, type) (j.contains(field) && j[field].is_number_ ## type())
    if (!CHECK("width", integer) || !CHECK("height", integer))
    {
        return {};
    }

#undef CHECK

    return wf::dimensions_t{
        .width  = j["width"],
        .height = j["height"],
    };
}
}
}
