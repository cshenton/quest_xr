// Editor only defines to shut up intellisense complaining about __attribute__(visibility("default"))
#ifdef EDITOR
#define EGLAPI
#define GL_APICALL
#endif

#define APPNAME "questxrexample"

////////////////////////////////////////////////////////////////////////////////////////////////////
// INCLUDES
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "android_native_app_glue.h"
#include <jni.h>
#include <android/native_activity.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SHADER SOURCE STRINGS
////////////////////////////////////////////////////////////////////////////////////////////////////

const char *BACKGROUND_VERT_SRC = R"glsl(
#version 320 es
precision highp float;

layout(location = 0) uniform mat4 view_proj;

layout(location = 0) out vec3 world_pos;

void main() {
        const vec2 positions[3] = vec2[3](vec2(-1000,-1000), vec2(3000,-1000), vec2(-1000, 3000));
        vec2 pos = positions[gl_VertexID];
        world_pos = vec3(pos.x, 0.0, pos.y);
        gl_Position = view_proj * vec4(world_pos, 1.0);
}
)glsl";

const char *BACKGROUND_FRAG_SRC = R"glsl(
#version 320 es
precision highp float;

layout(location = 1) uniform vec3 cam_pos;

layout(location = 0) in vec3 world_pos;
layout(location = 0) out vec4 out_color;

#define TILE_SIZE 1.0
#define LINE_WIDTH 0.1

void main() {
        // Based on the work of Evan Wallace: https://madebyevan.com/shaders/grid/
        vec3 cam_to_point = world_pos - cam_pos;
        float dist_sq = max(0.000001, (dot(cam_to_point, cam_to_point)));
        float fog_alpha = 1.0 / exp(0.005 * dist_sq);
        vec2 coord = world_pos.xz;
        vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
        float line = min(grid.x, grid.y);
        float color = 1.0 - min(line, 1.0);
        color = mix(color, 0.4, 1.0 - fog_alpha);
        vec3 col = mix(vec3(0.0), vec3(1.0, 1.0, 2.0), color);
        out_color = vec4(col, 1.0);
}
)glsl";

const char *BOX_VERT_SRC = R"glsl(
#version 320 es
precision highp float;

layout(location = 0) uniform mat4 mvp;
layout(location = 1) uniform vec2 trigger_state;
layout(location = 0) out vec3 vc;

void main() {
        const vec3 cube_positions[8] = vec3[8](
                vec3(-0.1, -0.1, -0.1),
                vec3( 0.1, -0.1, -0.1),
                vec3( 0.1, -0.1,  0.1),
                vec3(-0.1, -0.1,  0.1),
                vec3(-0.1,  0.1, -0.1),
                vec3( 0.1,  0.1, -0.1),
                vec3( 0.1,  0.1,  0.1),
                vec3(-0.1,  0.1,  0.1) 
        );

        const int cube_indices[36] = int[36](
                0, 2, 1, 0, 3, 2,
                4, 5, 6, 4, 6, 7,
                0, 1, 5, 0, 5, 4,
                3, 6, 2, 3, 7, 6,
                0, 4, 7, 0, 7, 3,
                1, 2, 6, 1, 6, 5 
        );

        int index = clamp(gl_VertexID, 0, 35);
        int element = clamp(cube_indices[index], 0, 7);
        float t = -(cos(3.14159 * trigger_state.x) - 1.0) / 2.0; // Ease
        vec3 pos = vec3(mix(1.0, 1.2, t)) * cube_positions[element];

        gl_Position = vec4(mvp * vec4(pos, 1.0));
        vc = mix(vec3(0, 0, 1), vec3(1, 0, 0), trigger_state.x);
}
)glsl";


const char *BOX_FRAG_SRC = R"glsl(
#version 320 es
precision highp float;

layout(location = 0) in vec3 vc;
layout(location = 0) out vec4 outColor;

void main() {
        outColor = vec4(vc, 1.0);
}
)glsl";

////////////////////////////////////////////////////////////////////////////////////////////////////
// MATRIX HELPERS
//
// Based on code from https://github.com/felselva/mathc
// Copyright © 2018 Felipe Ferreira da Silva
// https://github.com/felselva/mathc/blob/master/LICENSE
////////////////////////////////////////////////////////////////////////////////////////////////////

void matrix_identity(float *result) {
	result[0] = (1.0);
	result[1] = (0.0);
	result[2] = (0.0);
	result[3] = (0.0);
	result[4] = (0.0);
	result[5] = (1.0);
	result[6] = (0.0);
	result[7] = (0.0);
	result[8] = (0.0);
	result[9] = (0.0);
	result[10] = (1.0);
	result[11] = (0.0);
	result[12] = (0.0);
	result[13] = (0.0);
	result[14] = (0.0);
	result[15] = (1.0);
}

void matrix_translate(float *result, float* m0, float* v0) {
	result[0] = m0[0];
	result[1] = m0[1];
	result[2] = m0[2];
	result[3] = m0[3];
	result[4] = m0[4];
	result[5] = m0[5];
	result[6] = m0[6];
	result[7] = m0[7];
	result[8] = m0[8];
	result[9] = m0[9];
	result[10] = m0[10];
	result[11] = m0[11];
	result[12] = m0[12] + v0[0];
	result[13] = m0[13] + v0[1];
	result[14] = m0[14] + v0[2];
	result[15] = m0[15];
}

void matrix_rotation_from_quat(float *result, float *q0) {
	float xx = q0[0] * q0[0];
	float yy = q0[1] * q0[1];
	float zz = q0[2] * q0[2];
	float xy = q0[0] * q0[1];
	float zw = q0[2] * q0[3];
	float xz = q0[0] * q0[2];
	float yw = q0[1] * q0[3];
	float yz = q0[1] * q0[2];
	float xw = q0[0] * q0[3];
	result[0] = (1.0) - (2.0) * (yy + zz);
	result[1] = (2.0) * (xy + zw);
	result[2] = (2.0) * (xz - yw);
	result[3] = (0.0);
	result[4] = (2.0) * (xy - zw);
	result[5] = (1.0) - (2.0) * (xx + zz);
	result[6] = (2.0) * (yz + xw);
	result[7] = (0.0);
	result[8] = (2.0) * (xz + yw);
	result[9] = (2.0) * (yz - xw);
	result[10] = (1.0) - (2.0) * (xx + yy);
	result[11] = (0.0);
	result[12] = (0.0);
	result[13] = (0.0);
	result[14] = (0.0);
	result[15] = (1.0);
}

