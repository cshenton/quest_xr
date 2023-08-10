// Editor Only, for now
// #define ANDROID
// #define EGLAPI
// #define GL_APICALL

#include "android_native_app_glue.h"
#include <jni.h>
#include <android/native_activity.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#define XR_USE_PLATFORM_ANDROID
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

extern "C" int __system_property_get( const char *__name, char *__value );

extern "C" void handle_cmd( struct android_app *app, int32_t cmd ) {
	switch ( cmd )
	{
	case APP_CMD_DESTROY:
		//This gets called initially after back.
		// HandleDestroy();
		ANativeActivity_finish( gapp->activity );
		break;
	case APP_CMD_INIT_WINDOW:
		//When returning from a back button suspension, this isn't called.
		if ( !g_window_is_init )
		{
			g_window_is_init = 1;
			printf( "Got start event\n" );
		}
		else
		{
			// CNFGSetup( "", -1, -1 );
			// HandleResume();
		}
		break;
		//case APP_CMD_TERM_WINDOW:
			//This gets called initially when you click "back"
			//This also gets called when you are brought into standby.
			//Not sure why - callbacks here seem to break stuff.
		//	break;
	default:
		printf( "event not handled: %d", cmd );
	}
}


extern "C" int32_t handle_input( struct android_app *app, AInputEvent *event ) {
	//Potentially do other things here.

	// if ( AInputEvent_getType( event ) == AINPUT_EVENT_TYPE_MOTION )
	// {
	// 	int action = AMotionEvent_getAction( event );
	// 	int whichsource = action >> 8;
	// 	action &= AMOTION_EVENT_ACTION_MASK;
	// 	size_t pointerCount = AMotionEvent_getPointerCount( event );

	// 	for ( size_t i = 0; i < pointerCount; ++i )
	// 	{
	// 		int x, y, index;
	// 		x = AMotionEvent_getX( event, i );
	// 		y = AMotionEvent_getY( event, i );
	// 		index = AMotionEvent_getPointerId( event, i );

	// 		if ( action == AMOTION_EVENT_ACTION_POINTER_DOWN || action == AMOTION_EVENT_ACTION_DOWN )
	// 		{
	// 			int id = index;
	// 			if ( action == AMOTION_EVENT_ACTION_POINTER_DOWN && id != whichsource ) continue;
	// 			// HandleButton( x, y, id, 1 );
	// 			ANativeActivity_showSoftInput( gapp->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED );
	// 		}
	// 		else if ( action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL )
	// 		{
	// 			int id = index;
	// 			if ( action == AMOTION_EVENT_ACTION_POINTER_UP && id != whichsource ) continue;
	// 			// HandleButton( x, y, id, 0 );
	// 		}
	// 		else if ( action == AMOTION_EVENT_ACTION_MOVE )
	// 		{
	// 			// HandleMotion( x, y, index );
	// 		}
	// 	}
	// 	return 1;
	// }
	// else if ( AInputEvent_getType( event ) == AINPUT_EVENT_TYPE_KEY )
	// {
	// 	int code = AKeyEvent_getKeyCode( event );
	// 	int unicode = AndroidGetUnicodeChar( code, AMotionEvent_getMetaState( event ) );
	// 	if ( unicode ) {
	// 		// HandleKey( unicode, AKeyEvent_getAction( event ) );
		
	// 	} else {
	// 		// HandleKey( code, !AKeyEvent_getAction( event ) );
	// 		return ( code == 4 ) ? 1 : 0; //don't override functionality.
	// 	}
	// 	return 1;
	// }
	return 0;
}

extern "C" void android_main(struct android_app *app) {
	printf("Window: %p", app->window);
	{
		char sdk_ver_str[92];
		int len = __system_property_get( "ro.build.version.sdk", sdk_ver_str );
		if ( len <= 0 )
			android_sdk_version = 0;
		else
			android_sdk_version = atoi( sdk_ver_str );
	}

	gapp = app;
	app->onAppCmd = handle_cmd;
	// app->onInputEvent = handle_input;
	printf( "Starting with Android SDK Version: %d\n", android_sdk_version );

 	EGLint egl_major, egl_minor;
	EGLint num_config;

	int events;
	while ( !g_window_is_init ) {
		struct android_poll_source *source;
		if ( ALooper_pollAll( 0, 0, &events, (void **)&source ) >= 0 )
		{
			if ( source != NULL ) source->process( gapp, source );
		}
	}
	printf("Getting Display\n");

	egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	if ( egl_display == EGL_NO_DISPLAY ) {
		printf( "Error: No display found!\n" );
		return;
	}

	printf("Initialising EGL\n");
	if ( !eglInitialize( egl_display, &egl_major, &egl_minor ) ) {
		printf( "Error: eglInitialise failed!\n" );
		return;
	}

	printf("g_window_is_init_2\n");
	printf( "EGL Version: \"%s\"\n", eglQueryString( egl_display, EGL_VERSION ) );
	printf( "EGL Vendor: \"%s\"\n", eglQueryString( egl_display, EGL_VENDOR ) );
	printf( "EGL Extensions: \"%s\"\n", eglQueryString( egl_display, EGL_EXTENSIONS ) );

	eglChooseConfig( egl_display, config_attribute_list, &egl_config, 1, &num_config );
	printf( "Config: %d\n", num_config );

	printf( "Creating Context\n" );
        static const EGLint context_attribute_list[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
        };
	egl_context = eglCreateContext( egl_display, egl_config, EGL_NO_CONTEXT, context_attribute_list );
	if ( egl_context == EGL_NO_CONTEXT ) {
		printf( "Error: eglCreateContext failed: 0x%08X\n",
				eglGetError() );
		return;
	}
	printf( "Context Created %p\n", egl_context );


	// Do I need this window code?
	if ( native_window && !gapp->window ) {
		printf( "WARNING: App restarted without a window.  Cannot progress.\n" );
		return;
	}

	printf( "Getting Surface %p\n", native_window = gapp->window );

	if ( !native_window ) {
		printf( "FAULT: Cannot get window\n" );
		return;
	}

        android_width = ANativeWindow_getWidth(native_window);
        android_height = ANativeWindow_getHeight(native_window);

	printf( "Width/Height: %dx%d\n", android_width, android_height );
        static EGLint window_attribute_list[] = {
                EGL_NONE
        };
	egl_surface = eglCreateWindowSurface( egl_display, egl_config, gapp->window, window_attribute_list );
	printf( "Got Surface: %p\n", egl_surface );

	if ( egl_surface == EGL_NO_SURFACE ) {
		printf("Error: eglCreateWindowSurface failed: " "0x%08X\n", eglGetError() );
		return;
	}

	if ( !eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context ) ) {
		printf( "Error: eglMakeCurrent() failed: 0x%08X\n", eglGetError() );
		return;
	}

	printf( "GL Vendor: \"%s\"\n", glGetString( GL_VENDOR ) );
	printf( "GL Renderer: \"%s\"\n", glGetString( GL_RENDERER ) );
	printf( "GL Version: \"%s\"\n", glGetString( GL_VERSION ) );
	printf( "GL Extensions: \"%s\"\n", glGetString( GL_EXTENSIONS ) );

	return;       
}
