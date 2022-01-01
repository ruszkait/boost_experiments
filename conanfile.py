import re, os
from conans import ConanFile, CMake, tools

class SensorDataPackage(ConanFile):
    name = 'boostTest'

    build_policy = 'never'
    generators = "cmake_find_package"

    settings = 'os', 'arch', 'compiler', 'build_type'

    def requirements(self):
        self.requires('boost/1.76.0')
        self.requires('gtest/1.11.0')

    def configure(self):
        self.options['boost'].shared = True
        self.options['googletest'].shared = False

    def create_cmake(self):
        cmake = CMake(self)
        cmake.verbose = True
        return cmake

    def imports(self):
        self.copy('lib/*')
        self.copy("*.dll")

    def build(self):
        cmake = self.create_cmake()
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = self.create_cmake()
        cmake.install()