void matrix_inverse(float *result, float *m0) {
	float inverse[16];
	float inverted_determinant;
	float m11 = m0[0];
	float m21 = m0[1];
	float m31 = m0[2];
	float m41 = m0[3];
	float m12 = m0[4];
	float m22 = m0[5];
	float m32 = m0[6];
	float m42 = m0[7];
	float m13 = m0[8];
	float m23 = m0[9];
	float m33 = m0[10];
	float m43 = m0[11];
	float m14 = m0[12];
	float m24 = m0[13];
	float m34 = m0[14];
	float m44 = m0[15];
	inverse[0] = m22 * m33 * m44 - m22 * m43 * m34 - m23 * m32 * m44 + m23 * m42 * m34 + m24 * m32 * m43 - m24 * m42 * m33;
	inverse[4] = -m12 * m33 * m44 + m12 * m43 * m34 + m13 * m32 * m44 - m13 * m42 * m34 - m14 * m32 * m43 + m14 * m42 * m33;
	inverse[8] = m12 * m23 * m44 - m12 * m43 * m24 - m13 * m22 * m44 + m13 * m42 * m24 + m14 * m22 * m43 - m14 * m42 * m23;
	inverse[12] = -m12 * m23 * m34 + m12 * m33 * m24 + m13 * m22 * m34 - m13 * m32 * m24 - m14 * m22 * m33 + m14 * m32 * m23;
	inverse[1] = -m21 * m33 * m44 + m21 * m43 * m34 + m23 * m31 * m44 - m23 * m41 * m34 - m24 * m31 * m43 + m24 * m41 * m33;
	inverse[5] =m11 * m33 * m44 -m11 * m43 * m34 - m13 * m31 * m44 + m13 * m41 * m34 + m14 * m31 * m43 - m14 * m41 * m33;
	inverse[9] = -m11 * m23 * m44 +m11 * m43 * m24 + m13 * m21 * m44 - m13 * m41 * m24 - m14 * m21 * m43 + m14 * m41 * m23;
	inverse[13] =m11 * m23 * m34 -m11 * m33 * m24 - m13 * m21 * m34 + m13 * m31 * m24 + m14 * m21 * m33 - m14 * m31 * m23;
	inverse[2] = m21 * m32 * m44 - m21 * m42 * m34 - m22 * m31 * m44 + m22 * m41 * m34 + m24 * m31 * m42 - m24 * m41 * m32;
	inverse[6] = -m11 * m32 * m44 +m11 * m42 * m34 + m12 * m31 * m44 - m12 * m41 * m34 - m14 * m31 * m42 + m14 * m41 * m32;
	inverse[10] =m11 * m22 * m44 -m11 * m42 * m24 - m12 * m21 * m44 + m12 * m41 * m24 + m14 * m21 * m42 - m14 * m41 * m22;
	inverse[14] = -m11 * m22 * m34 +m11 * m32 * m24 + m12 * m21 * m34 - m12 * m31 * m24 - m14 * m21 * m32 + m14 * m31 * m22;
	inverse[3] = -m21 * m32 * m43 + m21 * m42 * m33 + m22 * m31 * m43 - m22 * m41 * m33 - m23 * m31 * m42 + m23 * m41 * m32;
	inverse[7] = m11 * m32 * m43 - m11 * m42 * m33 - m12 * m31 * m43 + m12 * m41 * m33 + m13 * m31 * m42 - m13 * m41 * m32;
	inverse[11] = -m11 * m22 * m43 + m11 * m42 * m23 + m12 * m21 * m43 - m12 * m41 * m23 - m13 * m21 * m42 + m13 * m41 * m22;
	inverse[15] = m11 * m22 * m33 - m11 * m32 * m23 - m12 * m21 * m33 + m12 * m31 * m23 + m13 * m21 * m32 - m13 * m31 * m22;
	inverted_determinant = (1.0) / (m11 * inverse[0] + m21 * inverse[4] + m31 * inverse[8] + m41 * inverse[12]);
	result[0] = inverse[0] * inverted_determinant;
	result[1] = inverse[1] * inverted_determinant;
	result[2] = inverse[2] * inverted_determinant;
	result[3] = inverse[3] * inverted_determinant;
	result[4] = inverse[4] * inverted_determinant;
	result[5] = inverse[5] * inverted_determinant;
	result[6] = inverse[6] * inverted_determinant;
	result[7] = inverse[7] * inverted_determinant;
	result[8] = inverse[8] * inverted_determinant;
	result[9] = inverse[9] * inverted_determinant;
	result[10] = inverse[10] * inverted_determinant;
	result[11] = inverse[11] * inverted_determinant;
	result[12] = inverse[12] * inverted_determinant;
	result[13] = inverse[13] * inverted_determinant;
	result[14] = inverse[14] * inverted_determinant;
	result[15] = inverse[15] * inverted_determinant;
}

void matrix_proj_opengl(float *proj, float left, float right, float up, float down, float near, float far) {
        assert(near < far);

        const float tan_left = tan(left);
        const float tan_right = tan(right);

        const float tan_down = tan(down);
        const float tan_up = tan(up);

        const float tan_width = tan_right - tan_left;
        const float tan_height = (tan_up - tan_down);

        const float offset = near;

        proj[0] = 2 / tan_width;
        proj[4] = 0;
        proj[8] = (tan_right + tan_left) / tan_width;
        proj[12] = 0;

        proj[1] = 0;
        proj[5] = 2 / tan_height;
        proj[9] = (tan_up + tan_down) / tan_height;
        proj[13] = 0;

        proj[2] = 0;
        proj[6] = 0;
        proj[10] = -(far + offset) / (far - near);
        proj[14] = -(far * (near + offset)) / (far - near);

        proj[3] = 0;
        proj[7] = 0;
        proj[11] = -1;
        proj[15] = 0;
}

