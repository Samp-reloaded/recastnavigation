cmake_minimum_required(VERSION 3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(RecastNavigationModern VERSION 2.0.0 LANGUAGES CXX)
endif()

function(recastnav_component target alias)
    cmake_parse_arguments(PARSE_ARGV 2 COMPONENT "" "" "INTERFACES;IMPLEMENTATIONS;DEPENDENCIES;PRIVATE_INCLUDES")
    add_library(${target}_api OBJECT)
    target_sources(${target}_api PUBLIC FILE_SET modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${COMPONENT_INTERFACES})
    add_library(${target}_impl OBJECT ${COMPONENT_IMPLEMENTATIONS})
    target_include_directories(${target}_impl PRIVATE ${COMPONENT_PRIVATE_INCLUDES})
    target_link_libraries(${target}_impl PRIVATE "$<COMPILE_ONLY:${target}_api>")

    foreach(producer IN ITEMS ${target}_api ${target}_impl)
        target_compile_features(${producer} PUBLIC cxx_std_23)
        set_target_properties(${producer} PROPERTIES CXX_SCAN_FOR_MODULES ON UNITY_BUILD OFF
            DISABLE_PRECOMPILE_HEADERS ON FOLDER "RecastNavigation/compile")
        if(COMMAND gtasa_apply_module_consumer)
            gtasa_apply_module_consumer(${producer})
            gtasa_apply_object_producer(${producer})
            gtasa_register_target_modules(${producer})
        else()
            set_property(TARGET ${producer} PROPERTY CXX_MODULE_STD ON)
        endif()
        foreach(dependency IN LISTS COMPONENT_DEPENDENCIES)
            target_link_libraries(${producer} PRIVATE "$<COMPILE_ONLY:${dependency}_api>")
        endforeach()
    endforeach()

    add_library(${target} STATIC)
    add_library(RecastNavigation::${alias} ALIAS ${target})
    target_sources(${target} PRIVATE "$<TARGET_OBJECTS:${target}_api>" "$<TARGET_OBJECTS:${target}_impl>")
    target_link_libraries(${target} PUBLIC "$<COMPILE_ONLY:${target}_api>")
    foreach(dependency IN LISTS COMPONENT_DEPENDENCIES)
        target_link_libraries(${target} PRIVATE "$<LINK_ONLY:${dependency}>")
    endforeach()
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX FOLDER "RecastNavigation/libraries")
endfunction()

recastnav_component(RecastCore Core
    INTERFACES Core/Core.ixx Core/Math.ixx Core/Document.ixx
    IMPLEMENTATIONS Core/Document.cpp)
recastnav_component(RecastCodec Codec
    INTERFACES Codec/TileCodec.ixx Codec/LayerCodec.ixx
    IMPLEMENTATIONS Codec/TileCodec.cpp Codec/LayerCodec.cpp
    DEPENDENCIES RecastCore)

recastnav_component(DetourMesh Mesh
    INTERFACES Detour/Mesh.ixx
    IMPLEMENTATIONS Detour/Mesh.cpp
    DEPENDENCIES RecastCore)
recastnav_component(DetourQuery Query
    INTERFACES Detour/Query.ixx
    IMPLEMENTATIONS Detour/Query.cpp
    DEPENDENCIES DetourMesh RecastCore)

recastnav_component(DetourTileCache TileCache
    INTERFACES TileCache/TileCache.ixx
    IMPLEMENTATIONS TileCache/TileCache.cpp
    DEPENDENCIES DetourMesh RecastCodec RecastCore)
recastnav_component(DetourCrowd Crowd
    INTERFACES Crowd/Crowd.ixx
    IMPLEMENTATIONS Crowd/Crowd.cpp
    DEPENDENCIES DetourQuery DetourMesh RecastCore)

set(recastnav_kernel_root "${CMAKE_CURRENT_SOURCE_DIR}/../Recast")
add_library(recastnav_cooker_kernel STATIC
    "${recastnav_kernel_root}/Source/Recast.cpp"
    "${recastnav_kernel_root}/Source/RecastArea.cpp"
    "${recastnav_kernel_root}/Source/RecastAssert.cpp"
    "${recastnav_kernel_root}/Source/RecastContour.cpp"
    "${recastnav_kernel_root}/Source/RecastFilter.cpp"
    "${recastnav_kernel_root}/Source/RecastLayers.cpp"
    "${recastnav_kernel_root}/Source/RecastMesh.cpp"
    "${recastnav_kernel_root}/Source/RecastMeshDetail.cpp"
    "${recastnav_kernel_root}/Source/RecastRasterization.cpp"
    "${recastnav_kernel_root}/Source/RecastRegion.cpp")
target_compile_features(recastnav_cooker_kernel PRIVATE cxx_std_23)
target_include_directories(recastnav_cooker_kernel PRIVATE "${recastnav_kernel_root}/Include")
target_compile_definitions(recastnav_cooker_kernel PRIVATE
    rcAlloc=recastnavCookAlloc rcFree=recastnavCookFree)
if(TARGET notsa_module_abi)
    target_link_libraries(recastnav_cooker_kernel PRIVATE notsa_module_abi)
endif()
set_target_properties(recastnav_cooker_kernel PROPERTIES
    CXX_SCAN_FOR_MODULES OFF UNITY_BUILD OFF DISABLE_PRECOMPILE_HEADERS ON
    FOLDER "RecastNavigation/private")

recastnav_component(RecastBuild Build
    INTERFACES Build/Build.ixx
    IMPLEMENTATIONS Build/Build.cpp Build/LegacyAllocation.cpp
    PRIVATE_INCLUDES "${recastnav_kernel_root}/Include"
    DEPENDENCIES RecastCore)
target_link_libraries(RecastBuild PRIVATE "$<LINK_ONLY:recastnav_cooker_kernel>")

if(COMMAND gtasa_apply_target_module_imports)
    foreach(producer IN ITEMS RecastCore_api RecastCore_impl RecastCodec_api RecastCodec_impl
            DetourMesh_api DetourMesh_impl DetourQuery_api DetourQuery_impl RecastBuild_api RecastBuild_impl
            DetourTileCache_api DetourTileCache_impl DetourCrowd_api DetourCrowd_impl)
        gtasa_apply_target_module_imports(${producer})
    endforeach()
endif()
