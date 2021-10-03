# HellTech Engine
An WIP exploration of the modern rendering landscape with Vulkan 1.2+ on Windows.


### REQUIREMENTS:
- Win10+
- Vulkan1.2+
- dedicated GPU ( ?, prefferably NV )


### DEPENDENCIES :
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [cgltf](https://github.com/jkuhlmann/cgltf)
- [spng](https://github.com/randy408/libspng)
- [spirv_reflect](https://github.com/KhronosGroup/SPIRV-Reflect)

### POINTS OF INTEREST ( WIP ):
- custom binary file format + asset compiler
- instance culling ( frustum + occlusion )
- meshlet culling ( frustum + occlusion )
- occlusion culling via HiZ buffer
- merge the triangles of the surviving meshlets into one index buffer
- 3 bytes tanget frames
- resource access in shaders relies on bindless or BDAs ( sometimes push descriptors too )

![HellTech Engine 8_27_2021 12_35_42 PM](https://user-images.githubusercontent.com/32171756/135079403-c1c025b4-bb22-4181-a33a-0a49b469a5e6.png)
![HellTech Engine 9_2_2021 1_43_40 PM](https://user-images.githubusercontent.com/32171756/135079505-5b91c42c-8445-46d4-b7e2-c3f41124a4a9.png)
![HellTech Engine 10_3_2021 1_15_57 PM](https://user-images.githubusercontent.com/32171756/135749331-4a191c8f-d44b-473b-baba-4361418860cc.png)
