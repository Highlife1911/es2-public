"""The sim suites of the in-vitro harness, one binary each.

Each suite is a case header that compares one module's original machine code
with its own code in sim/src through the full service dispatch. A suite's
header includes the case headers it builds on (`#pragma once`), so its binary
compiles only that closure; a change to one suite's cases rebuilds and reruns
that suite and the suites including it, never the hook groups.

SIM_SUITES maps the test-name stem (`differential_<stem>_test`) to:
  header: the case header,
  entry:  the C expression that runs the suite and returns its status,
  shards: Bazel test shards (the suite splits its parts with
          suite_part_in_shard, suite_sharding.h),
  size:   the Bazel test size ("large" where an 8-job run on a loaded
          machine came near the medium timeout, e.g. gfx: 153 s alone,
          over 600 s under load),
  data:   extra data labels beyond the reference binaries.

One line per suite, sorted by stem: a new suite is one inserted line, so two
packages adding suites do not touch the same line.
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

SIM_SUITES = {
    "actor_draw": struct(header = "actor_draw_cases.h", entry = "actor_draw_cases()", shards = 1, size = "medium", data = []),
    "adapter_leaves": struct(header = "adapter_leaves_cases.h", entry = "adapter_leaves_cases()", shards = 1, size = "medium", data = []),
    "audio": struct(header = "audio_cases.h", entry = "audio_cases()", shards = 1, size = "large", data = []),
    "cockpit": struct(header = "cockpit_cases.h", entry = "cockpit_cases()", shards = 1, size = "medium", data = []),
    "cockpit_pixels": struct(header = "cockpit_pixels_cases.h", entry = "cockpit_pixels_cases()", shards = 1, size = "medium", data = []),
    "cockpit_rest": struct(header = "cockpit_rest_cases.h", entry = "cockpit_rest_cases()", shards = 1, size = "medium", data = []),
    "dialog_ctors": struct(header = "dialog_ctors_cases.h", entry = "dialog_ctors_cases()", shards = 1, size = "medium", data = []),
    "dialogs": struct(header = "dialogs_cases.h", entry = "dialogs_cases()", shards = 1, size = "medium", data = []),
    "display": struct(header = "display_cases.h", entry = "display_cases()", shards = 6, size = "large", data = []),
    "file_streams": struct(header = "file_streams_cases.h", entry = "file_streams_cases()", shards = 1, size = "medium", data = []),
    "fill_rest": struct(header = "fill_rest_cases.h", entry = "fill_rest_cases()", shards = 4, size = "medium", data = []),
    "frustum": struct(header = "frustum_cases.h", entry = "frustum_cases()", shards = 1, size = "medium", data = []),
    "fullscreen": struct(header = "fullscreen_cases.h", entry = "fullscreen_cases()", shards = 1, size = "medium", data = []),
    "gadgets": struct(header = "gadgets_cases.h", entry = "gadgets_cases()", shards = 1, size = "medium", data = []),
    "gap_roots_shell": struct(header = "gap_roots_shell_cases.h", entry = "gap_roots_shell_cases()", shards = 1, size = "medium", data = []),
    "gap_roots_sim": struct(header = "gap_roots_sim_cases.h", entry = "gap_roots_sim_cases()", shards = 1, size = "medium", data = []),
    "gauges": struct(header = "gauges_cases.h", entry = "gauges_cases()", shards = 1, size = "medium", data = []),
    "gfx": struct(header = "gfx_cases.h", entry = "gfx_cases()", shards = 1, size = "large", data = []),
    "gfx_blit": struct(header = "gfx_blit_cases.h", entry = "gfx_blit_cases()", shards = 1, size = "large", data = []),
    "gfx_objects": struct(header = "gfx_objects_cases.h", entry = "gfx_objects_cases()", shards = 1, size = "large", data = []),
    "gfx_polygon": struct(header = "gfx_polygon_cases.h", entry = "gfx_polygon_cases()", shards = 1, size = "medium", data = []),
    "gl_fill": struct(header = "gl_cases.h", entry = "gl_fill_cases()", shards = 6, size = "large", data = []),
    "gl_gouraud": struct(header = "gouraud_cases.h", entry = "gl_gouraud_cases()", shards = 6, size = "large", data = []),
    "hdd_map_leaves": struct(header = "hdd_map_leaves_cases.h", entry = "hdd_map_leaves_cases()", shards = 1, size = "large", data = []),
    "hud_cockpit": struct(header = "hud_cockpit_cases.h", entry = "hud_cockpit_cases()", shards = 1, size = "large", data = []),
    "hud_frame": struct(header = "hud_frame_cases.h", entry = "hud_frame_cases()", shards = 1, size = "medium", data = []),
    "hud_mfd_leaves": struct(header = "hud_mfd_leaves_cases.h", entry = "hud_mfd_leaves_cases()", shards = 1, size = "medium", data = []),
    "hud_pixels": struct(header = "hud_pixels_cases.h", entry = "hud_pixels_cases()", shards = 1, size = "large", data = []),
    "hud_radio": struct(header = "hud_radio_cases.h", entry = "hud_radio_cases()", shards = 1, size = "medium", data = []),
    "hud_widgets": struct(header = "hud_widgets_cases.h", entry = "hud_widgets_cases()", shards = 1, size = "large", data = []),
    "init_records": struct(header = "init_records_cases.h", entry = "init_records_cases()", shards = 1, size = "medium", data = []),
    "input_devices": struct(header = "input_devices_cases.h", entry = "input_devices_cases()", shards = 1, size = "medium", data = []),
    "input_layer": struct(header = "input_layer_cases.h", entry = "input_layer_cases()", shards = 1, size = "large", data = []),
    "joystick_page": struct(header = "joystick_page_cases.h", entry = "joystick_page_cases()", shards = 1, size = "medium", data = []),
    "joystick_prefs": struct(header = "joystick_prefs_cases.h", entry = "joystick_prefs_cases()", shards = 1, size = "medium", data = []),
    "load_rest": struct(header = "load_rest_cases.h", entry = "load_rest_cases()", shards = 1, size = "medium", data = []),
    "loop": struct(header = "loop_cases.h", entry = "loop_cases()", shards = 4, size = "large", data = []),
    "mfd": struct(header = "mfd_cases.h", entry = "mfd_cases()", shards = 4, size = "large", data = []),
    "mfd_rest": struct(header = "mfd_rest_cases.h", entry = "mfd_rest_cases()", shards = 8, size = "large", data = []),
    "mfd_rest2": struct(header = "mfd_rest2_cases.h", entry = "mfd_rest2_cases()", shards = 1, size = "medium", data = []),
    "mission_abort_cleanup": struct(header = "mission_abort_cleanup_cases.h", entry = "mission_abort_cleanup_cases()", shards = 1, size = "medium", data = []),
    "panel": struct(header = "panel_cases.h", entry = "panel_cases()", shards = 1, size = "medium", data = []),
    "panel_bars": struct(header = "panel_bars_cases.h", entry = "panel_bars_cases()", shards = 1, size = "medium", data = []),
    "panel_gadgets": struct(header = "panel_gadgets_cases.h", entry = "panel_gadgets_cases()", shards = 1, size = "medium", data = []),
    "pod_gauges": struct(header = "pod_gauges_cases.h", entry = "pod_gauges_cases()", shards = 1, size = "medium", data = []),
    "poly": struct(header = "poly_cases.h", entry = "poly_cases()", shards = 1, size = "medium", data = []),
    "poly_rest": struct(header = "pfill_cases.h", entry = "poly_rest_cases()", shards = 1, size = "medium", data = []),
    "present": struct(header = "present_cases.h", entry = "present_cases()", shards = 1, size = "medium", data = []),
    "presentation_loads": struct(header = "presentation_loads_cases.h", entry = "presentation_loads_cases()", shards = 1, size = "medium", data = []),
    "presentation_roots": struct(header = "presentation_roots_cases.h", entry = "presentation_roots_cases()", shards = 1, size = "medium", data = []),
    "renderer_leaves": struct(header = "renderer_leaves_cases.h", entry = "renderer_leaves_cases()", shards = 3, size = "medium", data = []),
    "runtime_leaves": struct(header = "runtime_leaves_cases.h", entry = "runtime_leaves_cases()", shards = 1, size = "medium", data = []),
    "scene": struct(header = "scene_cases.h", entry = "scene_cases()", shards = 1, size = "medium", data = []),
    "shade_ranges": struct(header = "shade_ranges_cases.h", entry = "shade_ranges_cases()", shards = 1, size = "large", data = []),
    "shape_draw": struct(header = "shape_draw_cases.h", entry = "shape_draw_cases()", shards = 1, size = "large", data = []),
    "shell_rest": struct(header = "shell_rest_cases.h", entry = "shell_rest_cases()", shards = 1, size = "medium", data = []),
    "startup": struct(header = "startup_cases.h", entry = "startup_suite()", shards = 1, size = "medium", data = ["@es2_data//:cd"]),
    "stream_writes": struct(header = "stream_writes_cases.h", entry = "stream_writes_cases()", shards = 1, size = "medium", data = []),
    "terrain_draw": struct(header = "terrain_draw_cases.h", entry = "terrain_draw_cases()", shards = 1, size = "medium", data = []),
    "view": struct(header = "view_cases.h", entry = "view_cases()", shards = 1, size = "medium", data = []),
    "widget_leaves": struct(header = "widget_leaves_cases.h", entry = "widget_leaves_cases()", shards = 1, size = "medium", data = []),
}

_PREFIX = "tools/reference-game/invitro"

def _sim_suite_header_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.name + "/sim_suite.h")
    ctx.actions.write(out, "\n".join([
        "/* Generated by suites.bzl (sim_suite_header); do not edit. */",
        "#include \"%s/%s\"" % (_PREFIX, ctx.attr.header),
        "#define ES2_SIM_SUITE_NAME \"%s\"" % ctx.attr.suite,
        "static int sim_suite_entry(void) { return %s; }" % ctx.attr.entry,
        "",
    ]))
    return [DefaultInfo(files = depset([out]))]

_sim_suite_header = rule(
    implementation = _sim_suite_header_impl,
    attrs = {
        "suite": attr.string(mandatory = True),
        "header": attr.string(mandatory = True),
        "entry": attr.string(mandatory = True),
    },
)

def sim_suites(name, srcs, deps, copts, linkopts, data, target_compatible_with):
    """One harness binary and one test per SIM_SUITES entry.

    Args:
      name: unused label stem (the macro creates per-suite targets).
      srcs: the shared sources (sim_main.c, harness_base.h, the case headers).
      deps: the libraries every suite links.
      copts: compiler options.
      linkopts: linker options (image base, entry, stack).
      data: data every suite test reads.
      target_compatible_with: platform constraints.
    """
    for stem, suite in SIM_SUITES.items():
        header = "differential_%s_suite_header" % stem
        _sim_suite_header(
            name = header,
            suite = stem,
            header = suite.header,
            entry = suite.entry,
            testonly = True,
        )
        harness = "differential_%s_harness" % stem
        cc_binary(
            name = harness,
            testonly = True,
            srcs = srcs + [":" + header],
            # sim_main.c includes "sim_suite.h" from the generated directory.
            copts = copts + ["-iquote", "$(GENDIR)/%s/%s" % (_PREFIX, header)],
            deps = deps,
            linkopts = linkopts,
            target_compatible_with = target_compatible_with,
        )
        cc_test(
            name = "differential_%s_test" % stem,
            srcs = ["launch.c"],
            data = [":" + harness] + data + suite.data,
            args = ["$(rootpath :%s)" % harness],
            shard_count = suite.shards if suite.shards > 1 else None,
            size = suite.size,
            # One unit of .bazelrc's membw (three): sim shards are bound by
            # memory bandwidth, not by CPU.
            tags = ["resources:membw:1"],
            target_compatible_with = target_compatible_with,
        )

def sim_suite_tests():
    """The labels of the sim-suite tests, for the test_suites."""
    return [":differential_%s_test" % stem for stem in SIM_SUITES]