void matrix_multiply(float *result, float *m0, float *m1) {
	float multiplied[16];
	multiplied[0] = m0[0] * m1[0] + m0[4] * m1[1] + m0[8] * m1[2] + m0[12] * m1[3];
	multiplied[1] = m0[1] * m1[0] + m0[5] * m1[1] + m0[9] * m1[2] + m0[13] * m1[3];
	multiplied[2] = m0[2] * m1[0] + m0[6] * m1[1] + m0[10] * m1[2] + m0[14] * m1[3];
	multiplied[3] = m0[3] * m1[0] + m0[7] * m1[1] + m0[11] * m1[2] + m0[15] * m1[3];
	multiplied[4] = m0[0] * m1[4] + m0[4] * m1[5] + m0[8] * m1[6] + m0[12] * m1[7];
	multiplied[5] = m0[1] * m1[4] + m0[5] * m1[5] + m0[9] * m1[6] + m0[13] * m1[7];
	multiplied[6] = m0[2] * m1[4] + m0[6] * m1[5] + m0[10] * m1[6] + m0[14] * m1[7];
	multiplied[7] = m0[3] * m1[4] + m0[7] * m1[5] + m0[11] * m1[6] + m0[15] * m1[7];
	multiplied[8] = m0[0] * m1[8] + m0[4] * m1[9] + m0[8] * m1[10] + m0[12] * m1[11];
	multiplied[9] = m0[1] * m1[8] + m0[5] * m1[9] + m0[9] * m1[10] + m0[13] * m1[11];
	multiplied[10] = m0[2] * m1[8] + m0[6] * m1[9] + m0[10] * m1[10] + m0[14] * m1[11];
	multiplied[11] = m0[3] * m1[8] + m0[7] * m1[9] + m0[11] * m1[10] + m0[15] * m1[11];
	multiplied[12] = m0[0] * m1[12] + m0[4] * m1[13] + m0[8] * m1[14] + m0[12] * m1[15];
	multiplied[13] = m0[1] * m1[12] + m0[5] * m1[13] + m0[9] * m1[14] + m0[13] * m1[15];
	multiplied[14] = m0[2] * m1[12] + m0[6] * m1[13] + m0[10] * m1[14] + m0[14] * m1[15];
	multiplied[15] = m0[3] * m1[12] + m0[7] * m1[13] + m0[11] * m1[14] + m0[15] * m1[15];
	result[0] = multiplied[0];
	result[1] = multiplied[1];
	result[2] = multiplied[2];
	result[3] = multiplied[3];
	result[4] = multiplied[4];
	result[5] = multiplied[5];
	result[6] = multiplied[6];
	result[7] = multiplied[7];
	result[8] = multiplied[8];
	result[9] = multiplied[9];
	result[10] = multiplied[10];
	result[11] = multiplied[11];
	result[12] = multiplied[12];
	result[13] = multiplied[13];
	result[14] = multiplied[14];
	result[15] = multiplied[15];
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// APPLICATION STATE
////////////////////////////////////////////////////////////////////////////////////////////////////

// Some array length defines for readability
#define HAND_COUNT (2)
#define MAX_VIEWS (4)
#define MAX_SWAPCHAIN_LENGTH (3)

struct app_t {
        // Native app glue
        android_app *app;
        bool is_window_init;

        // EGL state required to initialise OpenXR
        EGLDisplay egl_display;
        EGLConfig egl_config;
        EGLContext egl_context;
        EGLSurface egl_surface;

        // OpenXR driver state
        XrInstance instance;
        XrSystemId system;
        XrSession session;

        // Views
        uint32_t view_count;
        XrViewConfigurationView view_configs[MAX_VIEWS];

        // Spaces
        XrSpace stage_space;
        XrSpace hand_spaces[HAND_COUNT];

        // OpenXR paths (interned query strings)
        XrPath touch_controller_path;
        XrPath hand_paths[HAND_COUNT];
        XrPath squeeze_value_paths[HAND_COUNT];
        XrPath trigger_value_paths[HAND_COUNT];
        XrPath pose_paths[HAND_COUNT];
        XrPath haptic_paths[HAND_COUNT];
        XrPath menu_click_paths[HAND_COUNT];

        // Action Set and Actions
        XrActionSet action_set;
        XrAction grab_action;
        XrAction trigger_action;
        XrAction trigger_click_action;
        XrAction pose_action;
        XrAction vibrate_action;
        XrAction menu_action;

        // Swapchains
        int32_t swapchain_widths[MAX_VIEWS];
        int32_t swapchain_heights[MAX_VIEWS];
        uint32_t swapchain_lengths[MAX_VIEWS];
        XrSwapchain swapchains[MAX_VIEWS];
	XrSwapchainImageOpenGLESKHR swapchain_images[MAX_VIEWS][MAX_SWAPCHAIN_LENGTH];

        // OpenGL state
        uint32_t box_program;
        uint32_t background_program;
        uint32_t framebuffer;
        uint32_t depth_targets[MAX_VIEWS];

        // Current Controller Inputs
        XrSpaceLocation hand_locations[HAND_COUNT];
        XrActionStateFloat trigger_states[HAND_COUNT];
        XrActionStateBoolean trigger_click_states[HAND_COUNT];

        // Session State
        XrSessionState session_state;
        XrFrameState frame_state;
        bool should_render;
        bool is_running;
        bool is_session_ready;
        bool is_session_begin_ever;

        // Frame Submission
        uint32_t view_submit_count;
        XrCompositionLayerProjection projection_layer;
        XrCompositionLayerProjectionView projection_layer_views[MAX_VIEWS];
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// APPLICATION INIT
////////////////////////////////////////////////////////////////////////////////////////////////////

// Callback handling android commands
extern "C" void app_android_handle_cmd(android_app *app, int32_t cmd) {
        app_t *a = (app_t *)app->userData;

        switch (cmd) {
        case APP_CMD_DESTROY:
                // Handle application shutdown
                ANativeActivity_finish(app->activity);
                // TODO: call app_shutdown
                break;
        case APP_CMD_INIT_WINDOW:
                if (!a->is_window_init) {
                        a->is_window_init = true;
                        printf( "Got start event\n" );
                }
                else {
                        // TODO: Handle Resume
                }
                break;
        case APP_CMD_TERM_WINDOW:
                // Turns up when focus is lost
                // Seems like the main loop just xrWaitFrame hitches
        	break;
        case APP_CMD_SAVE_STATE:
                printf("Saving application state\n");
                app->savedState = malloc(sizeof(app_t));
                memcpy(app->savedState, a, sizeof(app_t));
                app->savedStateSize = sizeof(app_t);
                break;
        case APP_CMD_RESUME:
                // Nope, that doesn't work
                // printf("Resumed, loading state\n");
                // memcpy(a, app->savedState, sizeof(app_t));
                break;
        // TODO: APP_CMD_SAVE_STATE
        default:
                printf("event not handled: %d\n", cmd);
        }
}

// Sets command callback and userdata on the android app object and blocks until the window is ready
void app_set_callbacks_and_wait(app_t *a, android_app *app) {
        a->app = app;
        app->userData = a;
        app->onAppCmd = app_android_handle_cmd;
        
        int events;
        while (!a->is_window_init) {
                struct android_poll_source *source;
                if ( ALooper_pollAll( 0, 0, &events, (void **)&source ) >= 0 ) {
                        if (source != NULL) {
                                source->process(app, source);
                        }
                }
        }
        printf("Window Initialized\n");
}

// Initialise EGL resources and context, needed later to pass to OpenXR
void app_init_egl(app_t *a) {
        // Display
        a->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        assert(a->egl_display != EGL_NO_DISPLAY);
        EGLint egl_major, egl_minor;
        int egl_init_success = eglInitialize(a->egl_display, &egl_major, &egl_minor);
        assert(egl_init_success);
        printf("EGL Version: \"%s\"\n", eglQueryString(a->egl_display, EGL_VERSION));
        printf("EGL Vendor: \"%s\"\n", eglQueryString(a->egl_display, EGL_VENDOR));
        printf("EGL Extensions: \"%s\"\n", eglQueryString(a->egl_display, EGL_EXTENSIONS));

        // Config
        EGLint num_config;
        EGLint const config_attribute_list[] = {
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_BUFFER_SIZE, 32,
                EGL_STENCIL_SIZE, 0,
                EGL_DEPTH_SIZE, 16, // Maybe 32?
                //EGL_SAMPLES, 1,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_NONE
        };
        eglChooseConfig(a->egl_display, config_attribute_list, &a->egl_config, 1, &num_config);
        printf("Config: %d\n", num_config);

        // Context
        printf("Creating Context\n");
        static const EGLint context_attribute_list[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
        };
        a->egl_context = eglCreateContext(a->egl_display, a->egl_config, EGL_NO_CONTEXT, context_attribute_list);
        assert(a->egl_context != EGL_NO_CONTEXT);
        printf("Context Created %p\n", a->egl_context);

        // Surface
        assert(a->app->window);
        int win_width = ANativeWindow_getWidth(a->app->window);
        int win_height = ANativeWindow_getHeight(a->app->window);
        printf("Width/Height: %dx%d\n", win_width, win_height);
        EGLint window_attribute_list[] = { EGL_NONE };
        a->egl_surface = eglCreateWindowSurface(a->egl_display, a->egl_config, a->app->window, window_attribute_list);
        printf("Got Surface: %p\n", a->egl_surface);
        assert(a->egl_surface != EGL_NO_SURFACE);

        // Make Current
        int egl_make_current_success = eglMakeCurrent(a->egl_display, a->egl_surface, a->egl_surface, a->egl_context);
        assert(egl_make_current_success);

        // Make some OpenGL calls
        printf("GL Vendor: \"%s\"\n", glGetString(GL_VENDOR));
        printf("GL Renderer: \"%s\"\n", glGetString(GL_RENDERER));
        printf("GL Version: \"%s\"\n", glGetString(GL_VERSION));
        printf("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));
}

// Initialise the loader, ensure we have the extensions we need, and create the OpenXR instance
void app_init_xr_create_instance(app_t *a) {
        XrResult result;

        // Loader
        PFN_xrInitializeLoaderKHR loader_func;
	result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&loader_func);
        assert(XR_SUCCEEDED(result));
	XrLoaderInitInfoAndroidKHR init_data = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
	init_data.applicationVM = a->app->activity->vm;
	init_data.applicationContext = a->app->activity->clazz;
	result = loader_func((XrLoaderInitInfoBaseHeaderKHR*)&init_data);
        assert(XR_SUCCEEDED(result));

        // Enumerate Extensions
        XrExtensionProperties extension_properties[128];
	uint32_t extension_count = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &extension_count, NULL);
        assert(XR_SUCCEEDED(result));
        assert(extension_count <= 128);
        for (int i=0; i < extension_count; i++) {
                extension_properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
                extension_properties[i].next = NULL;
        }
        result = xrEnumerateInstanceExtensionProperties(NULL, extension_count, &extension_count, extension_properties);
        assert(XR_SUCCEEDED(result));
        printf("OpenXR Extension Count: %d\n", extension_count);
        for (int i=0; i < extension_count; i++) {
                printf("        %s\n", extension_properties[i].extensionName);
        }

        // Check for GLES Extension
	bool is_gles_supported = false;
	for(int i = 0; i < extension_count; i++ ) {
		if (!strcmp("XR_KHR_opengl_es_enable", extension_properties[i].extensionName)) {
			is_gles_supported = true;
		}
	}
        assert(is_gles_supported);
        printf("OpenXR OpenGL ES extension found\n");

        // Create Instance
	const char* const enabledExtensions[] = {"XR_KHR_opengl_es_enable"};
	XrInstanceCreateInfo instance_desc = { XR_TYPE_INSTANCE_CREATE_INFO };
	instance_desc.next = NULL;
	instance_desc.createFlags = 0;
	instance_desc.enabledExtensionCount = 1;
	instance_desc.enabledExtensionNames = enabledExtensions;
	instance_desc.enabledApiLayerCount = 0;
	instance_desc.enabledApiLayerNames = NULL;
	strcpy(instance_desc.applicationInfo.applicationName, "questxrexample");
	instance_desc.applicationInfo.engineName[0] = '\0';
	instance_desc.applicationInfo.applicationVersion = 1;
	instance_desc.applicationInfo.engineVersion = 0;
	instance_desc.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	result = xrCreateInstance(&instance_desc, &a->instance);
        assert(XR_SUCCEEDED(result));

        // Instance Properties
        XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
        instance_props.next = NULL;
        result = xrGetInstanceProperties(a->instance, &instance_props);
        assert(XR_SUCCEEDED(result));
        printf("Runtime Name: %s\n", instance_props.runtimeName);
        printf("Runtime Name: %s\n", instance_props.runtimeName);
        printf("Runtime Version: %d.%d.%d\n",
                XR_VERSION_MAJOR(instance_props.runtimeVersion),
                XR_VERSION_MINOR(instance_props.runtimeVersion),
                XR_VERSION_PATCH(instance_props.runtimeVersion));

        // Enumerate API Layers
        XrApiLayerProperties layer_props[64];
        uint32_t layer_count;
        result = xrEnumerateApiLayerProperties(0, &layer_count, NULL);
        assert(XR_SUCCEEDED(result));
        for (int i=0; i < layer_count; i++) {
                layer_props[i].type = XR_TYPE_API_LAYER_PROPERTIES;
                layer_props[i].next = NULL;
        }
        result = xrEnumerateApiLayerProperties(layer_count, &layer_count, layer_props);
        assert(XR_SUCCEEDED(result));
        printf("OpenXR API Layers: %d\n", layer_count);
        for (int i=0; i < layer_count; i++) {
                printf("        %s, %s\n", layer_props[i].layerName, layer_props[i].description);
        }
}

// Get the system and print its properties
void app_init_xr_get_system(app_t *a) {
        XrSystemGetInfo system_desc = { XR_TYPE_SYSTEM_GET_INFO };
	system_desc.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	system_desc.next = NULL;

	XrResult result = xrGetSystem(a->instance, &system_desc, &a->system);
        assert(XR_SUCCEEDED(result));

        XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };
        result = xrGetSystemProperties(a->instance, a->system, &system_props);
        assert(XR_SUCCEEDED(result));

        printf("System properties for system \"%s\":\n", system_props.systemName);
        printf("	maxLayerCount: %d\n", system_props.graphicsProperties.maxLayerCount);
        printf("	maxSwapChainImageHeight: %d\n", system_props.graphicsProperties.maxSwapchainImageHeight);
        printf("	maxSwapChainImageWidth: %d\n", system_props.graphicsProperties.maxSwapchainImageWidth);
        printf("	Orientation Tracking: %s\n", system_props.trackingProperties.orientationTracking ? "true" : "false");
        printf("	Position Tracking: %s\n", system_props.trackingProperties.positionTracking ? "true" : "false");
}

