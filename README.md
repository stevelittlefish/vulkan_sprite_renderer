# Vulkan Sprite Renderer

2D sprite renderer in plain C using the vulkan API

This project is a plain c implementation of a 2D sprite renderer using the Vulkan API.  I started by following one of
the Vulkan tutorials, and ended up making a lot of changes, mainly:

- Using SDL3 instead of GLFW
- Using Vulkan 1.3 instead of 1.0
- Dynamic rendering
- Sychronisation2
- Offscreen rendering / post processing
- Multiple piplines
- Generating sprite vertices in the vertex shader

I make no claims that this is well organised or setup perfectly.  I tried to tidy a few things up but I'm still not
sure the best way to organise a Vulkan project.

## Linux Instructions

You will need the following dependencies:

- gcc
- make
- cmake
- vulkan
- sdl3
- cglm

To get everything working you should just need to do (from the root of the project):

```bash
./compile_shaders.sh
./build_and_run.sh
```

## Windows Instructions

You will need the following dependencies:

- The Vulkan SDK (installed in the default location on your C drive)
- Microsoft Visual Studio community edition

To get it working, first double click on `win_compile_shaders.bat` to compile the shaders.  Then load the folder in Visual Studio.

If you forget to compile the shaders first, CMake won't copy them into the right location.  Either shut down Visual Studio,
delete the .vs and out directories and load the folder again, or make some trivial change to the CMakeLists.txt file (i.e.
add a space) to get it to reload.  There probably is a better way to do this but I don't really develop on Windows and I'm a
Visual Studio noob!

# Known Issues

The sync objects are not managed correctly and may result in sync issues depending on your graphics card / driver.

Currently all sync objects (semaphores and fences) are created and indexed per frame in flight.  This is actually incorrect.

The fence and image available semaphores should be one per frame in flight, but the render fishished semaphore should be per swap chain image.

See [Odin Vulkan Sprite Renderer](https://github.com/stevelittlefish/odin_vulkan_sprite_renderer) for a better implementation (in the Odin programming language).
