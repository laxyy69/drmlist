# drmlist
drmlist is a simple program designed to interface directly with Linux's DRM (Direct Rendering Manager) devices through system calls, bypassing the use of libdrm.
### What does it do?
* **Listing Connectors and Modes:** drmlist allows you to retrieve a comprehensive list of connectors and available modes on the DRM device.
* **Direct Graphics Rendering:** Choose your desired mode and render graphics directly onto the screen without relying on X11 or Wayland, utilizing the DRM device.

### What I've learned:
drmlist has been an educational journey for me, providing insights into:
* **User-Space Interaction with GPU Drivers:** Understanding how user-space interacts with GPU drivers on Linux.
* **Significance of Graphics Libraries:** Exploring the purpose behind the existence of graphics libraries like OpenGL and Vulkan.
* **Underlying Operations of OpenGL, Vulkan, etc.:** Gaining a deeper understanding of what OpenGL, Vulkan, and similar technologies actually accomplish under the hood.

## Resources:
Where I learned and extended from:
* [OS development using the Linux kernel - DRM/KMS Video Modes (Part 9)](https://youtu.be/86tz5m0hy9M?si=hNEc5favrPHnsKOZ)
* [OS development using the Linux kernel - DRM/KMS Drawing (Part 10)](https://youtu.be/wjnLBjLM2QQ?si=iXOjcDPIBWoVoFbF)