// Enumerate the views (perspectives we need to render) and print their properties
void app_init_xr_enum_views(app_t *a) {
        XrResult result;

        // Enumerate View Configs
        result = xrEnumerateViewConfigurationViews(a->instance, a->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &a->view_count, NULL);
        assert(XR_SUCCEEDED(result));
        assert(a->view_count <= MAX_VIEWS);
        for (int i=0; i < a->view_count; i++) {
                a->view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                a->view_configs[i].next = NULL;
        }
        result = xrEnumerateViewConfigurationViews(a->instance, a->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, a->view_count, &a->view_count, a->view_configs);
        assert(XR_SUCCEEDED(result));
        printf("%d view_configs:\n", a->view_count);
        for (int i = 0; i < a->view_count; i++) {
                printf("	view_configs[%d]:\n", i);
                printf("		recommendedImageRectWidth: %d\n", a->view_configs[i].recommendedImageRectWidth);
                printf("		maxImageRectWidth: %d\n", a->view_configs[i].maxImageRectWidth);
                printf("		recommendedImageRectHeight: %d\n", a->view_configs[i].recommendedImageRectHeight);
                printf("		maxImageRectHeight: %d\n", a->view_configs[i].maxImageRectHeight);
                printf("		recommendedSwapchainSampleCount: %d\n", a->view_configs[i].recommendedSwapchainSampleCount);
                printf("		maxSwapchainSampleCount: %d\n", a->view_configs[i].maxSwapchainSampleCount);
        }
}

// Create the session, passing in our EGL information
void app_init_xr_create_session(app_t *a) {
        XrResult result;

        // Create the session
	PFN_xrGetOpenGLESGraphicsRequirementsKHR gles_reqs_func;
	xrGetInstanceProcAddr(a->instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)(&gles_reqs_func));
	XrGraphicsRequirementsOpenGLESKHR xr_gles_reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
	result = gles_reqs_func(a->instance, a->system, &xr_gles_reqs);
        assert(XR_SUCCEEDED(result));
	const XrVersion egl_version = XR_MAKE_VERSION(3, 2, 0);
        assert(egl_version >= xr_gles_reqs.minApiVersionSupported && egl_version <= xr_gles_reqs.maxApiVersionSupported);

	XrGraphicsBindingOpenGLESAndroidKHR gl_binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
	gl_binding.display = a->egl_display;
	gl_binding.config = a->egl_config;
	gl_binding.context = a->egl_context;
	XrSessionCreateInfo session_desc = { XR_TYPE_SESSION_CREATE_INFO };
	session_desc.next = &gl_binding;
	session_desc.systemId = a->system;
	result = xrCreateSession(a->instance, &session_desc, &a->session);
        assert(XR_SUCCEEDED(result));
}

