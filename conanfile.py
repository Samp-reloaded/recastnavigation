import os

from conan import ConanFile
from conan.tools.files import copy, save


class RecastNavigationSource(ConanFile):
    name = "recastnavigation-source"
    version = "2.0.0-dev.1"
    package_type = "header-library"
    license = "Zlib"
    description = "C++23 Recast Navigation source modules; compiled in the consumer build graph"
    url = "https://github.com/Samp-reloaded/recastnavigation"
    homepage = "https://github.com/Samp-reloaded/recastnavigation"
    exports_sources = "Modern/*", "Recast/*", "License.txt", "UPSTREAM.md"
    no_copy_source = True

    def package(self):
        copy(self, "*", src=os.path.join(self.source_folder, "Modern"),
             dst=os.path.join(self.package_folder, "src", "Modern"))
        copy(self, "*", src=os.path.join(self.source_folder, "Recast"),
             dst=os.path.join(self.package_folder, "src", "Recast"))
        copy(self, "License.txt", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "UPSTREAM.md", src=self.source_folder,
             dst=os.path.join(self.package_folder, "src"))
        save(self, os.path.join(self.package_folder, "res", "RecastNavigationSource.cmake"),
             'get_filename_component(RECASTNAVIGATION_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../src" ABSOLUTE)\n')

    def package_info(self):
        self.cpp_info.includedirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.bindirs = []
        self.cpp_info.builddirs = ["res"]
        self.cpp_info.set_property("cmake_file_name", "RecastNavigationSource")
        self.cpp_info.set_property("cmake_target_name", "RecastNavigationSource::Sources")
        self.cpp_info.set_property("cmake_build_modules", ["res/RecastNavigationSource.cmake"])
