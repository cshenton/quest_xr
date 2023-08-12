// Editor only defines to shut up intellisense
#ifdef EDITOR
#define EGLAPI
#define GL_APICALL
#endif

#include "android_native_app_glue.h"
#include <jni.h>
#include <android/native_activity.h>

#include <assert.h>
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

static int g_window_is_init;

// Globals
struct android_app *gapp;
int android_width, android_height;
int override_android_screen_dimensons = 0;
int android_sdk_version;

EGLNativeWindowType native_window;
EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;
EGLConfig  egl_config;

static EGLint const config_attribute_list[] = {
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

static const EGLint context_attribute_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
};

static EGLint window_attribute_list[] = {
        EGL_NONE
};

extern "C" int __system_property_get(const char *__name, char *__value);

extern "C" void handle_cmd(android_app *app, int32_t cmd) {
        switch (cmd) {
        case APP_CMD_DESTROY:
                // Handle application shutdown
                ANativeActivity_finish(gapp->activity);
                break;
        case APP_CMD_INIT_WINDOW:
                if (!g_window_is_init) {
                        g_window_is_init = 1;
                        printf( "Got start event\n" );
                }
                else {
                        // TODO: Handle Resume
                }
                break;
                //case APP_CMD_TERM_WINDOW:
                        //This gets called initially when you click "back"
                        //This also gets called when you are brought into standby.
                        //Not sure why - callbacks here seem to break stuff.
                //	break;
        default:
                printf("event not handled: %d", cmd);
        }
}

extern "C" int32_t handle_input(android_app *app, AInputEvent *event) {
        return 0;
}