// Create the reference space, and print available spaces
void app_init_xr_create_stage_space(app_t *a) {
        XrResult result;

        // Create Space
        uint32_t reference_spaces_count;
        XrReferenceSpaceType reference_spaces[64];
        result = xrEnumerateReferenceSpaces(a->session, 0, &reference_spaces_count, NULL);
        assert(XR_SUCCEEDED(result) && reference_spaces_count <= 64);
        for (int i = 0; i < reference_spaces_count; i++) {
                reference_spaces[i] = XR_REFERENCE_SPACE_TYPE_VIEW;
        }
        result = xrEnumerateReferenceSpaces(a->session, reference_spaces_count, &reference_spaces_count, reference_spaces);
        assert(XR_SUCCEEDED(result));
        printf("Reference Spaces:\n");
        for (int i = 0; i < reference_spaces_count; i++) {
                switch (reference_spaces[i]) {
                case XR_REFERENCE_SPACE_TYPE_VIEW:
                        printf("	XR_REFERENCE_SPACE_TYPE_VIEW\n");
                        break;
                case XR_REFERENCE_SPACE_TYPE_LOCAL:
                        printf("	XR_REFERENCE_SPACE_TYPE_LOCAL\n");
                        break;
                case XR_REFERENCE_SPACE_TYPE_STAGE:
                        printf("	XR_REFERENCE_SPACE_TYPE_STAGE\n");
                        break;
                default:
                        printf("	XR_REFERENCE_SPACE_TYPE_%d\n", reference_spaces[i]);
                        break;
                }
        }

	XrPosef identity_pose = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
	XrReferenceSpaceCreateInfo space_desc;
	space_desc.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	space_desc.next = NULL;
	space_desc.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	space_desc.poseInReferenceSpace = identity_pose;
	result = xrCreateReferenceSpace(a->session, &space_desc, &a->stage_space);
        assert(XR_SUCCEEDED(result));
}

