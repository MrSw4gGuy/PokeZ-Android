# letsgo-recomp frame generation (optical flow)

`runtime/nvn/vulkan/shaders.c` (`fg_*.frag`) and `runtime/nvn/vulkan/exec.c` (`fg_*`) adapt the optical-flow frame
generation of [letsgo-recomp](https://github.com/raulzrrs/letsgo-recomp) (MIT, see `LICENSE`) to work from the
presented images alone: block matching on the luminance, a per-pixel pick, and a warp of the two real frames to the
half-way time. Their motion-vector, depth and interface-mask inputs are left out.
