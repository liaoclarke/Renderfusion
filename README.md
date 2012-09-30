Renderfusion
============


This is project RenderFusion, welcoming to play it!

## Purpose

Porting Android renderscript to AMD APU platform, for now, renderscript only support perform data parallelism
for Java program on _CPU_ only, our task is making renderscript generates native GPU-instruction which feed to
APU.

Hardware Platform: 
APU: APU-E350(Brazos, Zacate)
CPU: 2 Cores, Freq 1.6 GhHz, L2 cache 2x512KB 
GPU: Radeon HD6310(Terascale2, Evergreen family)

Software Stack:
Mesa(opengl)
LLVM(IR)
Gallium(back-end) 
Android libdrm
GNU/Linux Kernel(drm) 
Graphics accelerator driver(radeon)

## Git
git clone git://github.com/liaoclarke/Renderfusion.git