// Create the action set, actions, interaction profile, and attach the action set to the session
void app_init_xr_create_actions(app_t *a) {
        XrResult result;

        // Create Action Set
	XrActionSetCreateInfo action_set_desc;
	action_set_desc.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	action_set_desc.next = NULL;
	strcpy(action_set_desc.actionSetName, "gameplay");
	strcpy(action_set_desc.localizedActionSetName, "Gameplay");
	action_set_desc.priority = 0;
	result = xrCreateActionSet(a->instance, &action_set_desc, &a->action_set);
        assert(XR_SUCCEEDED(result));

        // Create sub-action paths
	xrStringToPath(a->instance, "/user/hand/left", &a->hand_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right", &a->hand_paths[1]);
	xrStringToPath(a->instance, "/user/hand/left/input/squeeze/value",  &a->squeeze_value_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right/input/squeeze/value", &a->squeeze_value_paths[1]);
	xrStringToPath(a->instance, "/user/hand/left/input/trigger/value",  &a->trigger_value_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right/input/trigger/value", &a->trigger_value_paths[1]);
	xrStringToPath(a->instance, "/user/hand/left/input/grip/pose", &a->pose_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right/input/grip/pose", &a->pose_paths[1]);
	xrStringToPath(a->instance, "/user/hand/left/output/haptic", &a->haptic_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right/output/haptic", &a->haptic_paths[1]);
	xrStringToPath(a->instance, "/user/hand/left/input/menu/click", &a->menu_click_paths[0]);
	xrStringToPath(a->instance, "/user/hand/right/input/menu/click", &a->menu_click_paths[1]);

        // Create Actions
        XrActionCreateInfo grab_desc;
	grab_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	grab_desc.next = NULL;
	grab_desc.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(grab_desc.actionName, "grab_object" );
	strcpy(grab_desc.localizedActionName, "Grab Object");
	grab_desc.countSubactionPaths = 2;
	grab_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &grab_desc, &a->grab_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo trigger_desc;
	trigger_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	trigger_desc.next = NULL;
	trigger_desc.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(trigger_desc.actionName, "trigger" );
	strcpy(trigger_desc.localizedActionName, "Trigger");
	trigger_desc.countSubactionPaths = 2;
	trigger_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &trigger_desc, &a->trigger_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo click_desc;
	click_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	click_desc.next = NULL;
	click_desc.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(click_desc.actionName, "trigger_click" );
	strcpy(click_desc.localizedActionName, "Trigger Click");
	click_desc.countSubactionPaths = 2;
	click_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &click_desc, &a->trigger_click_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo pose_desc;
	pose_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	pose_desc.next = NULL;
	pose_desc.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(pose_desc.actionName, "hand_pose" );
	strcpy(pose_desc.localizedActionName, "Hand Pose");
	pose_desc.countSubactionPaths = 2;
	pose_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &pose_desc, &a->pose_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo vibrate_desc;
	vibrate_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	vibrate_desc.next = NULL;
	vibrate_desc.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	strcpy(vibrate_desc.actionName, "vibrate_hand" );
	strcpy(vibrate_desc.localizedActionName, "Vibrate Hand");
	vibrate_desc.countSubactionPaths = 2;
	vibrate_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &vibrate_desc, &a->vibrate_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo menu_desc;
	menu_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	menu_desc.next = NULL;
	menu_desc.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(menu_desc.actionName, "quit_session" );
	strcpy(menu_desc.localizedActionName, "Menu Button");
	menu_desc.countSubactionPaths = 2;
	menu_desc.subactionPaths = a->hand_paths;
	result = xrCreateAction(a->action_set, &menu_desc, &a->menu_action);
        assert(XR_SUCCEEDED(result));

        // Oculus Touch Controller Interaction Profile
        xrStringToPath(a->instance, "/interaction_profiles/oculus/touch_controller", &a->touch_controller_path);
        XrActionSuggestedBinding bindings[] = {
                {a->grab_action, a->squeeze_value_paths[0]},
                {a->grab_action, a->squeeze_value_paths[1]},
                {a->trigger_action, a->trigger_value_paths[0]},
                {a->trigger_action, a->trigger_value_paths[1]},
                {a->trigger_click_action, a->trigger_value_paths[0]},
                {a->trigger_click_action, a->trigger_value_paths[1]},
                {a->pose_action, a->pose_paths[0]},
                {a->pose_action, a->pose_paths[1]},
                {a->menu_action, a->menu_click_paths[0]},
                {a->vibrate_action, a->haptic_paths[0]},
                {a->vibrate_action, a->haptic_paths[1]}
        };
        XrInteractionProfileSuggestedBinding suggested_bindings;
        suggested_bindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggested_bindings.next = NULL;
        suggested_bindings.interactionProfile = a->touch_controller_path;
        suggested_bindings.suggestedBindings = bindings;
        suggested_bindings.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
        result = xrSuggestInteractionProfileBindings(a->instance, &suggested_bindings);
        assert(XR_SUCCEEDED(result));

        // Hand Spaces
	XrActionSpaceCreateInfo action_space_desc = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
	action_space_desc.action = a->pose_action;
	XrPosef identity = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
	action_space_desc.poseInActionSpace = identity;
	action_space_desc.subactionPath = a->hand_paths[0];
	result = xrCreateActionSpace(a->session, &action_space_desc, &a->hand_spaces[0]);
        assert(XR_SUCCEEDED(result));
	action_space_desc.subactionPath = a->hand_paths[1];
	result = xrCreateActionSpace(a->session, &action_space_desc, &a->hand_spaces[1]);
        assert(XR_SUCCEEDED(result));

        // Attach Action Set
	XrSessionActionSetsAttachInfo session_actions_desc;
	session_actions_desc.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	session_actions_desc.next = NULL;
	session_actions_desc.countActionSets = 1;
	session_actions_desc.actionSets = &a->action_set;
	result = xrAttachSessionActionSets(a->session, &session_actions_desc);
        assert(XR_SUCCEEDED(result));
}

// Choose a swapchain format, and create a swapchain per view
void app_init_xr_create_swapchains(app_t *a) {
        XrResult result;

        // Choose Swapchain Format
        uint32_t swapchain_format_count;
        result = xrEnumerateSwapchainFormats(a->session, 0, &swapchain_format_count, NULL);
        assert(XR_SUCCEEDED(result));
        assert(swapchain_format_count <= 128);
        int64_t swapchain_formats[128];
        result = xrEnumerateSwapchainFormats(a->session, swapchain_format_count, &swapchain_format_count, swapchain_formats);
        assert(XR_SUCCEEDED(result));
        bool is_default = true;
        int64_t selected_format = 0;
        for (int i=0; i < swapchain_format_count; i++) {
                if (swapchain_formats[i] == GL_SRGB8_ALPHA8) {
                        is_default = false;
                        selected_format = swapchain_formats[i];
                }
                if (swapchain_formats[i] == GL_SRGB8 && is_default) {
                        is_default = false;
                        selected_format = swapchain_formats[i];
                }
        }

	for (int i = 0; i < a->view_count; i++) {
                // Create Swapchain
		XrSwapchainCreateInfo swapchain_desc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchain_desc.createFlags = 0;
		swapchain_desc.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchain_desc.format = selected_format;
		swapchain_desc.sampleCount = 1;
		swapchain_desc.width = a->view_configs[i].recommendedImageRectWidth;
		swapchain_desc.height = a->view_configs[i].recommendedImageRectHeight;
		swapchain_desc.faceCount = 1;
		swapchain_desc.arraySize = 1;
		swapchain_desc.mipCount = 1;
		result = xrCreateSwapchain(a->session, &swapchain_desc, &a->swapchains[i]);
                assert(XR_SUCCEEDED(result));
                a->swapchain_widths[i] = swapchain_desc.width;
                a->swapchain_heights[i] = swapchain_desc.height;

                // Enumerate Swapchain Images
                result = xrEnumerateSwapchainImages(a->swapchains[i], 0, &a->swapchain_lengths[i], NULL);
                assert(XR_SUCCEEDED(result) && a->swapchain_lengths[i] <= MAX_SWAPCHAIN_LENGTH);
		for (int j = 0; j < a->swapchain_lengths[i]; j++) {
			a->swapchain_images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
			a->swapchain_images[i][j].next = NULL;
		}
                XrSwapchainImageBaseHeader* image_header = (XrSwapchainImageBaseHeader*)(&a->swapchain_images[i][0]);
		result = xrEnumerateSwapchainImages(a->swapchains[i], a->swapchain_lengths[i], &a->swapchain_lengths[i], image_header);
                assert(XR_SUCCEEDED(result));
	}

        printf("Swapchains:\n");
        for (int i = 0; i < a->view_count; i++) {
                printf("        width: %d\n", a->swapchain_widths[i]);
                printf("        height: %d\n", a->swapchain_heights[i]);
                printf("        length: %d\n", a->swapchain_lengths[i]);
        }
}

// Create a framebuffer, and a depth buffer per view
void app_init_opengl_framebuffers(app_t *a) {
        glGenFramebuffers(1, &a->framebuffer);

        for (int i=0; i < a->view_count; i++) {
                int width = a->swapchain_widths[i];
                int height = a->swapchain_heights[i];

                glGenTextures(1, &a->depth_targets[i]);
                glBindTexture(GL_TEXTURE_2D, a->depth_targets[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, a->depth_targets[i], 0);
        }
}

// Compile the OpenGL shaders into programs
void app_init_opengl_shaders(app_t *a) {
        glEnable(GL_DEPTH_TEST);  

        // Box Program
        uint32_t vert_shd = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert_shd, 1, &BOX_VERT_SRC, NULL);
        glCompileShader(vert_shd);

        int success;
        glGetShaderiv(vert_shd, GL_COMPILE_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetShaderInfoLog(vert_shd, 512, NULL, info_log);
                printf("Vertex shader compilation failed:\n %s\n", info_log);
        }

        uint32_t frag_shd = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag_shd, 1, &BOX_FRAG_SRC, NULL);
        glCompileShader(frag_shd);

        glGetShaderiv(frag_shd, GL_COMPILE_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetShaderInfoLog(frag_shd, 512, NULL, info_log);
                printf("Fragment shader compilation failed:\n %s\n", info_log);
        }

        a->box_program = glCreateProgram();
        glAttachShader(a->box_program, vert_shd);
        glAttachShader(a->box_program, frag_shd);
        glLinkProgram(a->box_program);

        glGetProgramiv(a->box_program, GL_LINK_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetProgramInfoLog(a->box_program, 512, NULL, info_log);
                printf("Program Linking failed:\n %s\n", info_log);
        }

        glDeleteShader(vert_shd);
        glDeleteShader(frag_shd);

        // Background Program
        vert_shd = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert_shd, 1, &BACKGROUND_VERT_SRC, NULL);
        glCompileShader(vert_shd);

        glGetShaderiv(vert_shd, GL_COMPILE_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetShaderInfoLog(vert_shd, 512, NULL, info_log);
                printf("Vertex shader compilation failed:\n %s\n", info_log);
        }

        frag_shd = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag_shd, 1, &BACKGROUND_FRAG_SRC, NULL);
        glCompileShader(frag_shd);

        glGetShaderiv(frag_shd, GL_COMPILE_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetShaderInfoLog(frag_shd, 512, NULL, info_log);
                printf("Fragment shader compilation failed:\n %s\n", info_log);
        }

        a->background_program = glCreateProgram();
        glAttachShader(a->background_program, vert_shd);
        glAttachShader(a->background_program, frag_shd);
        glLinkProgram(a->background_program);

        glGetProgramiv(a->background_program, GL_LINK_STATUS, &success);
        if (!success) {
                char info_log[512];
                glGetProgramInfoLog(a->background_program, 512, NULL, info_log);
                printf("Program Linking failed:\n %s\n", info_log);
        }

        glDeleteShader(vert_shd);
        glDeleteShader(frag_shd);
}

// Initialises the application state
void app_init(app_t *a, android_app *app) {
        app_set_callbacks_and_wait(a, app);
        app_init_egl(a);
        app_init_xr_create_instance(a);
        app_init_xr_get_system(a);
        app_init_xr_enum_views(a);
        app_init_xr_create_session(a);
        app_init_xr_create_stage_space(a);
        app_init_xr_create_actions(a);
        app_init_xr_create_swapchains(a);
        app_init_opengl_framebuffers(a);
        app_init_opengl_shaders(a);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// UPDATE LOOP
////////////////////////////////////////////////////////////////////////////////////////////////////

// Begin the OpenXR session
void app_update_begin_session(app_t *a) {
        printf("Beginning Session");
        XrSessionBeginInfo begin_desc;
        begin_desc.type = XR_TYPE_SESSION_BEGIN_INFO;
        begin_desc.next = NULL;
        begin_desc.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult result = xrBeginSession(a->session, &begin_desc);
        assert(XR_SUCCEEDED(result));
        a->is_session_begin_ever = true;
        a->is_session_ready = true;
}

// Handle session state changes
void app_update_session_state_change(app_t *a, XrSessionState state) {
        a->session_state = state;
        switch (a->session_state) {
        case XR_SESSION_STATE_IDLE:
                printf("XR_SESSION_STATE_IDLE\n");
                break;
        case XR_SESSION_STATE_READY:
                printf("XR_SESSION_STATE_READY\n");
                app_update_begin_session(a);
                break;
        case XR_SESSION_STATE_SYNCHRONIZED:
                printf("XR_SESSION_STATE_SYNCHRONIZED\n");
                break;
        case XR_SESSION_STATE_VISIBLE:
                printf("XR_SESSION_STATE_VISIBLE\n");
                break;
        case XR_SESSION_STATE_FOCUSED:
                printf("XR_SESSION_STATE_FOCUSED\n");
                break;
        case XR_SESSION_STATE_STOPPING:
                printf("XR_SESSION_STATE_STOPPING\n");
                break;
        case XR_SESSION_STATE_LOSS_PENDING:
                printf("XR_SESSION_STATE_LOSS_PENDING\n");
                break;
        case XR_SESSION_STATE_EXITING:
                printf("XR_SESSION_STATE_EXITING\n");
                break;
        default:
                printf("XR_SESSION_STATE_??? %d\n", (int)a->session_state);
                break;
        }
}

// Pump the android and OpenXR event loops
void app_update_pump_events(app_t *a) {
        // Pump Android Event Loop
        int events;
        struct android_poll_source *source;
        while (ALooper_pollAll(0, 0, &events, (void **)&source) >= 0 ) {
                if (source != NULL) {
                        source->process(a->app, source );
                }
        }

        // Pump OpenXR Event Loop
        bool is_remaining_events = true;
        while (is_remaining_events) {
                XrEventDataBuffer event_data = { XR_TYPE_EVENT_DATA_BUFFER };
                XrResult result = xrPollEvent(a->instance, &event_data);
                assert(XR_SUCCEEDED(result));
                if (result != XR_SUCCESS) {
                        is_remaining_events = false;
                        continue;
                }

                switch (event_data.type) {
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                        printf("Event: XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING\n");
                        // TODO: Handle, or prefer to handle loss pending in session state?
                        break;
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                        printf("Event: XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED -> ");
                        XrEventDataSessionStateChanged* ssc = (XrEventDataSessionStateChanged*)&event_data;
                        app_update_session_state_change(a, ssc->state);
                        break;
                }
                case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                        printf("Event: XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING\n");
                        // TODO: Handle Reference Spaces changes
                        break;
                case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                        printf("Event: XR_TYPE_EVENT_DATA_EVENTS_LOST\n");
                        // TODO: print warning
                        break;
                case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                        printf("Event: XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED\n");
                        // TODO: this shouldn't happen but handle
                        break;
                default:
                        printf("Event: Unhandled event type %d\n", event_data.type);
                        break;
                }
        }
}

// Wait for the next frame, and get the transforms and button inputs of the controllers
void app_update_begin_frame_and_get_inputs(app_t *a) {
        XrResult result;

        // Sync Input
        XrActiveActionSet active_action_set;
        active_action_set.actionSet = a->action_set;
        active_action_set.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo action_sync_info;
        action_sync_info.type = XR_TYPE_ACTIONS_SYNC_INFO;
        action_sync_info.next = NULL;
        action_sync_info.countActiveActionSets = 1;
        action_sync_info.activeActionSets = &active_action_set;
        result = xrSyncActions(a->session, &action_sync_info);
        assert(XR_SUCCEEDED(result));

        // Wait Frame
        a->frame_state.type = XR_TYPE_FRAME_STATE;
        a->frame_state.next = NULL;
        XrFrameWaitInfo frame_wait;
        frame_wait.type = XR_TYPE_FRAME_WAIT_INFO;
        frame_wait.next = NULL;
        result = xrWaitFrame(a->session, &frame_wait, &a->frame_state);
        assert(XR_SUCCEEDED(result));
        a->should_render = a->frame_state.shouldRender;

        // TODO: Different code paths for focussed vs. not focussed

        // Get Action States and Spaces (i.e. current state of the controller inputs)
        for (int i=0; i < HAND_COUNT; i++) {
                a->hand_locations[i].type = XR_TYPE_SPACE_LOCATION;
                a->trigger_states[i].type = XR_TYPE_ACTION_STATE_FLOAT;
                a->trigger_click_states[i].type = XR_TYPE_ACTION_STATE_BOOLEAN;
        }

        result = xrLocateSpace(a->hand_spaces[0], a->stage_space, a->frame_state.predictedDisplayTime, &a->hand_locations[0]);
        assert(XR_SUCCEEDED(result));
        result = xrLocateSpace(a->hand_spaces[1], a->stage_space, a->frame_state.predictedDisplayTime, &a->hand_locations[1]);
        assert(XR_SUCCEEDED(result));

        XrActionStateGetInfo action_get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
        action_get_info.action = a->trigger_action;
        action_get_info.subactionPath = a->hand_paths[0];
        xrGetActionStateFloat(a->session, &action_get_info, &a->trigger_states[0]);
        action_get_info.subactionPath = a->hand_paths[1];
        xrGetActionStateFloat(a->session, &action_get_info, &a->trigger_states[1]);
        action_get_info.action = a->trigger_click_action;
        action_get_info.subactionPath = a->hand_paths[0];
        xrGetActionStateBoolean(a->session, &action_get_info, &a->trigger_click_states[0]);
        action_get_info.subactionPath = a->hand_paths[1];
        xrGetActionStateBoolean(a->session, &action_get_info, &a->trigger_click_states[1]);

        XrFrameBeginInfo frame_begin;
        frame_begin.type = XR_TYPE_FRAME_BEGIN_INFO;
        frame_begin.next = NULL;
        result = xrBeginFrame(a->session, &frame_begin);
        assert(XR_SUCCEEDED(result));
}

// Locate the views, and render into the swapchains
void app_update_render(app_t *a) {
        XrResult result;

        // Reset Composition Layer
        a->projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        a->projection_layer.layerFlags = 0;
        a->projection_layer.next = NULL;
        a->projection_layer.space = a->stage_space;

        // Locate Views
        XrView views[MAX_VIEWS];
        for (int i=0; i < a->view_count; i++) {
                views[i].type = XR_TYPE_VIEW;
                views[i].next = NULL;
        }
        XrViewState view_state = { XR_TYPE_VIEW_STATE };
        XrViewLocateInfo view_locate_info;
        view_locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
        view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        view_locate_info.displayTime = a->frame_state.predictedDisplayTime;
        view_locate_info.space = a->stage_space;
        result = xrLocateViews(a->session, &view_locate_info, &view_state, a->view_count, &a->view_submit_count, views);
        assert(XR_SUCCEEDED(result));

        // Fill in Projection Views info
        for (int i = 0; i < a->view_submit_count; i++) {
                a->projection_layer_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                a->projection_layer_views[i].pose = views[i].pose;
                a->projection_layer_views[i].fov = views[i].fov;
                a->projection_layer_views[i].subImage.swapchain = a->swapchains[i];
                a->projection_layer_views[i].subImage.imageRect.offset.x = 0;
                a->projection_layer_views[i].subImage.imageRect.offset.y = 0;
                a->projection_layer_views[i].subImage.imageRect.extent.width = a->swapchain_widths[i];
                a->projection_layer_views[i].subImage.imageRect.extent.height = a->swapchain_heights[i];
                a->projection_layer_views[i].subImage.imageArrayIndex = 0;
        }

        for (int v = 0; v < a->view_submit_count; v++) {
                // Acquire and wait for the swapchain image
                uint32_t image_index;
                XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                result = xrAcquireSwapchainImage(a->swapchains[v], &acquire_info, &image_index);
                assert(XR_SUCCEEDED(result));
                XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wait_info.timeout = XR_INFINITE_DURATION;
                result = xrWaitSwapchainImage(a->swapchains[v], &wait_info);
                assert(XR_SUCCEEDED(result));
                XrSwapchainImageOpenGLESKHR swapchain_image = a->swapchain_images[v][image_index];
                uint32_t colour_tex = swapchain_image.image;
                int width = a->projection_layer_views[v].subImage.imageRect.extent.width;
                int height = a->projection_layer_views[v].subImage.imageRect.extent.height;

                // Projection
                float left = a->projection_layer_views[v].fov.angleLeft;
                float right = a->projection_layer_views[v].fov.angleRight;
                float up = a->projection_layer_views[v].fov.angleUp;
                float down = a->projection_layer_views[v].fov.angleDown;
                float proj[16];
                matrix_proj_opengl(proj, left, right, up, down, 0.01, 100.0);

                // View, View Projection
                float translation[16];
                float rotation[16];
                float view[16];
                float view_proj[16];
                float inverse_view_proj[16];
                matrix_identity(translation);
                matrix_translate(translation, translation, (float *)&a->projection_layer_views[v].pose.position);
                matrix_rotation_from_quat(rotation, (float *)&a->projection_layer_views[v].pose.orientation);
                matrix_multiply(view, translation, rotation);
                matrix_inverse(view, view);
                matrix_multiply(view_proj, proj, view);
                matrix_inverse(inverse_view_proj, view_proj);

                // Left MVP
                float left_translation[16];
                float left_rotation[16];
                float left_model[16];
                float left_mvp[16];
                matrix_identity(left_translation);
                matrix_translate(left_translation, left_translation, (float *)&a->hand_locations[0].pose.position);
                matrix_rotation_from_quat(left_rotation, (float *)&a->hand_locations[0].pose.orientation);
                matrix_multiply(left_model, left_translation, left_rotation);
                matrix_multiply(left_mvp, view_proj, left_model);

                // Right Model
                float right_translation[16];
                float right_rotation[16];
                float right_model[16];
                float right_mvp[16];
                matrix_identity(right_translation);
                matrix_translate(right_translation, right_translation, (float *)&a->hand_locations[1].pose.position);
                matrix_rotation_from_quat(right_rotation, (float *)&a->hand_locations[1].pose.orientation);
                matrix_multiply(right_model, right_translation, right_rotation);
                matrix_multiply(right_mvp, view_proj, right_model);
        
                // Render into the swapchain directly
                glBindFramebuffer(GL_FRAMEBUFFER, a->framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_tex, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, a->depth_targets[v], 0);
                glViewport(0, 0, width, height);
                glClearColor(0.4, 0.4, 0.8, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                // Render Left Hand
                glUseProgram(a->box_program);
                glUniformMatrix4fv(0, 1, GL_FALSE, left_mvp);
                glUniform2f(1, a->trigger_states[0].currentState, (float)(a->trigger_click_states[0].currentState));
                glDrawArrays(GL_TRIANGLES, 0, 36);

                // Render Right Hand
                glUseProgram(a->box_program);
                glUniformMatrix4fv(0, 1, GL_FALSE, right_mvp);
                glUniform2f(1, a->trigger_states[1].currentState, (float)(a->trigger_click_states[1].currentState));
                glDrawArrays(GL_TRIANGLES, 0, 36);
                
                // Render Background
                glUseProgram(a->background_program);
                glUniformMatrix4fv(0, 1, GL_FALSE, view_proj);
                glUniform3fv(1, 1, (float *)&a->hand_locations[0].pose.position);
                glDrawArrays(GL_TRIANGLES, 0, 3);

                // Release Image
                XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                result = xrReleaseSwapchainImage(a->swapchains[v], &release_info);
                assert(XR_SUCCEEDED(result));
        }

        a->projection_layer.viewCount = a->view_submit_count;
        a->projection_layer.views = &a->projection_layer_views[0];
}

// Submit the frame
void app_update_end_frame(app_t *a) {
        const XrCompositionLayerBaseHeader * layers[1] = { (XrCompositionLayerBaseHeader *)&a->projection_layer };
        XrFrameEndInfo frame_end = { XR_TYPE_FRAME_END_INFO };
        frame_end.displayTime = a->frame_state.predictedDisplayTime;
        frame_end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        frame_end.layerCount = a->should_render ? 1 : 0;
        frame_end.layers = a->should_render ? layers : NULL;

        XrResult result = xrEndFrame(a->session, &frame_end);
        assert(XR_SUCCEEDED(result));
}

// Update the application while it is running
void app_update(app_t *a) {
        app_update_pump_events(a);
        if (!a->is_session_ready) { return; }
        app_update_begin_frame_and_get_inputs(a);
        if (a->should_render) {
                app_update_render(a);
        }
        app_update_end_frame(a);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SHUTDOWN
////////////////////////////////////////////////////////////////////////////////////////////////////

// Clean up the OpenXR handles
void app_shutdown(app_t *a) {
        XrResult result;

        printf("Shutting Down\n");

        // Clean up
        for (int i=0; i < a->view_count; i++) {
                result = xrDestroySwapchain(a->swapchains[i]);
                assert(XR_SUCCEEDED(result));
        }

	result = xrDestroySpace(a->stage_space);
        assert(XR_SUCCEEDED(result));

        if (a->is_session_begin_ever) {
                result = xrEndSession(a->session);
                assert(XR_SUCCEEDED(result));
        }

        result = xrDestroySession(a->session);
        assert(XR_SUCCEEDED(result));

	result = xrDestroyInstance(a->instance);
        assert(XR_SUCCEEDED(result));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ENTRY POINT
////////////////////////////////////////////////////////////////////////////////////////////////////

// Entrypoint called by the OS when using native activity
extern "C" void android_main(android_app *app) {
        app_t a{};
        app_init(&a, app);

        a.is_running = true;
        while (a.is_running) {
                app_update(&a);
        }

        app_shutdown(&a);
}
