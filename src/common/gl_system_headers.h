// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The platform's own GL declarations: desktop OpenGL, or OpenGL ES 3.2 on Android. Code that calls
// GL directly includes this instead of naming either set of headers.
#if defined(__ANDROID__)
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#else
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