extern "C" void android_main(android_app *app) {
        printf("Window: %p", app->window);
        {
                char sdk_ver_str[92];
                int len = __system_property_get( "ro.build.version.sdk", sdk_ver_str );
                if ( len <= 0 ) {
                        android_sdk_version = 0;
                } else {
                        android_sdk_version = atoi(sdk_ver_str);
                }
        }

        gapp = app;
        app->onAppCmd = handle_cmd;
        // app->onInputEvent = handle_input;
        printf("Starting with Android SDK Version: %d\n", android_sdk_version);
        EGLint egl_major, egl_minor;
        EGLint num_config;

        int events;
        while (!g_window_is_init) {
                struct android_poll_source *source;
                if ( ALooper_pollAll( 0, 0, &events, (void **)&source ) >= 0 ) {
                        if (source != NULL) { 
                                source->process(gapp, source); 
                        }
                }
        }
        printf("Getting Display\n");

        egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

        if (egl_display == EGL_NO_DISPLAY) {
                printf("Error: No display found!\n");
                return;
        }

        printf("Initialising EGL\n");
        if (!eglInitialize( egl_display, &egl_major, &egl_minor)) {
                printf( "Error: eglInitialise failed!\n" );
                return;
        }

        printf("g_window_is_init_2\n");
        printf("EGL Version: \"%s\"\n", eglQueryString(egl_display, EGL_VERSION));
        printf("EGL Vendor: \"%s\"\n", eglQueryString(egl_display, EGL_VENDOR));
        printf("EGL Extensions: \"%s\"\n", eglQueryString(egl_display, EGL_EXTENSIONS));

        eglChooseConfig(egl_display, config_attribute_list, &egl_config, 1, &num_config);
        printf("Config: %d\n", num_config);

        printf("Creating Context\n");
        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribute_list);
        if (egl_context == EGL_NO_CONTEXT) {
                printf( "Error: eglCreateContext failed: 0x%08X\n",
                                eglGetError() );
                return;
        }
        printf("Context Created %p\n", egl_context);

        // Do I need this window code?
        if (native_window && !gapp->window) {
                printf( "WARNING: App restarted without a window.  Cannot progress.\n" );
                return;
        }
        printf("Getting Surface %p\n", native_window = gapp->window);

        if (!native_window) {
                printf( "FAULT: Cannot get window\n" );
                return;
        }

        android_width = ANativeWindow_getWidth(native_window);
        android_height = ANativeWindow_getHeight(native_window);
        printf("Width/Height: %dx%d\n", android_width, android_height);

        egl_surface = eglCreateWindowSurface(egl_display, egl_config, gapp->window, window_attribute_list);
        printf("Got Surface: %p\n", egl_surface);

        if (egl_surface == EGL_NO_SURFACE) {
                printf("Error: eglCreateWindowSurface failed: " "0x%08X\n", eglGetError());
                return;
        }

        if (!eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context)) {
                printf( "Error: eglMakeCurrent() failed: 0x%08X\n", eglGetError() );
                return;
        }

        printf("GL Vendor: \"%s\"\n", glGetString(GL_VENDOR));
        printf("GL Renderer: \"%s\"\n", glGetString(GL_RENDERER));
        printf("GL Version: \"%s\"\n", glGetString(GL_VERSION));
        printf("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));

        // OpenGL is working, now initialize OpenXR

        // Initialise the loader
        PFN_xrInitializeLoaderKHR xr_loader_func;
	XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xr_loader_func);
        assert(XR_SUCCEEDED(result));

	XrLoaderInitInfoAndroidKHR init_data = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
	init_data.applicationVM = gapp->activity->vm;
	init_data.applicationContext = gapp->activity->clazz;
	result = xr_loader_func((XrLoaderInitInfoBaseHeaderKHR*)&init_data);
        assert(XR_SUCCEEDED(result));

        // Enumerate the available extensions
        XrExtensionProperties xr_extension_properties[128];
	uint32_t xr_extension_count = 0;

	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &xr_extension_count, NULL);
        assert(XR_SUCCEEDED(result));
        assert(xr_extension_count <= 128);

        for (int i=0; i < xr_extension_count; i++) {
                xr_extension_properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
                xr_extension_properties[i].next = NULL;
        }

        result = xrEnumerateInstanceExtensionProperties(NULL, xr_extension_count, &xr_extension_count, xr_extension_properties);
        assert(XR_SUCCEEDED(result));

        // #ifndef NDEBUG
        printf("OpenXR Extension Count: %d\n", xr_extension_count);
        for (int i=0; i < xr_extension_count; i++) {
                printf("        %s\n", xr_extension_properties[i].extensionName);
        }
        // #endif

        // Check for extension support
        // TODO: Support multiple
	int xr_supported = 0;
	for(int i = 0; i < xr_extension_count; i++ ) {
		if (!strcmp("XR_KHR_opengl_es_enable", xr_extension_properties[i].extensionName)) {
			xr_supported = 1;
		}
	}
        assert(xr_supported);
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

        XrInstance xr_instance;
	result = xrCreateInstance(&instance_desc, &xr_instance);
        assert(XR_SUCCEEDED(result));

        // #ifndef NDEBUG
        XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
        instance_props.next = NULL;

        result = xrGetInstanceProperties(xr_instance, &instance_props);
        assert(XR_SUCCEEDED(result));

        printf("Runtime Name: %s\n", instance_props.runtimeName);
        printf("Runtime Name: %s\n", instance_props.runtimeName);
        printf("Runtime Version: %d.%d.%d\n",
                XR_VERSION_MAJOR(instance_props.runtimeVersion),
                XR_VERSION_MINOR(instance_props.runtimeVersion),
                XR_VERSION_PATCH(instance_props.runtimeVersion));
        // #endif

        // Enumerate API Layers
        XrApiLayerProperties xr_layer_props[64];
        uint32_t xr_layer_count;
        result = xrEnumerateApiLayerProperties(0, &xr_layer_count, NULL);
        assert(XR_SUCCEEDED(result));

        for (int i=0; i < xr_layer_count; i++) {
                xr_layer_props[i].type = XR_TYPE_API_LAYER_PROPERTIES;
                xr_layer_props[i].next = NULL;
        }

        result = xrEnumerateApiLayerProperties(xr_layer_count, &xr_layer_count, xr_layer_props);
        assert(XR_SUCCEEDED(result));
        // #ifndef NDEBUG
        printf("OpenXR API Layers: %d\n", xr_layer_count);
        for (int i=0; i < xr_layer_count; i++) {
                printf("        %s, %s\n", xr_layer_props[i].layerName, xr_layer_props[i].description);
        }
        // #endif

        // Get system
	XrSystemGetInfo system_desc = { XR_TYPE_SYSTEM_GET_INFO };
	system_desc.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	system_desc.next = NULL;

        XrSystemId xr_system;
	result = xrGetSystem(xr_instance, &system_desc, &xr_system);
        assert(XR_SUCCEEDED(result));

        // #ifndef NDEBUG
        XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };

        result = xrGetSystemProperties(xr_instance, xr_system, &system_props);
        assert(XR_SUCCEEDED(result));

        printf("System properties for system \"%s\":\n", system_props.systemName);
        printf("	maxLayerCount: %d\n", system_props.graphicsProperties.maxLayerCount);
        printf("	maxSwapChainImageHeight: %d\n", system_props.graphicsProperties.maxSwapchainImageHeight);
        printf("	maxSwapChainImageWidth: %d\n", system_props.graphicsProperties.maxSwapchainImageWidth);
        printf("	Orientation Tracking: %s\n", system_props.trackingProperties.orientationTracking ? "true" : "false");
        printf("	Position Tracking: %s\n", system_props.trackingProperties.positionTracking ? "true" : "false");
        // #endif

        // Enumerate Views
        uint32_t xr_view_count;
        XrViewConfigurationView xr_view_configs[8];
        result = xrEnumerateViewConfigurationViews(xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &xr_view_count, NULL);
        assert(XR_SUCCEEDED(result));
        assert(xr_view_count <= 8);

        for (int i=0; i < xr_view_count; i++) {
                xr_view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                xr_view_configs[i].next = NULL;
        }
        result = xrEnumerateViewConfigurationViews(xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, xr_view_count, &xr_view_count, xr_view_configs);
        assert(XR_SUCCEEDED(result));

        // #ifndef NDEBUG
        printf("%d view_configs:\n", xr_view_count);
        for (int i = 0; i < xr_view_count; i++) {
                printf("	view_configs[%d]:\n", i);
                printf("		recommendedImageRectWidth: %d\n", xr_view_configs[i].recommendedImageRectWidth);
                printf("		maxImageRectWidth: %d\n", xr_view_configs[i].maxImageRectWidth);
                printf("		recommendedImageRectHeight: %d\n", xr_view_configs[i].recommendedImageRectHeight);
                printf("		maxImageRectHeight: %d\n", xr_view_configs[i].maxImageRectHeight);
                printf("		recommendedSwapchainSampleCount: %d\n", xr_view_configs[i].recommendedSwapchainSampleCount);
                printf("		maxSwapchainSampleCount: %d\n", xr_view_configs[i].maxSwapchainSampleCount);
        }
        // #endif

        // Create the session
	PFN_xrGetOpenGLESGraphicsRequirementsKHR xr_gles_reqs_func;
	xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)(&xr_gles_reqs_func));

	XrGraphicsRequirementsOpenGLESKHR xr_gles_reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
	result = xr_gles_reqs_func(xr_instance, xr_system, &xr_gles_reqs);
        assert(XR_SUCCEEDED(result));

	const XrVersion egl_version = XR_MAKE_VERSION(3, 2, 0);
        assert(egl_version >= xr_gles_reqs.minApiVersionSupported && egl_version <= xr_gles_reqs.maxApiVersionSupported);

	XrGraphicsBindingOpenGLESAndroidKHR gl_binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
	gl_binding.display = egl_display;
	gl_binding.config = egl_config;
	gl_binding.context = egl_context;

	XrSessionCreateInfo session_desc = { XR_TYPE_SESSION_CREATE_INFO };
	session_desc.next = &gl_binding;
	session_desc.systemId = xr_system;

        XrSession xr_session;
	result = xrCreateSession(xr_instance, &session_desc, &xr_session);
        assert(XR_SUCCEEDED(result));

        // Create Space
        // #ifndef NDEBUG
        uint32_t xr_spaces_count;
        result = xrEnumerateReferenceSpaces(xr_session, 0, &xr_spaces_count, NULL);
        assert(XR_SUCCEEDED(result));

        XrReferenceSpaceType xr_spaces[64];
        for (int i = 0; i < xr_spaces_count; i++) {
                xr_spaces[i] = XR_REFERENCE_SPACE_TYPE_VIEW;
        }

        result = xrEnumerateReferenceSpaces(xr_session, xr_spaces_count, &xr_spaces_count, xr_spaces);
        assert(XR_SUCCEEDED(result));

        printf("reference_spaces:\n");
        for (int i = 0; i < xr_spaces_count; i++) {
                switch (xr_spaces[i]) {
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
                        printf("	XR_REFERENCE_SPACE_TYPE_%d\n", xr_spaces[i]);
                        break;
                }
        }
        // #endif

	XrPosef identity_pose = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
	XrReferenceSpaceCreateInfo space_desc;
	space_desc.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	space_desc.next = NULL;
	space_desc.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	space_desc.poseInReferenceSpace = identity_pose;

        XrSpace xr_stage_space;
	result = xrCreateReferenceSpace(xr_session, &space_desc, &xr_stage_space);
        assert(XR_SUCCEEDED(result));
        
        // Create Actions
	XrActionSetCreateInfo action_set_desc;
	action_set_desc.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	action_set_desc.next = NULL;
	strcpy(action_set_desc.actionSetName, "gameplay");
	strcpy(action_set_desc.localizedActionSetName, "Gameplay");
	action_set_desc.priority = 0;

        XrActionSet xr_action_set;
	result = xrCreateActionSet(xr_instance, &action_set_desc, &xr_action_set);
        assert(XR_SUCCEEDED(result));

        XrPath xr_hand_paths[2];
        XrPath xr_squeeze_value_paths[2];
        XrPath xr_trigger_value_paths[2];
        XrPath xr_pose_paths[2];
        XrPath xr_haptic_paths[2];
        XrPath xr_menu_click_paths[2];

	xrStringToPath(xr_instance, "/user/hand/left", &xr_hand_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right", &xr_hand_paths[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/squeeze/value",  &xr_squeeze_value_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/squeeze/value", &xr_squeeze_value_paths[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/trigger/value",  &xr_trigger_value_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/trigger/value", &xr_trigger_value_paths[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/grip/pose", &xr_pose_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/grip/pose", &xr_pose_paths[1]);
	xrStringToPath(xr_instance, "/user/hand/left/output/haptic", &xr_haptic_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right/output/haptic", &xr_haptic_paths[1]);
	xrStringToPath(xr_instance, "/user/hand/left/input/menu/click", &xr_menu_click_paths[0]);
	xrStringToPath(xr_instance, "/user/hand/right/input/menu/click", &xr_menu_click_paths[1]);

        XrActionCreateInfo grab_desc;
	grab_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	grab_desc.next = NULL;
	grab_desc.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(grab_desc.actionName, "grab_object" );
	strcpy(grab_desc.localizedActionName, "Grab Object");
	grab_desc.countSubactionPaths = 2;
	grab_desc.subactionPaths = xr_hand_paths;
        XrAction xr_grab_action;
	result = xrCreateAction(xr_action_set, &grab_desc, &xr_grab_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo trigger_desc;
	trigger_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	trigger_desc.next = NULL;
	trigger_desc.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	strcpy(trigger_desc.actionName, "trigger" );
	strcpy(trigger_desc.localizedActionName, "Trigger");
	trigger_desc.countSubactionPaths = 2;
	trigger_desc.subactionPaths = xr_hand_paths;
        XrAction xr_trigger_action;
	result = xrCreateAction(xr_action_set, &trigger_desc, &xr_trigger_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo click_desc;
	click_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	click_desc.next = NULL;
	click_desc.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(click_desc.actionName, "trigger_click" );
	strcpy(click_desc.localizedActionName, "Trigger Click");
	click_desc.countSubactionPaths = 2;
	click_desc.subactionPaths = xr_hand_paths;
        XrAction xr_trigger_click_action;
	result = xrCreateAction(xr_action_set, &click_desc, &xr_trigger_click_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo pose_desc;
	pose_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	pose_desc.next = NULL;
	pose_desc.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(pose_desc.actionName, "hand_pose" );
	strcpy(pose_desc.localizedActionName, "Hand Pose");
	pose_desc.countSubactionPaths = 2;
	pose_desc.subactionPaths = xr_hand_paths;
        XrAction xr_pose_action;
	result = xrCreateAction(xr_action_set, &pose_desc, &xr_pose_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo vibrate_desc;
	vibrate_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	vibrate_desc.next = NULL;
	vibrate_desc.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	strcpy(vibrate_desc.actionName, "vibrate_hand" );
	strcpy(vibrate_desc.localizedActionName, "Vibrate Hand");
	vibrate_desc.countSubactionPaths = 2;
	vibrate_desc.subactionPaths = xr_hand_paths;
        XrAction xr_vibrate_action;
	result = xrCreateAction(xr_action_set, &vibrate_desc, &xr_vibrate_action);
        assert(XR_SUCCEEDED(result));

        XrActionCreateInfo menu_desc;
	menu_desc.type = XR_TYPE_ACTION_CREATE_INFO;
	menu_desc.next = NULL;
	menu_desc.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(menu_desc.actionName, "quit_session" );
	strcpy(menu_desc.localizedActionName, "Menu Button");
	menu_desc.countSubactionPaths = 2;
	menu_desc.subactionPaths = xr_hand_paths;
        XrAction xr_menu_action;
	result = xrCreateAction(xr_action_set, &menu_desc, &xr_menu_action);
        assert(XR_SUCCEEDED(result));

        // Oculus Touch Controller Interaction Profile
        XrPath touch_controller_path;
        xrStringToPath(xr_instance, "/interaction_profiles/oculus/touch_controller", &touch_controller_path);
        XrActionSuggestedBinding bindings[] = {
                {xr_grab_action, xr_squeeze_value_paths[0]},
                {xr_grab_action, xr_squeeze_value_paths[1]},
                {xr_trigger_action, xr_trigger_value_paths[0]},
                {xr_trigger_action, xr_trigger_value_paths[1]},
                {xr_trigger_click_action, xr_trigger_value_paths[0]},
                {xr_trigger_click_action, xr_trigger_value_paths[1]},
                {xr_pose_action, xr_pose_paths[0]},
                {xr_pose_action, xr_pose_paths[1]},
                {xr_menu_action, xr_menu_click_paths[0]},
                {xr_vibrate_action, xr_haptic_paths[0]},
                {xr_vibrate_action, xr_haptic_paths[1]}
        };

        XrInteractionProfileSuggestedBinding suggested_bindings;
        suggested_bindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggested_bindings.next = NULL;
        suggested_bindings.interactionProfile = touch_controller_path;
        suggested_bindings.suggestedBindings = bindings;
        suggested_bindings.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
        result = xrSuggestInteractionProfileBindings(xr_instance, &suggested_bindings);
        assert(XR_SUCCEEDED(result));

	XrActionSpaceCreateInfo action_space_desc = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
	action_space_desc.action = xr_pose_action;
	XrPosef identity = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
	action_space_desc.poseInActionSpace = identity;

        XrSpace xr_hand_spaces[2];

	action_space_desc.subactionPath = xr_hand_paths[0];
	result = xrCreateActionSpace(xr_session, &action_space_desc, &xr_hand_spaces[0]);
        assert(XR_SUCCEEDED(result));

	action_space_desc.subactionPath = xr_hand_paths[1];
	result = xrCreateActionSpace(xr_session, &action_space_desc, &xr_hand_spaces[1]);
        assert(XR_SUCCEEDED(result));

	XrSessionActionSetsAttachInfo session_actions_desc;
	session_actions_desc.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	session_actions_desc.next = NULL;
	session_actions_desc.countActionSets = 1;
	session_actions_desc.actionSets = &xr_action_set;
	result = xrAttachSessionActionSets(xr_session, &session_actions_desc);

        // Create Swapchains
        int32_t xr_swapchain_widths[8];
        int32_t xr_swapchain_heights[8];
        XrSwapchain xr_swapchains[8];
        uint32_t xr_swapchain_lengths[8];
	XrSwapchainImageOpenGLESKHR xr_swapchain_images[8][4];

        uint32_t swapchain_format_count;
        result = xrEnumerateSwapchainFormats(xr_session, 0, &swapchain_format_count, NULL);
        assert(XR_SUCCEEDED(result));

        int64_t swapchain_formats[128];
        result = xrEnumerateSwapchainFormats(xr_session, swapchain_format_count, &swapchain_format_count, swapchain_formats);
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

	for (int i = 0; i < xr_view_count; i++) {
		XrSwapchainCreateInfo swapchain_desc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchain_desc.createFlags = 0;
		swapchain_desc.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchain_desc.format = selected_format;
		swapchain_desc.sampleCount = 1;
		swapchain_desc.width = xr_view_configs[i].recommendedImageRectWidth;
		swapchain_desc.height = xr_view_configs[i].recommendedImageRectHeight;
		swapchain_desc.faceCount = 1;
		swapchain_desc.arraySize = 1;
		swapchain_desc.mipCount = 1;

		result = xrCreateSwapchain(xr_session, &swapchain_desc, &xr_swapchains[i]);
                assert(XR_SUCCEEDED(result));

                xr_swapchain_widths[i] = swapchain_desc.width;
                xr_swapchain_heights[i] = swapchain_desc.height;

                result = xrEnumerateSwapchainImages(xr_swapchains[i], 0, &xr_swapchain_lengths[i], NULL);
                assert(XR_SUCCEEDED(result));
		
		for (int j = 0; j < xr_swapchain_lengths[i]; j++) {
			xr_swapchain_images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
			xr_swapchain_images[i][j].next = NULL;
		}

                XrSwapchainImageBaseHeader* image_header = (XrSwapchainImageBaseHeader*)(&xr_swapchain_images[i][0]);
		result = xrEnumerateSwapchainImages(xr_swapchains[i], xr_swapchain_lengths[i], &xr_swapchain_lengths[i], image_header);
                assert(XR_SUCCEEDED(result));
	}

        //#ifndef NDEBUG
        printf("Swapchains:\n");
        for (int i = 0; i < xr_view_count; i++) {
                printf("        width: %d\n", xr_swapchain_widths[i]);
                printf("        height: %d\n", xr_swapchain_heights[i]);
                printf("        length: %d\n", xr_swapchain_lengths[i]);
        }
        //#endif

        // Create Framebuffers
        uint32_t xr_colour_targets[8];
        uint32_t xr_depth_targets[8];
        uint32_t xr_framebuffers[8];
        
        uint32_t xr_blit_framebuffer;
        glGenFramebuffers(1, &xr_blit_framebuffer);

        for (int i=0; i < xr_view_count; i++) {
                int width = xr_swapchain_widths[i];
                int height = xr_swapchain_heights[i];

                glGenFramebuffers(1, &xr_framebuffers[i]);
                glBindFramebuffer(GL_FRAMEBUFFER, xr_framebuffers[i]);

                glGenTextures(1, &xr_colour_targets[i]);
                glBindTexture(GL_TEXTURE_2D, xr_colour_targets[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr_colour_targets[i], 0);  

                glGenTextures(1, &xr_depth_targets[i]);
                glBindTexture(GL_TEXTURE_2D, xr_depth_targets[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, xr_depth_targets[i], 0); 

                uint32_t draw_bufs[1] = {GL_COLOR_ATTACHMENT0};
                glDrawBuffers(1, draw_bufs);

                assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        }

        XrSessionState xr_session_state = XR_SESSION_STATE_UNKNOWN;
        bool is_running = true;
        bool is_session_ready = false;
        bool is_session_begin_ever = false;

        // Main Loop
        while (is_running) {
                // Pump Android Event Loop
                int events;
                struct android_poll_source *source;
                while (ALooper_pollAll(0, 0, &events, (void **)&source) >= 0 ) {
                        if (source != NULL) {
                                source->process( gapp, source );
                        }
                }

                // Pump OpenXR Event Loop
                bool is_remaining_events = true;
                while (is_remaining_events) {
                        XrEventDataBuffer xr_event = { XR_TYPE_EVENT_DATA_BUFFER };
                        XrResult result = xrPollEvent(xr_instance, &xr_event);
                        if (result != XR_SUCCESS) {
                                is_remaining_events = false;
                                continue;
                        }

                        switch (xr_event.type) {
                        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                                printf("xr_event: XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING\n");
                                break;
                        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                                printf("xr_event: XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED -> ");
                                XrEventDataSessionStateChanged* ssc = (XrEventDataSessionStateChanged*)&xr_event;
                                xr_session_state = ssc->state;
                                switch (xr_session_state) {
                                case XR_SESSION_STATE_IDLE:
                                        printf("XR_SESSION_STATE_IDLE\n");
                                        break;
                                case XR_SESSION_STATE_READY:
                                        printf("XR_SESSION_STATE_READY\n");
                                        XrSessionBeginInfo begin_desc;
                                        begin_desc.type = XR_TYPE_SESSION_BEGIN_INFO;
                                        begin_desc.next = NULL;
                                        begin_desc.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                                        result = xrBeginSession(xr_session, &begin_desc);
                                        assert(XR_SUCCEEDED(result));
                                        is_session_begin_ever = true;
                                        is_session_ready = true;
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
                                        printf("XR_SESSION_STATE_??? %d\n", (int)xr_session_state);
                                        break;
                                }
                                break;
                        }
                        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                                printf("XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING\n");
                                break;
                        case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                                printf("xr_event: XR_TYPE_EVENT_DATA_EVENTS_LOST\n");
                                break;
                        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                                printf("XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED\n");
                                break;
                        default:
                                printf("Unhandled event type %d\n", xr_event.type);
                                break;
                        }
                }

                if (!is_session_ready) { continue; }

                // Sync Input
                XrActiveActionSet active_action_set;
                active_action_set.actionSet = xr_action_set;
                active_action_set.subactionPath = XR_NULL_PATH;
                XrActionsSyncInfo action_sync_info;
                action_sync_info.type = XR_TYPE_ACTIONS_SYNC_INFO;
                action_sync_info.next = NULL;
                action_sync_info.countActiveActionSets = 1;
                action_sync_info.activeActionSets = &active_action_set;
                result = xrSyncActions(xr_session, &action_sync_info);
                assert(XR_SUCCEEDED(result));

                // Render Frame
                XrFrameState frame_state;
                frame_state.type = XR_TYPE_FRAME_STATE;
                frame_state.next = NULL;

                XrFrameWaitInfo frame_wait;
                frame_wait.type = XR_TYPE_FRAME_WAIT_INFO;
                frame_wait.next = NULL;

                result = xrWaitFrame(xr_session, &frame_wait, &frame_state);
                assert(XR_SUCCEEDED(result));

                XrFrameBeginInfo frame_begin;
                frame_begin.type = XR_TYPE_FRAME_BEGIN_INFO;
                frame_begin.next = NULL;
                result = xrBeginFrame(xr_session, &frame_begin);
                assert(XR_SUCCEEDED(result));

                int layer_count = 0;
                XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
                layer.layerFlags = 0; //XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                layer.next = NULL;
                layer.space = xr_stage_space;
                const XrCompositionLayerBaseHeader * layers[1] = { (XrCompositionLayerBaseHeader *)&layer };

                XrView views[8];
                for (int i=0; i < xr_view_count; i++) {
                        views[i].type = XR_TYPE_VIEW;
                        views[i].next = NULL;
                }
                
                uint32_t view_count_out;
                XrViewState view_state = { XR_TYPE_VIEW_STATE };

                XrViewLocateInfo view_locate_info;
                view_locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
                view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                view_locate_info.displayTime = frame_state.predictedDisplayTime;
                view_locate_info.space = xr_stage_space;
                result = xrLocateViews(xr_session, &view_locate_info, &view_state, xr_view_count, &view_count_out, views);
                assert(XR_SUCCEEDED(result));

                XrCompositionLayerProjectionView projection_layer_views[8]{};

                for (int i = 0; i < view_count_out; i++) {
                        projection_layer_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                        projection_layer_views[i].pose = views[i].pose;
                        projection_layer_views[i].fov = views[i].fov;
                        projection_layer_views[i].subImage.swapchain = xr_swapchains[i];
                        projection_layer_views[i].subImage.imageRect.offset.x = 0;
                        projection_layer_views[i].subImage.imageRect.offset.y = 0;
                        projection_layer_views[i].subImage.imageRect.extent.width = xr_swapchain_widths[i];
                        projection_layer_views[i].subImage.imageRect.extent.height = xr_swapchain_heights[i];
                        projection_layer_views[i].subImage.imageArrayIndex = 0;
                }

                if (frame_state.shouldRender) {
                        for (int v = 0; v < view_count_out; v++) {
                                // Acquire and wait for the swapchain image
                                uint32_t image_index;
                                XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                                result = xrAcquireSwapchainImage(xr_swapchains[v], &acquire_info, &image_index);
                                assert(XR_SUCCEEDED(result));

                                XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                                wait_info.timeout = XR_INFINITE_DURATION;
                                result = xrWaitSwapchainImage(xr_swapchains[v], &wait_info);
                                assert(XR_SUCCEEDED(result));

                                XrSwapchainImageOpenGLESKHR swapchain_image = xr_swapchain_images[v][image_index];
                                uint32_t colour_tex = swapchain_image.image;
                                int width = projection_layer_views[v].subImage.imageRect.extent.width;
                                int height =  projection_layer_views[v].subImage.imageRect.extent.height;

                                // Render to the swapchain directly
                                glBindFramebuffer(GL_FRAMEBUFFER, xr_blit_framebuffer);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_tex, 0);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, xr_depth_targets[v], 0);
                                glViewport(0, 0, width, height);
                                glClearColor(v == 0 ? 1 : 0, v == 0 ? 1 : 0, v == 0 ? 0 : 1, 1);
                                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                                XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                                result = xrReleaseSwapchainImage(xr_swapchains[v], &release_info);
                                assert(XR_SUCCEEDED(result));
                        }

                        layer.viewCount = view_count_out;
                        layer.views = projection_layer_views;
                        layer_count = 1;
                }

                XrFrameEndInfo frame_end = { XR_TYPE_FRAME_END_INFO };
                frame_end.displayTime = frame_state.predictedDisplayTime;
                frame_end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                frame_end.layerCount = layer_count;
                frame_end.layers = layers;

                result = xrEndFrame(xr_session, &frame_end);
                assert(XR_SUCCEEDED(result));
        }


        // Clean up
        // Destroy Swapchains

	result = xrDestroySpace(xr_stage_space);
        assert(XR_SUCCEEDED(result));

        if (is_session_begin_ever) {
                result = xrEndSession(xr_session);
                assert(XR_SUCCEEDED(result));
        }
	
        result = xrDestroySession(xr_session);
        assert(XR_SUCCEEDED(result));

	result = xrDestroyInstance(xr_instance);
        assert(XR_SUCCEEDED(result));

        return;
}
