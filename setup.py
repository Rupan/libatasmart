#!/usr/bin/python

# http://docs.python.org/2/distutils/apiref.html
# use set_libraries(libnames) or add_library(libname) for libatasmart

from distutils.core import setup, Extension

atasmart = Extension('atasmart',
                     libraries = ['atasmart', 'pthread'],
                     sources = ['atasmart_py.c'])

setup(name='atasmart',
      version='0.1.0',
      description = 'libatasmart Python bindings',
      author = 'Michael Mohr',
      author_email = 'akihana@gmail.com',
      url = 'http://0pointer.de/blog/projects/being-smart.html',
      ext_modules=[atasmart])
